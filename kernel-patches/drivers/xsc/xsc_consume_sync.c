// SPDX-License-Identifier: GPL-2.0
/*
 * XSC synchronization operation handlers (futex)
 */

#include <linux/futex.h>
#include <linux/uaccess.h>
#include "xsc_uapi.h"

int xsc_dispatch_sync(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	u32 __user *uaddr = (u32 __user *)sqe->addr;
	unsigned int flags = sqe->fsync_flags;

	switch (sqe->opcode) {
	case XSC_OP_FUTEX_WAIT: {
		u32 val = sqe->len;
		struct __kernel_timespec __user *timeout =
			(struct __kernel_timespec __user *)sqe->addr2;
		return do_futex(uaddr, FUTEX_WAIT, val, timeout, NULL, 0, 0);
	}

	case XSC_OP_FUTEX_WAKE: {
		int nr_wake = sqe->len;
		return do_futex(uaddr, FUTEX_WAKE, nr_wake, NULL, NULL, 0, 0);
	}

	default:
		return -EINVAL;
	}
}
