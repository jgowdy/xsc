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
#include <linux/sched/signal.h>
#include <linux/fdtable.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/vmalloc.h>

#include "xsc_uapi.h"
#include "xsc_internal.h"
#include "../include/xsc_wait.h"
#include "../include/xsc_mode.h"

#define XSC_DEVICE_NAME	"xsc"
#define XSC_MAX_ENTRIES	4096

/* struct xsc_ring and struct xsc_ctx are defined in xsc_internal.h */

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
	return vmap(pages, npages, VM_ALLOC, PAGE_KERNEL);
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

	// trace_xsc_complete(ctx, user_data, res);

	spin_unlock(&ctx->lock);

	wake_up_interruptible(&ctx->cq_wait);
}

struct xsc_dispatch_closure {
	struct xsc_ctx *ctx;
	struct xsc_sqe *sqe;
	struct xsc_cqe *cqe;
	int ret;
};

static void xsc_dispatch_with_ctx(void *data)
{
	struct xsc_dispatch_closure *closure = data;
	closure->ret = xsc_dispatch_op(closure->ctx, closure->sqe,
					      closure->cqe);
}

static void xsc_sq_worker(struct work_struct *work)
{
	struct xsc_ctx *ctx = container_of(work, struct xsc_ctx, sq_work);
	struct xsc_ring *ring = &ctx->ring;
	struct xsc_sqe *sqe;
	struct xsc_cqe cqe;
	struct xsc_task_cred tc;
	struct xsc_tp_enter tpe;
	struct xsc_tp_exit tpx;
	u32 head, tail, cq_idx;
	int ret;

	/*
	 * v8-D §10: Set SMT affinity to avoid running on same sibling
	 * as USER thread. Reduces microarchitectural side-channels.
	 */
	xsc_worker_set_affinity(ctx, current);

	while (1) {
		head = READ_ONCE(*ring->sq_head);
		tail = READ_ONCE(*ring->sq_tail);

		if (head == tail)
			break;

		sqe = ring->sqes + (head & *ring->sq_mask) * sizeof(struct xsc_sqe);

		/*
		 * v8-D §2.3: Snapshot origin task credentials at SQE dequeue.
		 * This captures PID, UID, GID, cgroup, and rlimits for attribution.
		 */
		xsc_task_cred_snapshot(&tc, ctx->task);

		/*
		 * v8-D §5.3: Seccomp check at consume (before execution).
		 * Semantic syscall number and canonicalized args.
		 */
		ret = xsc_seccomp_check(&tc, sqe->opcode, (u64 *)&sqe->arg1);
		if (ret) {
			/* Seccomp blocked operation */
			cqe.user_data = sqe->user_data;
			cqe.res = ret;
			cqe.flags = 0;
			goto complete;
		}

		/*
		 * v8-D §5.2: Emit sys_enter tracepoint for observability.
		 * Compatible with strace, BPF, perf.
		 */
		tpe.pid = tc.pid;
		tpe.tgid = tc.tgid;
		tpe.cgroup_id = tc.cgroup_id;
		tpe.nr = sqe->opcode;  /* Semantic syscall number */
		tpe.args[0] = sqe->arg1;
		tpe.args[1] = sqe->arg2;
		tpe.args[2] = sqe->arg3;
		tpe.args[3] = sqe->arg4;
		tpe.args[4] = sqe->arg5;
		tpe.args[5] = sqe->arg6;
		tpe.ts_nsec = ktime_get_ns();
		xsc_trace_sys_enter(&tpe);

		/*
		 * v8-D §5.4: Audit log submission.
		 */
		xsc_audit_submit(&tc, sqe->opcode, (u64 *)&sqe->arg1);

		/*
		 * v8-D §8.4: Check for pending signals before dispatch.
		 * Return -EINTR if fatal signal pending.
		 */
		ret = xsc_check_signals(ctx);
		if (ret) {
			cqe.user_data = sqe->user_data;
			cqe.res = ret;
			cqe.flags = 0;
			goto complete;
		}

		/*
		 * Dispatch to handler (fs, net, timer, sync, exec).
		 * Handler runs with origin task attribution via tc.
		 */
		struct xsc_dispatch_closure closure = {
			.ctx = ctx,
			.sqe = sqe,
			.cqe = &cqe,
			.ret = 0,
		};

		xsc_run_with_attribution(ctx, &tc, xsc_dispatch_with_ctx, &closure);
		ret = closure.ret;

		/*
		 * v8-D §5.2: Emit sys_exit tracepoint.
		 */
		tpx.pid = tc.pid;
		tpx.tgid = tc.tgid;
		tpx.ret = ret;
		tpx.ts_nsec = ktime_get_ns();
		xsc_trace_sys_exit(&tpx);

		/*
		 * v8-D §5.4: Audit log result.
		 */
		xsc_audit_result(&tc, ret);

		/* Prepare CQE */
		cqe.user_data = sqe->user_data;
		cqe.res = ret;
		cqe.flags = 0;

complete:
		/*
		 * v8-D §2.5: Write CQE with batched SMAP toggles.
		 * Uses optimized xsc_cqe_write() instead of direct copy.
		 */
		cq_idx = READ_ONCE(*ring->cq_tail);
		xsc_cqe_write(ctx, &cqe, cq_idx);

		/* Update CQ tail */
		smp_wmb();
		WRITE_ONCE(*ring->cq_tail, cq_idx + 1);

		/* Wake waiting threads */
		wake_up_interruptible(&ctx->cq_wait);

		/*
		 * v8-D §2.3: Release credential snapshot.
		 */
		xsc_task_cred_release(&tc);

		/* Update SQ head */
		smp_mb();
		WRITE_ONCE(*ring->sq_head, head + 1);
	}

	/*
	 * v8-D §10: Clear SMT affinity restrictions after processing.
	 */
	xsc_worker_clear_affinity(current);
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
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = xsc_enter_mode(current, ctx);
	if (ret) {
		kfree(ctx);
		return ret;
	}

	spin_lock_init(&ctx->lock);
	init_waitqueue_head(&ctx->cq_wait);
	ctx->file = file;
	ctx->task = current;
	ctx->files = current->files;
	get_task_struct(ctx->task);
	ctx->cpu = -1;

	file->private_data = ctx;

	return 0;
}

static int xsc_release(struct inode *inode, struct file *file)
{
	struct xsc_ctx *ctx = file->private_data;

	if (ctx) {
		xsc_exit_mode(ctx->task, ctx);
		/*
		 * v8-D §8.4: Cancel pending SQEs on ring close.
		 * This handles task exit, exec(), and explicit close().
		 */
		xsc_cancel_pending_sqes(ctx);

		/* Flush workqueue to ensure all workers complete */
		if (ctx->wq) {
			flush_workqueue(ctx->wq);
			destroy_workqueue(ctx->wq);
		}

		xsc_free_rings(ctx);
		if (ctx->task)
			put_task_struct(ctx->task);
		kfree(ctx);
	}

	return 0;
}

static ssize_t xsc_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct xsc_ctx *ctx = file->private_data;

	/* Writing any data triggers submission queue processing */
	if (ctx->wq)
		queue_work(ctx->wq, &ctx->sq_work);

	return count;
}

static const struct file_operations xsc_fops = {
	.owner		= THIS_MODULE,
	.open		= xsc_open,
	.release	= xsc_release,
	.unlocked_ioctl	= xsc_ioctl,
	.mmap		= xsc_mmap,
	.poll		= xsc_poll,
	.write		= xsc_write,
};

static int __init xsc_init(void)
{
	int ret;

	/* Initialize wait mechanisms first */
	ret = xsc_wait_init();
	if (ret) {
		pr_warn("xsc: wait mechanism init failed (non-fatal): %d\n", ret);
		/* Continue - wait mechanisms will use safe fallbacks */
	}

	ret = register_chrdev(0, XSC_DEVICE_NAME, &xsc_fops);
	if (ret < 0) {
		pr_err("xsc: failed to register char device\n");
		xsc_wait_cleanup();
		return ret;
	}
	xsc_major = ret;

	xsc_class = class_create(THIS_MODULE, XSC_DEVICE_NAME);
	if (IS_ERR(xsc_class)) {
		unregister_chrdev(xsc_major, XSC_DEVICE_NAME);
		xsc_wait_cleanup();
		return PTR_ERR(xsc_class);
	}

	xsc_device = device_create(xsc_class, NULL, MKDEV(xsc_major, 0),
				   NULL, XSC_DEVICE_NAME);
	if (IS_ERR(xsc_device)) {
		class_destroy(xsc_class);
		unregister_chrdev(xsc_major, XSC_DEVICE_NAME);
		xsc_wait_cleanup();
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

	/* Cleanup wait mechanisms */
	xsc_wait_cleanup();

	pr_info("xsc: unloaded\n");
}

module_init(xsc_init);
module_exit(xsc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XSC Project");
MODULE_DESCRIPTION("eXtended Syscall (XSC) driver");
MODULE_VERSION("1.0");
