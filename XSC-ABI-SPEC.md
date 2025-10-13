# XSC (eXtended SysCall) ABI Specification v1.0

**"All I want to do is... an take your syscall"**

## Overview

XSC changes **one thing**: how userspace invokes syscalls. Instead of CPU instructions (`syscall`, `sysenter`, `int 0x80`), programs use shared memory rings via `/dev/xsc`.

**Everything else is the same**: ELF format, instruction set, calling conventions, memory layout, file formats. XSC is not a new architecture - it's a syscall interface variant of x86-64/ARM64.

## 1. Core ABI Change

### 1.1 Syscall Interface
**Standard Linux**: CPU trap instruction → kernel
```asm
mov $1, %rax        ; SYS_write
mov $1, %rdi        ; fd=1 (stdout)
syscall             ; TRAP TO KERNEL
```

**XSC Linux**: Ring submission → kernel polling
```c
sqe->opcode = XSC_OP_WRITE;
sqe->fd = 1;
sqe->addr = (uint64_t)buf;
sqe->len = count;
// Kernel polls ring, processes, writes CQE
```

### 1.2 What This Means
- **No syscall instructions in binaries** - validation check
- **Glibc wraps syscalls differently** - calls `__xsc_submit_*()`
- **Kernel provides `/dev/xsc` device** - ring buffer interface
- **That's it** - rest is packaging/distribution infrastructure

## 2. Distribution Infrastructure

The following are **NOT ABI changes** - they're packaging conventions to keep XSC and standard binaries separate in repos:

### 2.1 GNU Triplet
- **x86-64**: `x86_64-xsc-linux-gnu`
- **x86-64 x32**: `x86_64-xsc-linux-gnux32`
- **ARM64**: `aarch64-xsc-linux-gnu`

### 2.2 Package Names

**Base variants** (required, maximum compatibility):
- **Debian**:
  - `xsc-amd64` - 64-bit XSC (x86-64)
  - `xsc-arm64` - 64-bit XSC (ARM64)
  - `xsc-x32` - x32 XSC (32-bit pointers, 64-bit ISA) [future]
- **RPM**:
  - `x86_64-xsc` - 64-bit XSC (x86-64)
  - `aarch64-xsc` - 64-bit XSC (ARM64)
  - `x86_64-xsc-x32` - x32 XSC [future]

**Hardened variants** (optional, hardware CFI):
- **Debian**:
  - `xsc-amd64-hardened` - Base + CET (x86-64)
  - `xsc-arm64-hardened` - Base + PAC (ARM64)
- **RPM**:
  - `x86_64-xsc-hardened` - Base + CET (x86-64)
  - `aarch64-xsc-hardened` - Base + PAC (ARM64)

These prevent `apt`/`dnf` from mixing incompatible binaries.

### 2.3 Hardened Variants

**Hardened builds** add hardware-enforced control-flow integrity on top of the base XSC ABI.

#### x86-64 Hardened (CET)
**Intel Control-flow Enforcement Technology**:
- **Shadow Stack (SHSTK)**: Hardware-enforced return address protection
- **Indirect Branch Tracking (IBT)**: Validates indirect call/jump targets
- **Compiler flag**: `-fcf-protection=full`
- **CPU requirement**: Intel 11th gen+ (Tiger Lake), AMD Zen 3+

#### ARM64 Hardened (PAC)
**ARM Pointer Authentication**:
- **PAC-RET**: Return address signing with cryptographic keys
- **Branch protection**: Validates function return addresses
- **Compiler flag**: `-mbranch-protection=pac-ret`
- **CPU requirement**: ARMv8.3-A+ (Apple M1+, AWS Graviton3+)

#### Compatibility
- **API compatible**: Same source code compiles for both base and hardened
- **Binary incompatible**: Cannot mix base and hardened binaries
- **Hardware required**: Hardened binaries fail on CPUs without support
- **Use case**: Deploy hardened on supported hardware, base elsewhere

#### Build Flags

**Base XSC**:
```bash
CFLAGS="-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
        -fstack-clash-protection -Wl,-z,relro,-z,now"
```

**Hardened XSC (x86-64)**:
```bash
CFLAGS="-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
        -fstack-clash-protection -Wl,-z,relro,-z,now \
        -fcf-protection=full"
```

**Hardened XSC (ARM64)**:
```bash
CFLAGS="-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3 \
        -fstack-clash-protection -Wl,-z,relro,-z,now \
        -mbranch-protection=pac-ret"
```

### 2.4 x32 ABI Variant
**x32** combines two ABI changes:
1. **XSC rings** - Syscalls via `/dev/xsc` (no syscall instructions)
2. **32-bit pointers** - ILP32 data model on x86-64 ISA

**Benefits**:
- 30-40% smaller memory footprint vs 64-bit
- Better cache utilization
- Full x86-64 instruction set (unlike i386)
- Same ring-based syscall mechanism

**Calling convention**: x32 uses its own variant (32-bit longs/pointers, different register usage)

**Not implemented yet** - defined for future work.

## 3. Ring Interface (The Actual ABI)

### 3.1 Device Interface
- **Device Node**: `/dev/xsc`
- **Major Number**: 248
- **Minor Number**: 0
- **Permissions**: 0600 (user-only by default)

### 3.2 Ring Structure

#### Submission Queue Entry (SQE)
```c
struct xsc_sqe {
    uint8_t  opcode;      // XSC_OP_* constant
    uint8_t  flags;       // Operation flags
    uint16_t ioprio;      // I/O priority
    int32_t  fd;          // File descriptor
    uint64_t addr;        // Buffer address (userspace)
    uint32_t len;         // Buffer/operation length
    uint64_t user_data;   // User context (returned in CQE)
    uint64_t offset;      // File offset
    uint32_t open_flags;  // open() flags
    uint32_t fsync_flags; // fsync/fdatasync flags
    uint32_t futex_val;   // futex value
    uint32_t timeout_flags;
    uint64_t clone_flags; // fork/clone flags
    uint64_t addr2;       // Secondary buffer address
    uint32_t reserved[4]; // Future expansion
} __attribute__((packed));
```

#### Completion Queue Entry (CQE)
```c
struct xsc_cqe {
    uint64_t user_data;   // Matches SQE user_data
    int32_t  res;         // Return value (or -errno)
    uint32_t flags;       // Status flags
} __attribute__((packed));
```

### 3.3 Ring Sizes
- **Default**: 128 entries (SQ and CQ)
- **Minimum**: 16 entries
- **Maximum**: 4096 entries
- **Must be power of 2**

### 3.4 Setup Sequence
1. `open("/dev/xsc", O_RDWR)` → Get XSC device fd
2. `ioctl(xsc_fd, XSC_SETUP_RINGS, &setup)` → Allocate rings
3. `mmap()` submission queue
4. `mmap()` completion queue
5. Submit syscalls via SQ, poll/wait CQ for results

## 4. Opcodes (Syscall Mapping)

### 4.1 Complete Syscall Coverage

**XSC provides the full Linux syscall API.** XSC opcode numbers map 1:1 with x86-64 Linux syscall numbers for API compatibility.

**Total syscalls**: ~450 (matching Linux kernel 6.1 x86-64)

**Examples**:
- `XSC_OP_READ` (0) - read()
- `XSC_OP_WRITE` (1) - write()
- `XSC_OP_OPEN` (2) - open()
- `XSC_OP_CLOSE` (3) - close()
- `XSC_OP_STAT` (4) - stat()
- `XSC_OP_FSTAT` (5) - fstat()
- `XSC_OP_LSEEK` (8) - lseek()
- `XSC_OP_MMAP` (9) - mmap()
- `XSC_OP_MPROTECT` (10) - mprotect()
- `XSC_OP_MUNMAP` (11) - munmap()
- ... (continues for all ~450 syscalls)

### 4.2 Syscall Categories

XSC supports all modern Linux syscalls including:

**File/IO**: read, write, open, close, stat, fstat, lstat, poll, lseek, mmap, mprotect, munmap, brk, ioctl, pread64, pwrite64, readv, writev, access, pipe, select, dup, dup2, pause, nanosleep, getitimer, alarm, setitimer, getpid, sendfile, socket operations, etc.

**Process**: fork, vfork, execve, exit, wait4, kill, uname, semget, semop, semctl, shmdt, msgget, msgsnd, msgrcv, msgctl, fcntl, flock, fsync, fdatasync, truncate, ftruncate, getdents, getcwd, chdir, fchdir, rename, mkdir, rmdir, creat, link, unlink, symlink, readlink, chmod, fchmod, chown, fchown, lchown, umask, gettimeofday, getrlimit, getrusage, sysinfo, times, ptrace, getuid, syslog, getgid, setuid, setgid, geteuid, getegid, setpgid, getppid, getpgrp, setsid, setreuid, setregid, getgroups, setgroups, setresuid, getresuid, setresgid, getresgid, getpgid, setfsuid, setfsgid, getsid, capget, capset, rt_sigaction, rt_sigprocmask, rt_sigpending, rt_sigtimedwait, rt_sigqueueinfo, rt_sigsuspend, sigaltstack, personality, statfs, fstatfs, prctl, arch_prctl, etc.

**Memory**: mmap, munmap, mprotect, madvise, shmget, shmat, shmctl, mremap, msync, mincore, mlock, munlock, mlockall, munlockall, mbind, set_mempolicy, get_mempolicy, migrate_pages, move_pages, etc.

**Network**: socket, connect, accept, sendto, recvfrom, sendmsg, recvmsg, shutdown, bind, listen, getsockname, getpeername, socketpair, setsockopt, getsockopt, accept4, recvmmsg, sendmmsg, etc.

**Advanced**: epoll_create, epoll_ctl, epoll_wait, signalfd, timerfd_create, eventfd, fallocate, timerfd_settime, timerfd_gettime, signalfd4, eventfd2, epoll_create1, inotify_init, inotify_add_watch, inotify_rm_watch, openat, mkdirat, mknodat, fchownat, futimesat, newfstatat, unlinkat, renameat, linkat, symlinkat, readlinkat, fchmodat, faccessat, pselect6, ppoll, splice, sync_file_range, tee, vmsplice, utimensat, epoll_pwait, fallocate, timerfd_settime, timerfd_gettime, accept4, signalfd4, eventfd2, epoll_create1, dup3, pipe2, inotify_init1, preadv, pwritev, rt_tgsigqueueinfo, perf_event_open, recvmmsg, name_to_handle_at, open_by_handle_at, clock_adjtime, syncfs, sendmmsg, setns, getcpu, process_vm_readv, process_vm_writev, kcmp, finit_module, sched_setattr, sched_getattr, renameat2, seccomp, getrandom, memfd_create, bpf, execveat, userfaultfd, membarrier, mlock2, copy_file_range, preadv2, pwritev2, pkey_mprotect, pkey_alloc, pkey_free, statx, io_pgetevents, rseq, pidfd_send_signal, io_uring_setup, io_uring_enter, io_uring_register, open_tree, move_mount, fsopen, fsconfig, fsmount, fspick, pidfd_open, clone3, close_range, openat2, pidfd_getfd, faccessat2, process_madvise, epoll_pwait2, mount_setattr, quotactl_fd, landlock_create_ruleset, landlock_add_rule, landlock_restrict_self, memfd_secret, process_mrelease

### 4.3 Removed Syscalls

Only truly obsolete/never-implemented syscalls are excluded:
- `uselib` (removed from kernel)
- `create_module`, `query_module`, `get_kernel_syms` (old module system)
- `afs_syscall`, `security`, `tuxcall`, `vserver` (never implemented)
- 32-bit compatibility layer (XSC is 64-bit only)

### 4.4 Source Compatibility

**Any program that compiles on RHEL 9 / Enterprise Linux compiles on XSC.**

The syscall API is identical - only the transport mechanism (rings vs CPU instructions) differs.

## 5. Implementation Details

### 5.1 Glibc Sysdeps Location
- `sysdeps/unix/sysv/linux/x86_64-xsc/` (x86-64)
- `sysdeps/unix/sysv/linux/x86_64-xsc/x32/` (x32 - not implemented)
- `sysdeps/unix/sysv/linux/aarch64-xsc/` (ARM64)

### 5.2 Syscall Wrapper Pattern
```c
#ifdef __GLIBC_XSC__
ssize_t __libc_read(int fd, void *buf, size_t count) {
    return __xsc_submit_read(fd, buf, count);
}
#else
// Fallback for cross-compilation
ssize_t __libc_read(int fd, void *buf, size_t count) {
    return syscall(SYS_read, fd, buf, count);
}
#endif
```

### 5.3 Required Functions
- `__xsc_init()` - Initialize XSC device, map rings
- `__xsc_submit_sync()` - Submit SQE, wait for CQE
- `__xsc_submit_async()` - Submit SQE, return immediately
- `__xsc_poll_cqe()` - Check for completed operations

### 5.4 Kernel Module Location
`drivers/xsc/xsc_main.c`

### 5.5 Binary Validation
Check for forbidden instructions:
```bash
objdump -d binary | grep -E 'syscall|sysenter|int.*0x80'
# Empty output = XSC-compliant
```

## 6. Toolchain (Build Infrastructure, Not ABI)

### 6.1 GCC Configuration

**x86-64**:
```bash
./configure \
    --target=x86_64-xsc-linux-gnu \
    --with-arch=x86-64 \
    --enable-languages=c,c++ \
    --disable-multilib
```

**x32** (not implemented):
```bash
./configure \
    --target=x86_64-xsc-linux-gnux32 \
    --with-arch=x86-64 \
    --with-abi=x32 \
    --enable-languages=c,c++ \
    --disable-multilib
```

**Required Flags**:
- `-mno-syscall` - Emit error if syscall instruction generated
- Can use: `-fno-builtin-syscall` as fallback

### 6.2 Binutils Configuration
```bash
./configure \
    --target=x86_64-xsc-linux-gnu \
    --enable-gold \
    --enable-ld=default
```

**Linker Modifications**:
- Automatically add `.note.XSC-ABI` section
- Verify no syscall instructions in linked binary
- Reject standard x86_64 libraries

### 6.3 Glibc Configuration
```bash
./configure \
    --host=x86_64-xsc-linux-gnu \
    --prefix=/usr \
    --enable-kernel=6.1.0
```

## 7. Distribution Packaging (Not ABI)

### 7.1 Package Naming
- **Debian**: `package_version_xsc-amd64.deb`
- **RPM**: `package-version.x86_64-xsc.rpm`

### 7.2 Repository Structure
**Debian**:
```
dists/bookworm/main/binary-xsc-amd64/Packages
pool/main/p/package/package_version_xsc-amd64.deb
```

**RPM**:
```
BaseOS/x86_64-xsc/os/Packages/package-version.x86_64-xsc.rpm
BaseOS/x86_64-xsc/os/repodata/
```

### 7.3 Architecture Lockdown

**Debian** (`/var/lib/dpkg/arch`):
```
xsc-amd64
```

**RPM** (`/etc/rpm/macros.xsc`):
```
%_arch x86_64-xsc
%_target_cpu x86_64
%_build_arch x86_64-xsc
```

## 8. Security (ABI-Level)

### 8.1 Syscall Instruction Blocking
- XSC kernel SHOULD trap syscall/sysenter/int 0x80
- Send SIGSYS to process on attempt
- Log violation (optional)

### 8.2 Ring Security
- Rings mapped read-only (SQ) and write-only (CQ) where possible
- Validate all SQE fields before processing
- Prevent ring buffer overflow attacks

### 8.3 Process Isolation
- Each process gets isolated ring buffers
- Cannot access other process rings
- XSC device fd not inheritable by default

## 9. Performance (Why This ABI Exists)

### 9.1 Batching - The Point
Submit multiple SQEs before requesting kernel processing:
```c
// Submit batch of operations
for (int i = 0; i < n; i++) {
    __xsc_queue_sqe(&sqes[i]);
}
__xsc_submit_batch(); // Single kernel entry
```

### 9.2 Async Operations
Non-blocking operations return immediately:
```c
__xsc_submit_async(sqe);  // Returns immediately
// ... do other work ...
__xsc_poll_cqe(cqe);      // Check if complete
```

## 10. What Doesn't Change (Still Standard Linux)

- **Instruction set**: Normal x86-64/ARM64 code
- **ELF format**: Standard sections, headers, linking
- **Calling conventions**: SysV ABI (RDI, RSI, RDX, RCX, R8, R9)
- **Memory layout**: Standard address space
- **Signals**: Standard signal delivery
- **File descriptors**: Same concept, same numbering
- **Processes/threads**: Same model
- **Virtual memory**: Same semantics

### 10.1 Cross-Compilation
XSC glibc includes syscall fallbacks for cross-compilation:
```c
#ifdef __GLIBC_XSC__
    // Use XSC rings
#else
    // Use standard syscalls (for building on non-XSC)
#endif
```

### 10.2 Mixed Environments
**NOT SUPPORTED**: Cannot mix XSC and standard binaries on same system.

## 11. Summary

**The actual ABI**: Syscalls go through `/dev/xsc` rings instead of CPU instructions.

**Everything else**: Packaging infrastructure to keep repos separate.

**Minimal change, massive rebuild**: The ABI is tiny. Rebuilding every package is the hard part.

## 12. Version History

- **v1.0** (2025-10-12): Initial XSC ABI specification
  - Ring-based syscall mechanism
  - x86-64 and ARM64 support (implemented)
  - x32 support defined (not yet implemented)
  - Kernel 6.1+ required

## 13. Reference Implementation

- **Kernel**: Linux 6.1 with XSC driver
- **Device**: `/dev/xsc` (major 248)
- **Test Program**: `xsc_ring_test.c`
- **ISOs**: Debian Bookworm, AlmaLinux 9

## 14. Files

```
/dev/xsc                                 - XSC device node
/etc/xsc/triplet                         - Architecture triplet
/lib/x86_64-xsc-linux-gnu/               - XSC libraries (64-bit)
/lib/x86_64-xsc-linux-gnux32/            - XSC libraries (x32, not implemented)
/usr/bin/x86_64-xsc-linux-gnu-gcc        - XSC compiler (64-bit)
/usr/bin/x86_64-xsc-linux-gnux32-gcc     - XSC compiler (x32, not implemented)
```

---

**Specification Status**: Draft v1.0
**Last Updated**: 2025-10-12
**Maintainer**: XSC Project
