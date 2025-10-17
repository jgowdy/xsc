// SPDX-License-Identifier: GPL-2.0
/*
 * XSC timer/wait operation handlers
 */

#include <linux/poll.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/sched/mm.h>
#include <linux/kthread.h>
#include <linux/eventpoll.h>
#include "xsc_internal.h"

/* External kernel functions exported for XSC */
extern int do_sys_poll(struct pollfd __user *ufds, unsigned int nfds,
		       struct timespec64 *end_time);
extern int kern_select(int n, fd_set __user *inp, fd_set __user *outp,
		       fd_set __user *exp, struct __kernel_old_timeval __user *tvp);
extern int do_epoll_wait(int epfd, struct epoll_event __user *events,
			 int maxevents, struct timespec64 *to);
extern long hrtimer_nanosleep(ktime_t rqtp, const enum hrtimer_mode mode,
			      const clockid_t clockid);

static int xsc_handle_poll(struct xsc_sqe *sqe)
{
	struct pollfd __user *ufds = (struct pollfd __user *)sqe->addr;
	unsigned int nfds = sqe->len;
	struct timespec64 end_time, *to = NULL;

	/* Check if timeout is specified in addr2 */
	if (sqe->off) {
		struct __kernel_timespec __user *ts =
			(struct __kernel_timespec __user *)sqe->off;
		struct __kernel_timespec kts;

		if (copy_from_user(&kts, ts, sizeof(kts)))
			return -EFAULT;

		end_time.tv_sec = kts.tv_sec;
		end_time.tv_nsec = kts.tv_nsec;
		to = &end_time;
	}

	/* Validate nfds */
	if (nfds > RLIMIT_NOFILE)
		return -EINVAL;

	return do_sys_poll(ufds, nfds, to);
}

static int xsc_handle_epoll_wait(struct xsc_sqe *sqe)
{
	int epfd = sqe->fd;
	struct epoll_event __user *events =
		(struct epoll_event __user *)sqe->addr;
	int maxevents = sqe->len;
	struct timespec64 timeout, *to = NULL;

	/* Check if timeout is specified in addr2/off */
	if (sqe->off) {
		struct __kernel_timespec __user *ts =
			(struct __kernel_timespec __user *)sqe->off;
		struct __kernel_timespec kts;

		if (copy_from_user(&kts, ts, sizeof(kts)))
			return -EFAULT;

		timeout.tv_sec = kts.tv_sec;
		timeout.tv_nsec = kts.tv_nsec;
		to = &timeout;
	}

	/* Validate parameters */
	if (maxevents <= 0)
		return -EINVAL;

	return do_epoll_wait(epfd, events, maxevents, to);
}

static int xsc_handle_select(struct xsc_sqe *sqe)
{
	/* SELECT implementation using kern_select
	 * sqe->len contains n (highest fd + 1)
	 * sqe->addr points to struct { fd_set *in, *out, *ex, *tv }
	 */
	struct {
		fd_set __user *inp;
		fd_set __user *outp;
		fd_set __user *exp;
		struct __kernel_old_timeval __user *tvp;
	} args;

	if (copy_from_user(&args, (void __user *)sqe->addr, sizeof(args)))
		return -EFAULT;

	return kern_select(sqe->len, args.inp, args.outp, args.exp, args.tvp);
}

static int xsc_handle_nanosleep(struct xsc_sqe *sqe)
{
	struct __kernel_timespec __user *rqtp_user =
		(struct __kernel_timespec __user *)sqe->addr;
	struct __kernel_timespec rqtp;
	ktime_t t;

	if (copy_from_user(&rqtp, rqtp_user, sizeof(rqtp)))
		return -EFAULT;

	/* Validate timespec */
	if (rqtp.tv_sec < 0 || rqtp.tv_nsec < 0 || rqtp.tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	t = timespec64_to_ktime((struct timespec64){
		.tv_sec = rqtp.tv_sec,
		.tv_nsec = rqtp.tv_nsec
	});

	return hrtimer_nanosleep(t, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
}

static int xsc_handle_clock_nanosleep(struct xsc_sqe *sqe)
{
	struct {
		clockid_t clockid;
		int flags;
		struct __kernel_timespec __user *rqtp;
		struct __kernel_timespec __user *rmtp;
	} args;
	struct __kernel_timespec rqtp;
	ktime_t t;
	enum hrtimer_mode mode;

	if (copy_from_user(&args, (void __user *)sqe->addr, sizeof(args)))
		return -EFAULT;

	if (copy_from_user(&rqtp, args.rqtp, sizeof(rqtp)))
		return -EFAULT;

	/* Validate timespec */
	if (rqtp.tv_sec < 0 || rqtp.tv_nsec < 0 || rqtp.tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	t = timespec64_to_ktime((struct timespec64){
		.tv_sec = rqtp.tv_sec,
		.tv_nsec = rqtp.tv_nsec
	});

	/* Check if absolute or relative time */
	mode = (args.flags & TIMER_ABSTIME) ? HRTIMER_MODE_ABS : HRTIMER_MODE_REL;

	return hrtimer_nanosleep(t, mode, args.clockid);
}

int xsc_dispatch_timer(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	int ret;

	switch (sqe->opcode) {
	case XSC_OP_POLL:
		ret = xsc_handle_poll(sqe);
		break;

	case XSC_OP_EPOLL_WAIT:
		ret = xsc_handle_epoll_wait(sqe);
		break;

	case XSC_OP_SELECT:
		ret = xsc_handle_select(sqe);
		break;

	case XSC_OP_NANOSLEEP:
		ret = xsc_handle_nanosleep(sqe);
		break;

	case XSC_OP_CLOCK_NANOSLEEP:
		ret = xsc_handle_clock_nanosleep(sqe);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}
