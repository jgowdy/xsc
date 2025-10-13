// SPDX-License-Identifier: GPL-2.0
/*
 * XSC timer and polling operation handlers
 */

#include <linux/poll.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include "xsc_uapi.h"

int xsc_dispatch_timer(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	switch (sqe->opcode) {
	case XSC_OP_POLL: {
		struct pollfd __user *fds = (struct pollfd __user *)sqe->addr;
		struct timespec64 __user *timeout = (struct timespec64 __user *)sqe->addr2;
		return do_sys_poll(fds, sqe->len, timeout);
	}

	case XSC_OP_EPOLL_WAIT: {
		struct epoll_event __user *events = (struct epoll_event __user *)sqe->addr;
		return do_epoll_wait(sqe->fd, events, sqe->len, sqe->off);
	}

	case XSC_OP_SELECT: {
		fd_set __user *readfds = (fd_set __user *)sqe->addr;
		fd_set __user *writefds = (fd_set __user *)sqe->addr2;
		fd_set __user *exceptfds = (fd_set __user *)(sqe->addr2 + sizeof(fd_set));
		struct __kernel_old_timeval __user *timeout =
			(struct __kernel_old_timeval __user *)(sqe->addr2 + 2 * sizeof(fd_set));
		return do_select(sqe->len, readfds, writefds, exceptfds, timeout);
	}

	case XSC_OP_NANOSLEEP: {
		struct __kernel_timespec __user *req = (struct __kernel_timespec __user *)sqe->addr;
		struct __kernel_timespec __user *rem = (struct __kernel_timespec __user *)sqe->addr2;
		return __sys_nanosleep(req, rem);
	}

	case XSC_OP_CLOCK_NANOSLEEP: {
		struct __kernel_timespec __user *req = (struct __kernel_timespec __user *)sqe->addr;
		struct __kernel_timespec __user *rem = (struct __kernel_timespec __user *)sqe->addr2;
		return __sys_clock_nanosleep(sqe->fd, sqe->timeout_flags, req, rem);
	}

	default:
		return -EINVAL;
	}
}
