# XSC Usercopy Helpers

These helpers centralize the "borrow task mm → copy → release" pattern.

## API

```c
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
```

All helpers wrap `get_task_mm()/kthread_use_mm()` internally, so call sites
only pass the submitting task (`ctx->task`).  Buffers are always copied or
pinned via `xsc_uvec` to honour quota/pinning rules before handing them to the
kernel helpers (`kernel_read`, `__sys_sendto`, etc.).

This keeps every dispatcher SMAP-safe without duplicating mm hand-off logic.
