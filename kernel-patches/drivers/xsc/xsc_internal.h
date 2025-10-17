/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XSC internal definitions - shared between module components
 */

#ifndef XSC_INTERNAL_H
#define XSC_INTERNAL_H

#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include "xsc_uapi.h"

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

#endif /* XSC_INTERNAL_H */
