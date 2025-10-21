/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XSC_USERCOPY_H
#define _XSC_USERCOPY_H

#include <linux/sched.h>
#include <linux/uio.h>

ssize_t xsc_copy_from_user_ctx(struct task_struct *origin, void *dst,
                               const void __user *src, size_t len);
ssize_t xsc_copy_to_user_ctx(struct task_struct *origin, void __user *dst,
                             const void *src, size_t len);
int xsc_iov_to_kvec_ctx(struct task_struct *origin,
                        const struct iovec __user *u_iov, unsigned int iovcnt,
                        struct kvec *kv, unsigned int *out_cnt, size_t *out_len);
void xsc_free_kvec_ctx(struct kvec *kv, unsigned int count);
int xsc_strndup_user_ctx(struct task_struct *origin,
                         const char __user *uname, char **kname, size_t max);

#endif
