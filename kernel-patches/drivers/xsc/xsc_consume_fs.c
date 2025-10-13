// SPDX-License-Identifier: GPL-2.0
/*
 * XSC filesystem operation handlers
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#include "xsc_uapi.h"

int xsc_dispatch_fs(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	struct file *file;
	loff_t pos;
	ssize_t ret;

	switch (sqe->opcode) {
	case XSC_OP_READ: {
		void __user *buf = (void __user *)sqe->addr;

		file = fget(sqe->fd);
		if (!file)
			return -EBADF;

		ret = kernel_read(file, buf, sqe->len, &file->f_pos);
		fput(file);
		return ret;
	}

	case XSC_OP_WRITE: {
		void __user *buf = (void __user *)sqe->addr;

		file = fget(sqe->fd);
		if (!file)
			return -EBADF;

		ret = kernel_write(file, buf, sqe->len, &file->f_pos);
		fput(file);
		return ret;
	}

	case XSC_OP_PREAD: {
		void __user *buf = (void __user *)sqe->addr;

		file = fget(sqe->fd);
		if (!file)
			return -EBADF;

		pos = sqe->off;
		ret = kernel_read(file, buf, sqe->len, &pos);
		fput(file);
		return ret;
	}

	case XSC_OP_PWRITE: {
		void __user *buf = (void __user *)sqe->addr;

		file = fget(sqe->fd);
		if (!file)
			return -EBADF;

		pos = sqe->off;
		ret = kernel_write(file, buf, sqe->len, &pos);
		fput(file);
		return ret;
	}

	case XSC_OP_OPEN: {
		const char __user *filename = (const char __user *)sqe->addr;
		int flags = sqe->open_flags;
		umode_t mode = sqe->len;

		return do_sys_open(AT_FDCWD, filename, flags, mode);
	}

	case XSC_OP_CLOSE:
		return __close_fd(current->files, sqe->fd);

	case XSC_OP_FSYNC:
		return do_fsync(sqe->fd, sqe->fsync_flags & FSYNC_FLAGS_DATASYNC);

	default:
		return -EINVAL;
	}
}
