// SPDX-License-Identifier: GPL-2.0
/*
 * XSC filesystem operation handlers - full implementation
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/kthread.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/uio.h>
#include <linux/string.h>
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
	case XSC_OP_NOP:
		return 0;

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

	case XSC_OP_READV: {
		struct iovec __user *iov = (struct iovec __user *)sqe->addr;
		unsigned long nr_segs = sqe->len;

		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;

		mm = get_task_mm(ctx->task);
		if (mm) {
			struct iov_iter iter;
			kthread_use_mm(mm);
			ret = import_iovec(READ, iov, nr_segs, 0, (struct iovec **)&iov, &iter);
			if (ret >= 0) {
				ret = vfs_iter_read(file, &iter, &file->f_pos, 0);
				kfree(iov);
			}
			kthread_unuse_mm(mm);
			mmput(mm);
		} else {
			ret = -EINVAL;
		}
		fput(file);
		return ret;
	}

	case XSC_OP_WRITEV: {
		struct iovec __user *iov = (struct iovec __user *)sqe->addr;
		unsigned long nr_segs = sqe->len;

		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;

		mm = get_task_mm(ctx->task);
		if (mm) {
			struct iov_iter iter;
			kthread_use_mm(mm);
			ret = import_iovec(WRITE, iov, nr_segs, 0, (struct iovec **)&iov, &iter);
			if (ret >= 0) {
				ret = vfs_iter_write(file, &iter, &file->f_pos, 0);
				kfree(iov);
			}
			kthread_unuse_mm(mm);
			mmput(mm);
		} else {
			ret = -EINVAL;
		}
		fput(file);
		return ret;
	}

	case XSC_OP_OPEN: {
		const char __user *filename = (const char __user *)sqe->addr;
		int flags = sqe->open_flags;
		umode_t mode = sqe->len;
		struct filename *tmp;

		mm = get_task_mm(ctx->task);
		if (!mm)
			return -EINVAL;

		kthread_use_mm(mm);
		tmp = getname(filename);
		if (IS_ERR(tmp)) {
			ret = PTR_ERR(tmp);
		} else {
			ret = do_sys_open(AT_FDCWD, tmp->name, flags, mode);
			putname(tmp);
		}
		kthread_unuse_mm(mm);
		mmput(mm);
		return ret;
	}

	case XSC_OP_CLOSE:
		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;
		filp_close(file, ctx->files);
		fput(file);
		return 0;

	case XSC_OP_FSYNC:
		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;
		ret = vfs_fsync(file, 0);
		fput(file);
		return ret;

	case XSC_OP_STAT:
	case XSC_OP_LSTAT: {
		const char __user *filename = (const char __user *)sqe->addr;
		struct stat __user *statbuf = (struct stat __user *)sqe->addr2;
		struct filename *tmp;
		int flags = (sqe->opcode == XSC_OP_LSTAT) ? AT_SYMLINK_NOFOLLOW : 0;

		mm = get_task_mm(ctx->task);
		if (!mm)
			return -EINVAL;

		kthread_use_mm(mm);
		tmp = getname(filename);
		if (IS_ERR(tmp)) {
			ret = PTR_ERR(tmp);
		} else {
			struct kstat kst;
			ret = vfs_fstatat(AT_FDCWD, tmp->name, &kst, flags);
			if (ret == 0) {
				struct stat st;
				memset(&st, 0, sizeof(st));
				st.st_dev = kst.dev;
				st.st_ino = kst.ino;
				st.st_mode = kst.mode;
				st.st_nlink = kst.nlink;
				st.st_uid = kst.uid.val;
				st.st_gid = kst.gid.val;
				st.st_rdev = kst.rdev;
				st.st_size = kst.size;
				st.st_blksize = kst.blksize;
				st.st_blocks = kst.blocks;
				st.st_atime = kst.atime.tv_sec;
				st.st_mtime = kst.mtime.tv_sec;
				st.st_ctime = kst.ctime.tv_sec;
				if (copy_to_user(statbuf, &st, sizeof(st)))
					ret = -EFAULT;
			}
			putname(tmp);
		}
		kthread_unuse_mm(mm);
		mmput(mm);
		return ret;
	}

	case XSC_OP_FSTAT: {
		struct stat __user *statbuf = (struct stat __user *)sqe->addr;
		struct kstat kst;

		file = xsc_fget(ctx->files, sqe->fd);
		if (!file)
			return -EBADF;

		ret = vfs_getattr_nosec(&file->f_path, &kst, STATX_BASIC_STATS, 0);
		fput(file);

		if (ret == 0) {
			mm = get_task_mm(ctx->task);
			if (mm) {
				struct stat st;
				kthread_use_mm(mm);
				memset(&st, 0, sizeof(st));
				st.st_dev = kst.dev;
				st.st_ino = kst.ino;
				st.st_mode = kst.mode;
				st.st_nlink = kst.nlink;
				st.st_uid = kst.uid.val;
				st.st_gid = kst.gid.val;
				st.st_rdev = kst.rdev;
				st.st_size = kst.size;
				st.st_blksize = kst.blksize;
				st.st_blocks = kst.blocks;
				st.st_atime = kst.atime.tv_sec;
				st.st_mtime = kst.mtime.tv_sec;
				st.st_ctime = kst.ctime.tv_sec;
				if (copy_to_user(statbuf, &st, sizeof(st)))
					ret = -EFAULT;
				kthread_unuse_mm(mm);
				mmput(mm);
			} else {
				ret = -EINVAL;
			}
		}
		return ret;
	}

	default:
		return -EINVAL;
	}
}
