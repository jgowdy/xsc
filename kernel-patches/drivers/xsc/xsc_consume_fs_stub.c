// SPDX-License-Identifier: GPL-2.0
/*
 * XSC filesystem operation handlers (stub version for testing)
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/kthread.h>
#include "xsc_internal.h"

static struct file *xsc_fget(struct files_struct *files, unsigned int fd)
{
	struct file *file;

	rcu_read_lock();
	file = files_lookup_fd_rcu(files, fd);
	if (file && !get_file_rcu(file))
		file = NULL;
	rcu_read_unlock();

	return file;
}

int xsc_dispatch_fs(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	struct file *file;
	struct mm_struct *mm;
	ssize_t ret;

	switch (sqe->opcode) {
	case XSC_OP_READ: {
		void __user *buf = (void __user *)sqe->addr;

		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;

		mm = get_task_mm(ctx->task);
		if (mm) {
			kthread_use_mm(mm);
			ret = kernel_read(file, buf, sqe->len, &file->f_pos);
			kthread_unuse_mm(mm);
			mmput(mm);
		} else {
			ret = -EINVAL;
		}
		fput(file);
		return ret;
	}

	case XSC_OP_WRITE: {
		void __user *buf = (void __user *)sqe->addr;

		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;

		mm = get_task_mm(ctx->task);
		if (mm) {
			kthread_use_mm(mm);
			ret = kernel_write(file, buf, sqe->len, &file->f_pos);
			kthread_unuse_mm(mm);
			mmput(mm);
		} else {
			ret = -EINVAL;
		}
		fput(file);
		return ret;
	}

	case XSC_OP_PREAD: {
		loff_t pos = sqe->off;
		void __user *buf = (void __user *)sqe->addr;

		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;

		mm = get_task_mm(ctx->task);
		if (mm) {
			kthread_use_mm(mm);
			ret = kernel_read(file, buf, sqe->len, &pos);
			kthread_unuse_mm(mm);
			mmput(mm);
		} else {
			ret = -EINVAL;
		}
		fput(file);
		return ret;
	}

	case XSC_OP_PWRITE: {
		loff_t pos = sqe->off;
		void __user *buf = (void __user *)sqe->addr;

		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;

		mm = get_task_mm(ctx->task);
		if (mm) {
			kthread_use_mm(mm);
			ret = kernel_write(file, buf, sqe->len, &pos);
			kthread_unuse_mm(mm);
			mmput(mm);
		} else {
			ret = -EINVAL;
		}
		fput(file);
		return ret;
	}

	case XSC_OP_CLOSE:
		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;
		fput(file);
		fput(file); /* Close the file */
		return 0;

	/* Not yet implemented - return ENOSYS */
	case XSC_OP_OPEN:
	case XSC_OP_FSYNC:
	case XSC_OP_READV:
	case XSC_OP_WRITEV:
	case XSC_OP_STAT:
	case XSC_OP_FSTAT:
	case XSC_OP_LSTAT:
		return -ENOSYS;

	default:
		return -EINVAL;
	}
}
