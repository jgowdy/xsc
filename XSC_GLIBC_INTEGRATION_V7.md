# XSC glibc Integration for v7

**Date**: 2025-10-17
**Status**: Preparation complete, awaiting server recovery for deployment

---

## Current State

We have existing XSC syscall shim implementation in:
- `/Users/jgowdy/flexsc/xsc-glibc-syscalls.c`
- `/Users/jgowdy/flexsc/integrate-xsc-glibc.sh`
- `/Users/jgowdy/flexsc/generate-glibc-sysdeps.sh`

### Existing Implementation Review

**Strengths**:
- Ring structure definitions (xsc_sqe, xsc_cqe) match kernel
- Basic initialization flow (__xsc_init)
- Syscall wrapper pattern is correct
- Sysdeps generation scripts for glibc integration

**Gaps vs v7 Specification**:

1. **Auxv-based Ring Discovery** (v7 requirement)
   - Current: Opens `/dev/xsc` and uses ioctl to setup rings
   - v7 spec: "Rings mapped by kernel at exec time, published in auxv (AT_XSC_*)"
   - **Impact**: Breaks v7's design where kernel sets up rings before userspace runs

2. **Ring Mapping Offsets**
   - Current: Uses simple sequential mmap offsets
   - Kernel (`xsc_core.c:397-430`): Uses specific offset constants:
     - 0x00000000: SQ ring metadata
     - 0x10000000: CQ ring metadata
     - 0x20000000: SQE array
     - 0x30000000: CQE array
   - **Status**: Current code needs to match kernel offsets exactly

3. **Completion Waiting**
   - Current: Spin-wait loop with comment "In real impl, would sleep or futex here"
   - Kernel: Uses `wake_up_interruptible(&ctx->cq_wait)` in xsc_core.c:345
   - v7 spec: Should integrate with adaptive polling or use file descriptor wait
   - **Impact**: High CPU usage, poor power efficiency

4. **Ring Pointers**
   - Current: Local sq_head, sq_tail, cq_head variables
   - Kernel: Shared memory pointers in ring metadata pages
   - **Missing**: Proper shared memory synchronization with kernel

5. **vDSO Integration**
   - v7 spec: "vDSO handles clock_gettime, gettimeofday, getcpu"
   - Current: Partial implementation in generate-glibc-sysdeps.sh
   - **Status**: Need to verify vDSO is actually provided by kernel

---

## v7-Compliant Implementation Plan

### Phase 1: Auxv-based Ring Initialization

Instead of opening /dev/xsc, read ring addresses from auxv at program startup.

**New auxv types** (need to be added to kernel's binfmt_elf.c):
```c
#define AT_XSC_SR_ADDR   48  /* Submission ring address */
#define AT_XSC_CR_ADDR   49  /* Completion ring address */
#define AT_XSC_SQE_ADDR  50  /* SQE array address */
#define AT_XSC_CQE_ADDR  51  /* CQE array address */
#define AT_XSC_CTRL_ADDR 52  /* Control page address */
```

**Updated glibc startup**:
```c
void __xsc_init_from_auxv(void) {
    unsigned long *auxv = (unsigned long *)environ + 1;
    while (*auxv++)
        ;  /* skip to auxv */

    while (*auxv != AT_NULL) {
        unsigned long type = *auxv++;
        unsigned long value = *auxv++;

        switch (type) {
        case AT_XSC_SR_ADDR:
            sq_ring = (struct xsc_sqe_ring *)value;
            break;
        case AT_XSC_CR_ADDR:
            cq_ring = (struct xsc_cqe_ring *)value;
            break;
        case AT_XSC_SQE_ADDR:
            sqes = (struct xsc_sqe *)value;
            break;
        case AT_XSC_CQE_ADDR:
            cqes = (struct xsc_cqe *)value;
            break;
        case AT_XSC_CTRL_ADDR:
            ctrl = (struct xsc_ctrl *)value;
            break;
        }
    }
}
```

### Phase 2: Proper Ring Pointer Access

Update to use shared memory pointers from ring metadata:

```c
struct xsc_sqe_ring {
    uint32_t head;
    uint32_t tail;
    uint32_t mask;
    uint32_t flags;
};

struct xsc_cqe_ring {
    uint32_t head;
    uint32_t tail;
    uint32_t mask;
    uint32_t overflow;
};

/* Access via shared memory */
static inline void submit_sqe(struct xsc_sqe *sqe) {
    uint32_t tail = sq_ring->tail;
    sqes[tail & sq_ring->mask] = *sqe;

    /* Memory barrier */
    __atomic_thread_fence(__ATOMIC_RELEASE);

    /* Update tail */
    sq_ring->tail = tail + 1;
}
```

### Phase 3: Completion Waiting with File Descriptor

Instead of spin-waiting, use the /dev/xsc file descriptor with poll():

```c
static long wait_for_completion(uint64_t user_data) {
    while (1) {
        /* Check if CQE is ready */
        uint32_t head = __atomic_load_n(&cq_ring->head, __ATOMIC_ACQUIRE);
        uint32_t tail = __atomic_load_n(&cq_ring->tail, __ATOMIC_ACQUIRE);

        while (head != tail) {
            struct xsc_cqe *cqe = &cqes[head & cq_ring->mask];
            if (cqe->user_data == user_data) {
                long result = cqe->res;
                cq_ring->head = head + 1;
                return result;
            }
            head++;
        }

        /* No completion yet, wait on fd */
        struct pollfd pfd = {
            .fd = xsc_fd,
            .events = POLLIN,
        };
        poll(&pfd, 1, -1);
    }
}
```

### Phase 4: Worker Notification

After submitting to SQ, notify kernel workers:

```c
static inline void notify_kernel(void) {
    /* Option 1: Write to /dev/xsc (triggers xsc_write in xsc_core.c:482) */
    char dummy = 1;
    write(xsc_fd, &dummy, 1);

    /* Option 2: Use doorbell if available (future) */
    /* Option 3: Rely on adaptive polling (no-op here) */
}
```

---

## Integration with Existing glibc Build

The existing toolchain already has glibc-2.38 built. We need to:

1. **Add XSC sysdeps directory**:
   ```
   /storage/icloud-backup/build/src/glibc-2.38/sysdeps/unix/sysv/linux/x86_64-xsc-linux-gnu/
   ```

2. **Copy improved xsc-glibc-syscalls.c** with v7 changes

3. **Create proper Implies file**:
   ```
   unix/sysv/linux/x86_64
   unix/sysv/linux
   x86_64
   ```

4. **Add to Makefile**:
   ```makefile
   ifeq ($(subdir),misc)
   sysdep_routines += xsc-init xsc-submit
   endif

   ifeq ($(subdir),posix)
   sysdep_routines += xsc-fork xsc-exec
   endif
   ```

5. **Rebuild glibc** in toolchain:
   ```bash
   cd /storage/icloud-backup/build/xsc-toolchain-x86_64-base/build/glibc
   make -j80
   make install
   ```

---

## Minimal Implementation for Basic ISO

For a **minimal bootable ISO**, we can use a simplified approach:

1. **Keep /dev/xsc opening** (defer auxv to later)
2. **Implement basic notification** (write to fd)
3. **Use poll() for waiting** (avoid spin-wait)
4. **Focus on essential syscalls**: read, write, open, close, fork, execve

This gets us a working system quickly, then we can refine to full v7 compliance.

---

## Essential Syscalls for Minimal ISO

To boot Debian and reach a shell prompt, we need:

| Category | Syscalls | Priority |
|----------|----------|----------|
| **File I/O** | open, close, read, write, lseek, stat, fstat | Critical |
| **Process** | fork, execve, exit, wait4 | Critical |
| **Memory** | mmap, munmap, brk | Critical |
| **Signals** | sigaction, sigprocmask, rt_sigreturn | High |
| **Filesystem** | chdir, getcwd, mkdir, rmdir, unlink | High |
| **Network** | socket, bind, listen, accept, connect, send, recv | Medium |
| **Time** | clock_gettime (vDSO), nanosleep | Medium |
| **Sync** | futex (for threading) | Medium |

---

## Next Steps (When Server Recovers)

1. ✅ Copy improved xsc-glibc-syscalls.c to server
2. ✅ Run integrate-xsc-glibc.sh with v7 updates
3. ✅ Rebuild toolchain glibc with XSC integration
4. ✅ Test simple program (hello world) with XSC
5. ✅ Build minimal package set using XSC-enabled toolchain
6. ✅ Create bootable ISO
7. ✅ Test in QEMU

---

## Kernel Changes Needed

For full v7 auxv support, we need to modify kernel:

**File**: `kernel-patches/fs/binfmt_elf.c` or create `kernel-patches/drivers/xsc/xsc_exec.c`

```c
/* Add auxv entries when loading XSC-enabled binary */
NEW_AUX_ENT(AT_XSC_SR_ADDR, (elf_addr_t)ctx->ring.sq_ring);
NEW_AUX_ENT(AT_XSC_CR_ADDR, (elf_addr_t)ctx->ring.cq_ring);
NEW_AUX_ENT(AT_XSC_SQE_ADDR, (elf_addr_t)ctx->ring.sqes);
NEW_AUX_ENT(AT_XSC_CQE_ADDR, (elf_addr_t)ctx->ring.cqes);
```

This can be deferred to post-minimal-ISO phase.

---

**Status**: Ready for deployment when server recovers
**Blocker**: bx.ee currently offline
**Next action**: Deploy improved glibc integration when server is back online
