// SPDX-License-Identifier: GPL-2.0
/*
 * XSC operation handlers (stub version for testing)
 */

#include <linux/errno.h>
#include "xsc_internal.h"

/* Stub implementations - return ENOSYS for unimplemented operations */

int xsc_dispatch_net(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	(void)ctx;
	(void)sqe;
	(void)cqe;
	return -ENOSYS;
}

int xsc_dispatch_timer(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	(void)ctx;
	(void)sqe;
	(void)cqe;
	return -ENOSYS;
}

int xsc_dispatch_sync(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	(void)ctx;
	(void)sqe;
	(void)cqe;
	return -ENOSYS;
}

int xsc_dispatch_exec(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	(void)ctx;
	(void)sqe;
	(void)cqe;
	return -ENOSYS;
}
