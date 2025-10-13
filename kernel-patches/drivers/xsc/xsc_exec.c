// SPDX-License-Identifier: GPL-2.0
/*
 * XSC process execution handlers (fork, exec)
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/kernel.h>
#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/uaccess.h>
#include "xsc_uapi.h"

/* Validate XSC ELF note in binary */
static int xsc_validate_elf_note(const char *filename)
{
	struct file *file;
	Elf64_Ehdr ehdr;
	Elf64_Phdr *phdr;
	int i, ret = -ENOEXEC;
	loff_t pos = 0;

	file = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	/* Read ELF header */
	if (kernel_read(file, &ehdr, sizeof(ehdr), &pos) != sizeof(ehdr))
		goto out;

	/* Verify it's an ELF file */
	if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0)
		goto out;

	/* Read program headers */
	phdr = kmalloc_array(ehdr.e_phnum, sizeof(Elf64_Phdr), GFP_KERNEL);
	if (!phdr) {
		ret = -ENOMEM;
		goto out;
	}

	pos = ehdr.e_phoff;
	if (kernel_read(file, phdr, ehdr.e_phnum * sizeof(Elf64_Phdr), &pos) !=
	    ehdr.e_phnum * sizeof(Elf64_Phdr)) {
		kfree(phdr);
		goto out;
	}

	/* Look for PT_NOTE with XSC_ABI marker */
	for (i = 0; i < ehdr.e_phnum; i++) {
		if (phdr[i].p_type == PT_NOTE) {
			/* Found a note segment - check for XSC_ABI=1 */
			/* In production, we'd parse the note structure here */
			ret = 0;  /* For now, accept if PT_NOTE exists */
			break;
		}
	}

	kfree(phdr);
out:
	filp_close(file, NULL);
	return ret;
}

int xsc_dispatch_exec(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	switch (sqe->opcode) {
	case XSC_OP_FORK:
		return kernel_clone(&(struct kernel_clone_args){
			.flags = SIGCHLD,
			.exit_signal = SIGCHLD,
		});

	case XSC_OP_VFORK:
		return kernel_clone(&(struct kernel_clone_args){
			.flags = CLONE_VFORK | CLONE_VM | SIGCHLD,
			.exit_signal = SIGCHLD,
		});

	case XSC_OP_CLONE: {
		unsigned long clone_flags = sqe->len;
		void __user *child_stack = (void __user *)sqe->addr;
		return kernel_clone(&(struct kernel_clone_args){
			.flags = clone_flags,
			.stack = (unsigned long)child_stack,
			.exit_signal = (clone_flags & CSIGNAL),
		});
	}

	case XSC_OP_EXECVE: {
		const char __user *filename = (const char __user *)sqe->addr;
		const char __user *const __user *argv =
			(const char __user *const __user *)sqe->addr2;
		const char __user *const __user *envp =
			(const char __user *const __user *)sqe->off;
		char *kfilename;
		int ret;

		kfilename = strndup_user(filename, PATH_MAX);
		if (IS_ERR(kfilename))
			return PTR_ERR(kfilename);

		/* Validate binary has XSC_ABI note */
		ret = xsc_validate_elf_note(kfilename);
		kfree(kfilename);

		if (ret < 0)
			return ret;

		return do_execve(getname(filename), argv, envp);
	}

	case XSC_OP_EXECVEAT: {
		int dirfd = sqe->fd;
		const char __user *filename = (const char __user *)sqe->addr;
		const char __user *const __user *argv =
			(const char __user *const __user *)sqe->addr2;
		const char __user *const __user *envp =
			(const char __user *const __user *)sqe->off;
		int flags = sqe->open_flags;

		return do_execveat(dirfd, getname(filename), argv, envp, flags);
	}

	default:
		return -EINVAL;
	}
}

/* Trap guard function - called when syscall/SVC instruction is executed */
int xsc_trap_guard(struct pt_regs *regs)
{
	/* Send SIGSYS to the process */
	force_sig(SIGSYS);
	return 0;
}
EXPORT_SYMBOL(xsc_trap_guard);
