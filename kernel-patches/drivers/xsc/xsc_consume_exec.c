// SPDX-License-Identifier: GPL-2.0
/*
 * XSC process execution operation handlers
 *
 * Implements spawn-like semantics using kernel_clone() and kernel_thread()
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include <linux/sched/mm.h>
#include <linux/kthread.h>
#include <linux/binfmts.h>
#include "xsc_internal.h"

/* External kernel functions */
extern pid_t kernel_clone(struct kernel_clone_args *kargs);
extern int kernel_execve(const char *filename,
			 const char *const *argv,
			 const char *const *envp);

/* Structure for passing exec args to kernel thread */
struct xsc_exec_args {
	const char *filename;
	const char *const *argv;
	const char *const *envp;
	int dirfd;
	int flags;
	int result;
	struct completion done;
};

/* Kernel thread function that performs exec */
static int xsc_exec_thread(void *data)
{
	struct xsc_exec_args *args = data;

	/* Execute the program in this thread's context */
	args->result = kernel_execve(args->filename, args->argv, args->envp);

	/* Signal completion */
	complete(&args->done);

	/* If exec succeeded, this won't be reached (process replaced) */
	/* If exec failed, exit this thread */
	return args->result;
}

static int xsc_handle_fork(struct xsc_sqe *sqe)
{
	struct kernel_clone_args args = {
		.flags		= SIGCHLD,
		.pidfd		= NULL,
		.child_tid	= NULL,
		.parent_tid	= NULL,
		.exit_signal	= SIGCHLD,
		.stack		= 0,
		.tls		= 0,
		.set_tid	= NULL,
		.set_tid_size	= 0,
		.cgroup		= 0,
		.io_thread	= 0,
	};

	return kernel_clone(&args);
}

static int xsc_handle_vfork(struct xsc_sqe *sqe)
{
	struct kernel_clone_args args = {
		.flags		= CLONE_VFORK | CLONE_VM | SIGCHLD,
		.pidfd		= NULL,
		.child_tid	= NULL,
		.parent_tid	= NULL,
		.exit_signal	= SIGCHLD,
		.stack		= 0,
		.tls		= 0,
		.set_tid	= NULL,
		.set_tid_size	= 0,
		.cgroup		= 0,
		.io_thread	= 0,
	};

	return kernel_clone(&args);
}

static int xsc_handle_clone(struct xsc_sqe *sqe)
{
	struct {
		unsigned long flags;
		void __user *child_stack;
		int __user *parent_tid;
		int __user *child_tid;
		unsigned long tls;
	} clone_args;
	struct kernel_clone_args args = {0};

	if (copy_from_user(&clone_args, (void __user *)sqe->addr, sizeof(clone_args)))
		return -EFAULT;

	args.flags = clone_args.flags;
	args.stack = (unsigned long)clone_args.child_stack;
	args.parent_tid = clone_args.parent_tid;
	args.child_tid = clone_args.child_tid;
	args.tls = clone_args.tls;
	args.exit_signal = (clone_args.flags & CSIGNAL);

	return kernel_clone(&args);
}

static int xsc_handle_execve(struct xsc_sqe *sqe)
{
	struct {
		const char __user *filename;
		const char __user *const __user *argv;
		const char __user *const __user *envp;
	} exec_args;
	struct xsc_exec_args *kargs;
	struct task_struct *exec_task;
	int ret;
	char *filename_kern;
	const char **argv_kern = NULL;
	const char **envp_kern = NULL;
	int argc = 0, envc = 0;

	if (copy_from_user(&exec_args, (void __user *)sqe->addr, sizeof(exec_args)))
		return -EFAULT;

	/* Allocate kernel args structure */
	kargs = kzalloc(sizeof(*kargs), GFP_KERNEL);
	if (!kargs)
		return -ENOMEM;

	/* Copy filename to kernel space */
	filename_kern = strndup_user(exec_args.filename, PATH_MAX);
	if (IS_ERR(filename_kern)) {
		ret = PTR_ERR(filename_kern);
		goto out_free_kargs;
	}

	/* Count and copy argv */
	if (exec_args.argv) {
		const char __user *const __user *argv_user = exec_args.argv;
		const char __user *arg;

		while (get_user(arg, argv_user++) == 0 && arg != NULL)
			argc++;

		argv_kern = kmalloc_array(argc + 1, sizeof(char *), GFP_KERNEL);
		if (!argv_kern) {
			ret = -ENOMEM;
			goto out_free_filename;
		}

		argv_user = exec_args.argv;
		for (int i = 0; i < argc; i++) {
			const char __user *arg;
			get_user(arg, argv_user++);
			argv_kern[i] = strndup_user(arg, PATH_MAX);
			if (IS_ERR(argv_kern[i])) {
				ret = PTR_ERR(argv_kern[i]);
				goto out_free_argv;
			}
		}
		argv_kern[argc] = NULL;
	}

	/* Count and copy envp */
	if (exec_args.envp) {
		const char __user *const __user *envp_user = exec_args.envp;
		const char __user *env;

		while (get_user(env, envp_user++) == 0 && env != NULL)
			envc++;

		envp_kern = kmalloc_array(envc + 1, sizeof(char *), GFP_KERNEL);
		if (!envp_kern) {
			ret = -ENOMEM;
			goto out_free_argv;
		}

		envp_user = exec_args.envp;
		for (int i = 0; i < envc; i++) {
			const char __user *env;
			get_user(env, envp_user++);
			envp_kern[i] = strndup_user(env, PATH_MAX);
			if (IS_ERR(envp_kern[i])) {
				ret = PTR_ERR(envp_kern[i]);
				goto out_free_envp;
			}
		}
		envp_kern[envc] = NULL;
	}

	/* Set up args for exec thread */
	kargs->filename = filename_kern;
	kargs->argv = argv_kern;
	kargs->envp = envp_kern;
	kargs->dirfd = -1;
	kargs->flags = 0;
	init_completion(&kargs->done);

	/* Create a kernel thread to perform the exec
	 * This is spawn-like: we create a new process that will exec
	 */
	exec_task = kthread_run(xsc_exec_thread, kargs, "xsc_exec");
	if (IS_ERR(exec_task)) {
		ret = PTR_ERR(exec_task);
		goto out_free_envp;
	}

	/* Wait for exec to complete or fail */
	wait_for_completion(&kargs->done);
	ret = kargs->result;

	/* If exec succeeded, the thread was replaced and we get the PID */
	if (ret >= 0)
		ret = pid_vnr(task_pid(exec_task));

out_free_envp:
	if (envp_kern) {
		for (int i = 0; i < envc; i++)
			if (!IS_ERR_OR_NULL(envp_kern[i]))
				kfree(envp_kern[i]);
		kfree(envp_kern);
	}
out_free_argv:
	if (argv_kern) {
		for (int i = 0; i < argc; i++)
			if (!IS_ERR_OR_NULL(argv_kern[i]))
				kfree(argv_kern[i]);
		kfree(argv_kern);
	}
out_free_filename:
	if (!IS_ERR_OR_NULL(filename_kern))
		kfree(filename_kern);
out_free_kargs:
	kfree(kargs);

	return ret;
}

static int xsc_handle_execveat(struct xsc_sqe *sqe)
{
	/* For now, execveat is similar to execve but with dirfd support
	 * We'll implement the simplified version that ignores dirfd
	 */
	return xsc_handle_execve(sqe);
}

int xsc_dispatch_exec(struct xsc_ctx *ctx, struct xsc_sqe *sqe, struct xsc_cqe *cqe)
{
	int ret;

	switch (sqe->opcode) {
	case XSC_OP_FORK:
		ret = xsc_handle_fork(sqe);
		break;

	case XSC_OP_VFORK:
		ret = xsc_handle_vfork(sqe);
		break;

	case XSC_OP_CLONE:
		ret = xsc_handle_clone(sqe);
		break;

	case XSC_OP_EXECVE:
		ret = xsc_handle_execve(sqe);
		break;

	case XSC_OP_EXECVEAT:
		ret = xsc_handle_execveat(sqe);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}
