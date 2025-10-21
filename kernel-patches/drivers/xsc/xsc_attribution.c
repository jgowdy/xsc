// SPDX-License-Identifier: GPL-2.0
/*
 * XSC Resource Attribution & Accounting (v8-D §2.3)
 * Copyright (C) 2025
 *
 * Charges CPU time, IO, memory, PSI stalls, and rlimit checks to the
 * origin (submitting task), not the worker thread.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/cgroup.h>
#include <linux/resource.h>
#include <linux/sched/task.h>
#include <linux/memcontrol.h>
#include <linux/blk-cgroup.h>
#include <linux/audit.h>

#include "xsc_internal.h"

/*
 * xsc_task_cred_snapshot - Capture origin task credentials
 * @tc: target credential structure
 * @origin: submitting task
 *
 * Called at SQE dequeue time. Snapshots the submitting task's
 * credentials, cgroup membership, and rlimits for attribution.
 */
void xsc_task_cred_snapshot(struct xsc_task_cred *tc,
				   struct task_struct *origin)
{
	const struct cred *cred;

	memset(tc, 0, sizeof(*tc));

	get_task_struct(origin);
	tc->origin = origin;
	tc->pid = task_pid_vnr(origin);
	tc->tgid = task_tgid_vnr(origin);

	/* Snapshot cgroup v2 membership */
	rcu_read_lock();
	tc->origin_css = task_css_set(origin);
	if (tc->origin_css)
		css_set_get(tc->origin_css);

#ifdef CONFIG_CGROUPS
	tc->cgroup_id = cgroup_id(task_cgroup(origin, 0));
#else
	tc->cgroup_id = 0;
#endif
	rcu_read_unlock();

	/* Snapshot credentials */
	rcu_read_lock();
	cred = __task_cred(origin);
	tc->uid = cred->uid;
	tc->gid = cred->gid;
	rcu_read_unlock();
#ifdef CONFIG_AUDIT
	tc->audit_ctx = origin->audit_context;
#endif

	/* Snapshot rlimits */
	task_lock(origin);
	memcpy(tc->rlim, origin->signal->rlim, sizeof(tc->rlim));
	task_unlock(origin);
}
EXPORT_SYMBOL_GPL(xsc_task_cred_snapshot);

/*
 * xsc_task_cred_release - Release credential snapshot
 * @tc: credential structure to release
 *
 * Called after operation completes and CQE is posted.
 */
void xsc_task_cred_release(struct xsc_task_cred *tc)
{
	if (tc->origin_css) {
		css_set_put(tc->origin_css);
		tc->origin_css = NULL;
	}
	if (tc->origin) {
		put_task_struct(tc->origin);
		tc->origin = NULL;
	}
}
EXPORT_SYMBOL_GPL(xsc_task_cred_release);

/*
 * xsc_attribution_enter - Begin attributed execution
 * @tc: credentials for attribution
 *
 * Sets up context for charging resources to origin task/cgroup.
 * Called before executing the actual kernel helper.
 */
struct xsc_attr_guard {
#ifdef CONFIG_CGROUPS
	bool css_switched;
#endif
	struct xsc_ctx *ctx;
	struct task_struct *prev_origin;
#ifdef CONFIG_AUDIT
	struct audit_context *prev_audit;
#endif
};

static void xsc_attribution_enter(struct xsc_ctx *ctx,
				  struct xsc_task_cred *tc,
				  struct xsc_attr_guard *guard)
{
#ifdef CONFIG_CGROUPS
	guard->css_switched = false;
#endif
	guard->ctx = ctx;
	guard->prev_origin = current->xsc_origin;
	current->xsc_origin = tc->origin;
#ifdef CONFIG_AUDIT
	guard->prev_audit = current->audit_context;
	current->audit_context = tc->audit_ctx;
#else
	guard->prev_audit = NULL;
#endif

#ifdef CONFIG_CGROUPS
	if (tc->origin && ctx && ctx->task && tc->origin != current)
		guard->css_switched = !cgroup_attach_task_all(tc->origin, current);
#endif
}

static void xsc_attribution_exit(struct xsc_attr_guard *guard)
{
#ifdef CONFIG_AUDIT
	current->audit_context = guard->prev_audit;
#endif
#ifdef CONFIG_CGROUPS
	if (guard->css_switched && guard->ctx && guard->ctx->task)
		cgroup_attach_task_all(guard->ctx->task, current);
#endif
	current->xsc_origin = guard->prev_origin;
}

/*
 * xsc_run_with_attribution - Execute function with origin attribution
 * @tc: credentials for attribution (must be initialized via snapshot)
 * @fn: function to execute
 * @arg: argument to function
 *
 * v8-D §2.3 & Appendix B: This is the attribution wrapper that ensures
 * CPU time, IO, memory, PSI stalls, and rlimit checks are charged to
 * the origin task/cgroup, not the worker thread.
 */
void xsc_run_with_attribution(struct xsc_ctx *ctx,
		       struct xsc_task_cred *tc,
		       void (*fn)(void *), void *arg)
{
struct xsc_attr_guard guard;

	xsc_attribution_enter(ctx, tc, &guard);

	/* Execute the actual operation (e.g., vfs_read, sendmsg, etc.) */
	fn(arg);

	xsc_attribution_exit(&guard);
}
EXPORT_SYMBOL_GPL(xsc_run_with_attribution);

/*
 * xsc_check_rlimit - Check rlimit against snapshot
 * @tc: credentials with rlimit snapshot
 * @resource: RLIMIT_* constant
 * @value: value to check
 *
 * Returns: 0 if within limit, -EPERM if exceeded
 *
 * Used for RLIMIT_FSIZE, RLIMIT_MEMLOCK, RLIMIT_NOFILE, etc.
 */
int xsc_check_rlimit(struct xsc_task_cred *tc, unsigned int resource,
		     unsigned long value)
{
	if (resource >= RLIM_NLIMITS)
		return -EINVAL;

	if (value > tc->rlim[resource].rlim_cur)
		return -EPERM;

	return 0;
}
EXPORT_SYMBOL_GPL(xsc_check_rlimit);
