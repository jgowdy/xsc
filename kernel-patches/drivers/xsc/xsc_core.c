// SPDX-License-Identifier: GPL-2.0
/*
 * XSC (eXtended Syscall) - Core driver implementation
 * Copyright (C) 2025
 *
 * This driver provides a ring-based syscall interface via /dev/xsc
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>

#define CREATE_TRACE_POINTS
#include "xsc_trace.h"
#include "xsc_uapi.h"

#define XSC_DEVICE_NAME	"xsc"
#define XSC_MAX_ENTRIES	4096

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
	bool			polling;
	int			cpu;
};

/* Forward declarations */
static void xsc_sq_worker(struct work_struct *work);

/* External dispatch functions (implemented in consume_*.c) */
extern int xsc_dispatch_fs(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);
extern int xsc_dispatch_net(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);
extern int xsc_dispatch_timer(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);
extern int xsc_dispatch_sync(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);
extern int xsc_dispatch_exec(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe);

static int xsc_major;
static struct class *xsc_class;
static struct device *xsc_device;

static int xsc_alloc_ring_pages(struct page ***pages, int *npages, size_t size)
{
	int nr_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	struct page **pg;
	int i;

	pg = kvmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pg)
		return -ENOMEM;

	for (i = 0; i < nr_pages; i++) {
		pg[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!pg[i]) {
			while (i--)
				__free_page(pg[i]);
			kvfree(pg);
			return -ENOMEM;
		}
	}

	*pages = pg;
	*npages = nr_pages;
	return 0;
}

static void xsc_free_ring_pages(struct page **pages, int npages)
{
	int i;

	if (!pages)
		return;

	for (i = 0; i < npages; i++)
		__free_page(pages[i]);
	kvfree(pages);
}

static void *xsc_ring_map_pages(struct page **pages, int npages)
{
	return vmap(pages, npages, VM_MAP, PAGE_KERNEL);
}

static int xsc_setup_rings(struct xsc_ctx *ctx, struct xsc_params *p)
{
	struct xsc_ring *ring = &ctx->ring;
	size_t sq_ring_size, cq_ring_size;
	size_t sqe_size, cqe_size;
	int ret;

	if (p->sq_entries > XSC_MAX_ENTRIES || p->cq_entries > XSC_MAX_ENTRIES)
		return -EINVAL;

	if (!p->sq_entries)
		p->sq_entries = 128;
	if (!p->cq_entries)
		p->cq_entries = 256;

	/* Round up to power of 2 */
	p->sq_entries = roundup_pow_of_two(p->sq_entries);
	p->cq_entries = roundup_pow_of_two(p->cq_entries);

	ring->sq_entries = p->sq_entries;
	ring->cq_entries = p->cq_entries;

	/* Allocate SQ ring metadata */
	sq_ring_size = sizeof(struct xsc_sqe_ring);
	ret = xsc_alloc_ring_pages(&ring->sq_pages, &ring->sq_npages, sq_ring_size);
	if (ret)
		return ret;

	ring->sq_ring = xsc_ring_map_pages(ring->sq_pages, ring->sq_npages);
	if (!ring->sq_ring) {
		ret = -ENOMEM;
		goto err_sq;
	}

	/* Allocate CQ ring metadata */
	cq_ring_size = sizeof(struct xsc_cqe_ring);
	ret = xsc_alloc_ring_pages(&ring->cq_pages, &ring->cq_npages, cq_ring_size);
	if (ret)
		goto err_sq_map;

	ring->cq_ring = xsc_ring_map_pages(ring->cq_pages, ring->cq_npages);
	if (!ring->cq_ring) {
		ret = -ENOMEM;
		goto err_cq;
	}

	/* Allocate SQE array */
	sqe_size = p->sq_entries * sizeof(struct xsc_sqe);
	ret = xsc_alloc_ring_pages(&ring->sqe_pages, &ring->sqe_npages, sqe_size);
	if (ret)
		goto err_cq_map;

	ring->sqes = xsc_ring_map_pages(ring->sqe_pages, ring->sqe_npages);
	if (!ring->sqes) {
		ret = -ENOMEM;
		goto err_sqe;
	}

	/* Allocate CQE array */
	cqe_size = p->cq_entries * sizeof(struct xsc_cqe);
	ret = xsc_alloc_ring_pages(&ring->cqe_pages, &ring->cqe_npages, cqe_size);
	if (ret)
		goto err_sqe_map;

	ring->cqes = xsc_ring_map_pages(ring->cqe_pages, ring->cqe_npages);
	if (!ring->cqes) {
		ret = -ENOMEM;
		goto err_cqe;
	}

	/* Initialize ring pointers */
	ring->sq_head = ring->sq_ring;
	ring->sq_tail = ring->sq_ring + sizeof(u32);
	ring->sq_mask = ring->sq_ring + sizeof(u32) * 2;
	ring->sq_flags = ring->sq_ring + sizeof(u32) * 4;
	*ring->sq_mask = p->sq_entries - 1;

	ring->cq_head = ring->cq_ring;
	ring->cq_tail = ring->cq_ring + sizeof(u32);
	ring->cq_mask = ring->cq_ring + sizeof(u32) * 2;
	ring->cq_overflow = ring->cq_ring + sizeof(u32) * 4;
	*ring->cq_mask = p->cq_entries - 1;

	/* Setup worker */
	ctx->wq = alloc_workqueue("xsc_wq", WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!ctx->wq) {
		ret = -ENOMEM;
		goto err_cqe_map;
	}

	INIT_WORK(&ctx->sq_work, xsc_sq_worker);

	return 0;

err_cqe_map:
	vunmap(ring->cqes);
err_cqe:
	xsc_free_ring_pages(ring->cqe_pages, ring->cqe_npages);
err_sqe_map:
	vunmap(ring->sqes);
err_sqe:
	xsc_free_ring_pages(ring->sqe_pages, ring->sqe_npages);
err_cq_map:
	vunmap(ring->cq_ring);
err_cq:
	xsc_free_ring_pages(ring->cq_pages, ring->cq_npages);
err_sq_map:
	vunmap(ring->sq_ring);
err_sq:
	xsc_free_ring_pages(ring->sq_pages, ring->sq_npages);
	return ret;
}

static void xsc_free_rings(struct xsc_ctx *ctx)
{
	struct xsc_ring *ring = &ctx->ring;

	if (ctx->wq) {
		destroy_workqueue(ctx->wq);
		ctx->wq = NULL;
	}

	if (ring->cqes)
		vunmap(ring->cqes);
	xsc_free_ring_pages(ring->cqe_pages, ring->cqe_npages);

	if (ring->sqes)
		vunmap(ring->sqes);
	xsc_free_ring_pages(ring->sqe_pages, ring->sqe_npages);

	if (ring->cq_ring)
		vunmap(ring->cq_ring);
	xsc_free_ring_pages(ring->cq_pages, ring->cq_npages);

	if (ring->sq_ring)
		vunmap(ring->sq_ring);
	xsc_free_ring_pages(ring->sq_pages, ring->sq_npages);
}

static int xsc_dispatch_op(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	switch (sqe->opcode) {
	case XSC_OP_READ:
	case XSC_OP_WRITE:
	case XSC_OP_OPEN:
	case XSC_OP_CLOSE:
	case XSC_OP_FSYNC:
	case XSC_OP_READV:
	case XSC_OP_WRITEV:
	case XSC_OP_PREAD:
	case XSC_OP_PWRITE:
	case XSC_OP_STAT:
	case XSC_OP_FSTAT:
	case XSC_OP_LSTAT:
		return xsc_dispatch_fs(ctx, sqe, cqe);

	case XSC_OP_SENDTO:
	case XSC_OP_RECVFROM:
	case XSC_OP_ACCEPT:
	case XSC_OP_CONNECT:
	case XSC_OP_SOCKET:
	case XSC_OP_BIND:
	case XSC_OP_LISTEN:
		return xsc_dispatch_net(ctx, sqe, cqe);

	case XSC_OP_POLL:
	case XSC_OP_EPOLL_WAIT:
	case XSC_OP_SELECT:
	case XSC_OP_NANOSLEEP:
	case XSC_OP_CLOCK_NANOSLEEP:
		return xsc_dispatch_timer(ctx, sqe, cqe);

	case XSC_OP_FUTEX_WAIT:
	case XSC_OP_FUTEX_WAKE:
		return xsc_dispatch_sync(ctx, sqe, cqe);

	case XSC_OP_FORK:
	case XSC_OP_VFORK:
	case XSC_OP_CLONE:
	case XSC_OP_EXECVE:
	case XSC_OP_EXECVEAT:
		return xsc_dispatch_exec(ctx, sqe, cqe);

	default:
		return -EINVAL;
	}
}

static void xsc_complete_cqe(struct xsc_ctx *ctx, u64 user_data, s32 res)
{
	struct xsc_ring *ring = &ctx->ring;
	struct xsc_cqe *cqe;
	u32 tail;

	spin_lock(&ctx->lock);

	tail = *ring->cq_tail;
	cqe = ring->cqes + (tail & *ring->cq_mask) * sizeof(struct xsc_cqe);

	cqe->user_data = user_data;
	cqe->res = res;
	cqe->flags = 0;

	/* Memory barrier before updating tail */
	smp_wmb();
	WRITE_ONCE(*ring->cq_tail, tail + 1);

	trace_xsc_complete(ctx, user_data, res);

	spin_unlock(&ctx->lock);

	wake_up_interruptible(&ctx->cq_wait);
}

static void xsc_sq_worker(struct work_struct *work)
{
	struct xsc_ctx *ctx = container_of(work, struct xsc_ctx, sq_work);
	struct xsc_ring *ring = &ctx->ring;
	struct xsc_sqe *sqe;
	struct xsc_cqe cqe;
	u32 head, tail;
	int ret;

	while (1) {
		head = READ_ONCE(*ring->sq_head);
		tail = READ_ONCE(*ring->sq_tail);

		if (head == tail)
			break;

		sqe = ring->sqes + (head & *ring->sq_mask) * sizeof(struct xsc_sqe);

		trace_xsc_dispatch(ctx, sqe->opcode, smp_processor_id());

		ret = xsc_dispatch_op(ctx, sqe, &cqe);

		xsc_complete_cqe(ctx, sqe->user_data, ret);

		/* Update head */
		smp_mb();
		WRITE_ONCE(*ring->sq_head, head + 1);
	}
}

static long xsc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct xsc_ctx *ctx = file->private_data;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case XSC_IOC_SETUP: {
		struct xsc_params params;

		if (copy_from_user(&params, argp, sizeof(params)))
			return -EFAULT;

		return xsc_setup_rings(ctx, &params);
	}
	default:
		return -EINVAL;
	}
}

static int xsc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct xsc_ctx *ctx = file->private_data;
	struct xsc_ring *ring = &ctx->ring;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long size = vma->vm_end - vma->vm_start;
	struct page **pages;
	int npages;

	/* Determine which region to map */
	if (off == 0) {
		/* SQ ring */
		pages = ring->sq_pages;
		npages = ring->sq_npages;
	} else if (off == 0x10000000) {
		/* CQ ring */
		pages = ring->cq_pages;
		npages = ring->cq_npages;
	} else if (off == 0x20000000) {
		/* SQE array */
		pages = ring->sqe_pages;
		npages = ring->sqe_npages;
	} else if (off == 0x30000000) {
		/* CQE array */
		pages = ring->cqe_pages;
		npages = ring->cqe_npages;
	} else {
		return -EINVAL;
	}

	return remap_pfn_range(vma, vma->vm_start,
			       page_to_pfn(pages[0]),
			       size, vma->vm_page_prot);
}

static __poll_t xsc_poll(struct file *file, poll_table *wait)
{
	struct xsc_ctx *ctx = file->private_data;
	struct xsc_ring *ring = &ctx->ring;
	__poll_t mask = 0;

	poll_wait(file, &ctx->cq_wait, wait);

	if (READ_ONCE(*ring->cq_head) != READ_ONCE(*ring->cq_tail))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static int xsc_open(struct inode *inode, struct file *file)
{
	struct xsc_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	spin_lock_init(&ctx->lock);
	init_waitqueue_head(&ctx->cq_wait);
	ctx->file = file;
	ctx->cpu = -1;

	file->private_data = ctx;

	return 0;
}

static int xsc_release(struct inode *inode, struct file *file)
{
	struct xsc_ctx *ctx = file->private_data;

	if (ctx) {
		xsc_free_rings(ctx);
		kfree(ctx);
	}

	return 0;
}

static const struct file_operations xsc_fops = {
	.owner		= THIS_MODULE,
	.open		= xsc_open,
	.release	= xsc_release,
	.unlocked_ioctl	= xsc_ioctl,
	.mmap		= xsc_mmap,
	.poll		= xsc_poll,
};

static int __init xsc_init(void)
{
	int ret;

	ret = register_chrdev(0, XSC_DEVICE_NAME, &xsc_fops);
	if (ret < 0) {
		pr_err("xsc: failed to register char device\n");
		return ret;
	}
	xsc_major = ret;

	xsc_class = class_create(THIS_MODULE, XSC_DEVICE_NAME);
	if (IS_ERR(xsc_class)) {
		unregister_chrdev(xsc_major, XSC_DEVICE_NAME);
		return PTR_ERR(xsc_class);
	}

	xsc_device = device_create(xsc_class, NULL, MKDEV(xsc_major, 0),
				   NULL, XSC_DEVICE_NAME);
	if (IS_ERR(xsc_device)) {
		class_destroy(xsc_class);
		unregister_chrdev(xsc_major, XSC_DEVICE_NAME);
		return PTR_ERR(xsc_device);
	}

	pr_info("xsc: initialized successfully\n");
	return 0;
}

static void __exit xsc_exit(void)
{
	device_destroy(xsc_class, MKDEV(xsc_major, 0));
	class_destroy(xsc_class);
	unregister_chrdev(xsc_major, XSC_DEVICE_NAME);
	pr_info("xsc: unloaded\n");
}

module_init(xsc_init);
module_exit(xsc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XSC Project");
MODULE_DESCRIPTION("eXtended Syscall (XSC) driver");
MODULE_VERSION("1.0");
