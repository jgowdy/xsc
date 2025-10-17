// SPDX-License-Identifier: GPL-2.0
/*
 * XSC synchronization operation handlers
 */

#include <linux/futex.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/sched/mm.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include "xsc_internal.h"

/* External kernel functions exported for XSC */
extern int futex_wait(u32 __user *uaddr, unsigned int flags, u32 val,
		      ktime_t *abs_time, u32 bitset);
extern int futex_wake(u32 __user *uaddr, unsigned int flags, int nr_wake, u32 bitset);

static int xsc_handle_futex_wait(struct xsc_sqe *sqe)
{
	struct {
		u32 __user *uaddr;
		u32 val;
		struct __kernel_timespec __user *timeout;
		u32 bitset;
	} args;
	ktime_t t, *tp = NULL;
	struct __kernel_timespec ts;
	unsigned int flags = FUTEX_BITSET_MATCH_ANY;

	/* Copy arguments from userspace */
	if (copy_from_user(&args, (void __user *)sqe->addr, sizeof(args)))
		return -EFAULT;

	/* If timeout specified, convert to ktime */
	if (args.timeout) {
		if (copy_from_user(&ts, args.timeout, sizeof(ts)))
			return -EFAULT;

		/* Validate timespec */
		if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= NSEC_PER_SEC)
			return -EINVAL;

		t = timespec64_to_ktime((struct timespec64){
			.tv_sec = ts.tv_sec,
			.tv_nsec = ts.tv_nsec
		});
		tp = &t;
	}

	/* Use provided bitset or default to match any */
	if (args.bitset)
		flags = args.bitset;

	return futex_wait(args.uaddr, 0, args.val, tp, flags);
}

static int xsc_handle_futex_wake(struct xsc_sqe *sqe)
{
	struct {
		u32 __user *uaddr;
		int nr_wake;
		u32 bitset;
	} args;
	unsigned int flags = FUTEX_BITSET_MATCH_ANY;

	/* Copy arguments from userspace */
	if (copy_from_user(&args, (void __user *)sqe->addr, sizeof(args)))
		return -EFAULT;

	/* Validate nr_wake */
	if (args.nr_wake < 0)
		return -EINVAL;

	/* Use provided bitset or default to match any */
	if (args.bitset)
		flags = args.bitset;

	return futex_wake(args.uaddr, 0, args.nr_wake, flags);
}

int xsc_dispatch_sync(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	int ret;

	switch (sqe->opcode) {
	case XSC_OP_FUTEX_WAIT:
		ret = xsc_handle_futex_wait(sqe);
		break;

	case XSC_OP_FUTEX_WAKE:
		ret = xsc_handle_futex_wake(sqe);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}
