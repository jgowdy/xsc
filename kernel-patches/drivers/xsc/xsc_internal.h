/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XSC internal definitions - shared between module components
 * v8-D: Adds resource attribution, uvec, observability parity
 */

#ifndef XSC_INTERNAL_H
#define XSC_INTERNAL_H

#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/cgroup.h>
#include <linux/resource.h>
#include <linux/sched/signal.h>
#ifdef CONFIG_AUDIT
#include <linux/audit.h>
#endif
#include <linux/uio.h>
#include "xsc_uapi.h"

/* v8-D §2.3: Resource Attribution & Accounting */
struct xsc_task_cred {
	struct task_struct	*origin;	/* submitter at dequeue time */
	struct css_set		*origin_css;	/* cgroup v2 membership snapshot */
	struct rlimit		rlim[RLIM_NLIMITS]; /* rlimit snapshot */
	kuid_t			uid;
	kgid_t			gid;
	pid_t			pid;
	pid_t			tgid;
	u64			cgroup_id;
#ifdef CONFIG_AUDIT
	struct audit_context	*audit_ctx;
#endif
};

/* v8-D §2.4: User-Pointer Lifetime Model */
#define XSC_UVEC_COPY		0	/* COPY-BOUNCE (default) */
#define XSC_UVEC_PIN		1	/* PINNED (restricted) */

struct xsc_uvec {
	__u64			addr;
	__u32			len;
	__u32			flags;	/* XSC_UVEC_COPY | XSC_UVEC_PIN */
	struct page		**pages;	/* for PIN mode */
	int			nr_pages;	/* for PIN mode */
};

/* v8-D §5.2: Stable Tracepoint Field Definitions */
struct xsc_tp_enter {
	__u32			pid;
	__u32			tgid;
	__u64			cgroup_id;
	__u64			nr;		/* semantic syscall nr */
	__u64			args[6];	/* canonicalized arguments */
	__u64			ts_nsec;	/* monotonic */
};

struct xsc_tp_exit {
	__u32			pid;
	__u32			tgid;
	__s64			ret;		/* return or -errno */
	__u64			ts_nsec;
};

struct xsc_ring {
	void			*sq_ring;
	void			*cq_ring;
	void			*sqes;
	void			*cqes;

	u32			sq_entries;
	u32			cq_entries;

	u32			*sq_head;
	u32			*sq_tail;
	u32			*sq_mask;
	u32			*sq_flags;

	u32			*cq_head;
	u32			*cq_tail;
	u32			*cq_mask;
	u32			*cq_overflow;

	struct page		**sq_pages;
	struct page		**cq_pages;
	struct page		**sqe_pages;
	struct page		**cqe_pages;

	int			sq_npages;
	int			cq_npages;
	int			sqe_npages;
	int			cqe_npages;
};

struct xsc_ctx {
	struct xsc_ring		ring;
	struct work_struct	sq_work;
	struct workqueue_struct	*wq;
	spinlock_t		lock;
	wait_queue_head_t	cq_wait;
	struct file		*file;
	struct task_struct	*task;		/* Owner task */
	struct files_struct	*files;		/* Owner files */
	bool			polling;
	int			cpu;
};

/* Dispatch functions */
int xsc_dispatch_fs(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);
int xsc_dispatch_net(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);
int xsc_dispatch_timer(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);
int xsc_dispatch_sync(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);
int xsc_dispatch_exec(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);

/* v8-D §2.3: Resource Attribution Wrapper */
void xsc_run_with_attribution(struct xsc_ctx *ctx,
		       struct xsc_task_cred *tc,
		       void (*fn)(void *), void *arg);

/* v8-D §2.5: CQE Write with Batched STAC/CLAC */
int xsc_cqe_write(struct xsc_ctx *ctx, struct xsc_cqe *cqe, u32 cq_idx);

/* v8-D §2.4: User-memory helpers */
int xsc_uvec_setup(struct xsc_uvec *uv, u64 addr, u32 len, u32 flags);
void xsc_uvec_cleanup(struct xsc_uvec *uv);

/* v8-D §5: Observability - Tracepoints & Audit */
void xsc_trace_sys_enter(struct xsc_tp_enter *tpe);
void xsc_trace_sys_exit(struct xsc_tp_exit *tpx);
void xsc_audit_submit(struct xsc_task_cred *tc, u64 nr, u64 *args);
void xsc_audit_result(struct xsc_task_cred *tc, s64 ret);

/* v8-D §8.4: Lifecycle - Signals, Cancellation, Exec */
int xsc_check_signals(struct xsc_ctx *ctx);
void xsc_exec_barrier(struct xsc_ctx *ctx);
void xsc_cancel_pending_sqes(struct xsc_ctx *ctx);

/* v8-D §5.3: Seccomp at Consume */
int xsc_seccomp_check(struct xsc_task_cred *tc, u64 nr, u64 *args);

/* v8-D §10: SMT Isolation */
int xsc_worker_set_affinity(struct xsc_ctx *ctx, struct task_struct *worker);
void xsc_worker_clear_affinity(struct task_struct *worker);

/* v8-D §2.4: User-memory copy helpers */
int xsc_uvec_copy_to_user(struct xsc_uvec *uv, const void *src, size_t len);
int xsc_uvec_copy_from_user(struct xsc_uvec *uv, void *dest, size_t len);
int xsc_uvec_copy_to_user_ctx(struct xsc_ctx *ctx, struct xsc_uvec *uv,
			    const void *src, size_t len);
int xsc_uvec_copy_from_user_ctx(struct xsc_ctx *ctx, struct xsc_uvec *uv,
			      void *dest, size_t len);

/* Context-aware user copying helpers */
ssize_t xsc_copy_from_user_ctx(struct task_struct *origin, void *dst,
			       const void __user *src, size_t len);
ssize_t xsc_copy_to_user_ctx(struct task_struct *origin, void __user *dst,
			     const void *src, size_t len);
int xsc_iov_to_kvec_ctx(struct task_struct *origin,
			     const struct iovec __user *u_iov, unsigned int iovcnt,
			     struct kvec *kv, unsigned int *out_cnt, size_t *out_len);
void xsc_free_kvec_ctx(struct kvec *kv, unsigned int count);
int xsc_strndup_user_ctx(struct task_struct *origin,
			 const char __user *uname, char **kname, size_t max);

/* v8-D §2.3: Resource Attribution helpers */
void xsc_task_cred_snapshot(struct xsc_task_cred *tc, struct task_struct *origin);
void xsc_task_cred_release(struct xsc_task_cred *tc);
int xsc_check_rlimit(struct xsc_task_cred *tc, unsigned int resource,
		     unsigned long value);

/* v8-D §2.5: CQE batch write */
int xsc_cqe_write_batch(struct xsc_ctx *ctx, struct xsc_cqe *cqes,
			u32 *indices, u32 count);

#endif /* XSC_INTERNAL_H */
