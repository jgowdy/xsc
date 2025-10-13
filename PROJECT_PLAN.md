# XSC (eXtended SysCall) Operating System Project

## Project Overview

Build a complete Linux distribution using XSC ring-based syscall mechanism instead of traditional syscall/sysenter/int 0x80 instructions.

**Critical**: ALL binaries must be compiled with XSC-aware toolchain. Standard x86_64 binaries WILL NOT WORK on XSC systems.

---

## Architecture Details

### XSC Mechanism
- **Ring-based syscalls**: User programs submit syscall requests to shared memory rings
- **No syscall instructions**: syscall/sysenter/int 0x80 instructions are FORBIDDEN and will cause SIGSYS
- **Kernel driver**: `/storage/icloud-backup/build/linux-6.1/drivers/xsc/`
- **Ring communication**: Submission Queue Entries (SQE) → Kernel → Completion Queue Entries (CQE)

### Architecture Triplet
- **GNU Triplet**: `x86_64-xsc-linux-gnu` (x86-64), `aarch64-xsc-linux-gnu` (ARM64)
- **Dpkg Architecture**: `xsc-amd64` (Debian/Ubuntu)
- **RPM Architecture**: `x86_64-xsc` (RHEL/Fedora/Alma/Rocky)

### Package Manager Lockdown
All ISOs configure package managers to REJECT standard architecture packages:
- Debian: `dpkg --print-architecture` returns `xsc-amd64`
- RHEL: `rpm --eval %_arch` returns `x86_64-xsc`

---

## Project Phases

### ✅ Phase 1: XSC Kernel (COMPLETE)
- [x] XSC kernel driver implementation
- [x] Ring-based syscall mechanism
- [x] Linux 6.1 kernel with XSC patches
- [x] Kernel builds and loads module
- **Location**: `/storage/icloud-backup/build/linux-6.1/`

### 🟡 Phase 2: Minimal XSC Test Environment (IN PROGRESS - Option A)
**Goal**: Create minimal environment to test XSC kernel with simple XSC-compiled binaries

#### 2.1 XSC Glibc Base
- [ ] Build minimal XSC glibc with sysdeps
- [ ] Implement XSC ring submission functions (`__xsc_submit_*`)
- [ ] Test basic syscalls (read, write, open, close)
- **Sysdeps location**: `/storage/icloud-backup/build/create_glibc_xsc.sh`
- **Patches**:
  - `glibc-configure-xsc.patch`
  - `glibc-makeconfig-xsc.patch`
  - `glibc-csu-libc-start.patch`

#### 2.2 XSC Test Binary
- [ ] Build `xsc_ring_test` with XSC glibc
- [ ] Add ELF notes to identify XSC binaries (`.note.XSC-ABI`)
- [ ] Verify binary has NO syscall instructions
- [ ] Package as `xsc-tools_1.0-1_xsc-amd64.deb`

#### 2.3 Minimal Test ISO
- [ ] Create minimal bootable ISO with:
  - XSC kernel (6.1)
  - XSC kernel modules
  - Initramfs with XSC driver
  - xsc_ring_test binary
- [ ] Boot in QEMU
- [ ] Load XSC module
- [ ] Run xsc_ring_test
- [ ] Verify syscalls work through rings

**Deliverable**: Working proof-of-concept showing XSC kernel executing XSC binaries

### 🔴 Phase 3: XSC Toolchain (TODO - Required for Option B)
**Goal**: Complete toolchain for building XSC binaries

#### 3.1 XSC GCC
- [ ] Build gcc targeting `x86_64-xsc-linux-gnu`
- [ ] Configure to link against XSC glibc
- [ ] Add `-mno-syscall` flag (emit compile error on syscall instructions)
- [ ] Test compilation produces XSC-only binaries

#### 3.2 XSC Binutils
- [ ] Build binutils for `x86_64-xsc-linux-gnu`
- [ ] Modify ld to add `.note.XSC-ABI` section to all binaries
- [ ] Add ELF machine type or OSABI field for XSC

#### 3.3 XSC Glibc Complete
- [ ] Complete all sysdeps implementations:
  - Process: fork, vfork, clone, execve, wait
  - I/O: read, write, open, close, ioctl, fcntl
  - Network: socket, bind, listen, accept, connect, send, recv
  - Filesystem: stat, mkdir, unlink, rename, mount
  - IPC: pipe, mmap, futex, semaphore
  - Signals: sigaction, sigprocmask, kill
  - Time: clock_gettime (vDSO), nanosleep
- [ ] Build complete libc.so with all XSC syscalls
- [ ] Create XSC glibc package for each distro

### 🔴 Phase 4: Core XSC Userland (TODO - Option B)
**Goal**: Rebuild essential system packages with XSC toolchain

#### 4.1 Essential Utilities
- [ ] bash (shell)
- [ ] coreutils (ls, cp, mv, rm, cat, etc.)
- [ ] util-linux (mount, umount, dmesg, etc.)
- [ ] findutils (find, xargs)
- [ ] grep, sed, awk
- [ ] tar, gzip, bzip2, xz

#### 4.2 System Components
- [ ] systemd or sysvinit
- [ ] udev
- [ ] dbus
- [ ] kmod (module loading)

#### 4.3 Package Managers
- [ ] dpkg (Debian)
- [ ] apt (Debian)
- [ ] rpm (RHEL)
- [ ] dnf (RHEL)

**Deliverable**: Minimal bootable system with working shell and basic utilities

### 🔴 Phase 5: Full XSC Distribution (TODO - Ultimate Goal)
**Goal**: Complete XSC Linux distributions with all packages

#### 5.1 Network Stack
- [ ] iproute2
- [ ] iptables/nftables
- [ ] openssh
- [ ] curl, wget

#### 5.2 Development Tools
- [ ] make, cmake
- [ ] git
- [ ] python3
- [ ] perl
- [ ] pkg-config

#### 5.3 Server Software
- [ ] nginx / apache
- [ ] postgresql / mysql
- [ ] redis
- [ ] docker

#### 5.4 Desktop Environment (Optional)
- [ ] X11 or Wayland
- [ ] GNOME / KDE / XFCE
- [ ] Firefox / Chromium

**Deliverable**: Full-featured XSC Linux distribution comparable to standard distros

### 🔴 Phase 6: XSC CI/CD & Repository (TODO)
**Goal**: Automated build system for XSC packages

#### 6.1 Build Infrastructure
- [ ] Automated package building for Debian
- [ ] Automated package building for AlmaLinux
- [ ] Automated package building for Rocky Linux
- [ ] Build farm with XSC toolchain

#### 6.2 Package Repository
- [ ] APT repository for Debian packages (xsc-amd64)
- [ ] DNF repository for RHEL packages (x86_64-xsc)
- [ ] Package signing infrastructure
- [ ] Mirror network

#### 6.3 ISO Generation
- [ ] Automated daily ISO builds
- [ ] Testing infrastructure (QEMU)
- [ ] Release process

**Deliverable**: Self-sustaining XSC distribution with automated builds

---

## Current Status

### Completed Work
- ✅ XSC kernel driver fully implemented
- ✅ Three ISO builds (Debian, AlmaLinux, Rocky) with XSC kernel
- ✅ Architecture triplet defined (`x86_64-xsc-linux-gnu`)
- ✅ Package manager lockdown configured
- ✅ XSC glibc sysdeps code written
- ✅ xsc_ring_test.c test program written

### Current Issues
1. **Standard binaries on ISO**: All ISOs currently contain standard x86_64 binaries that use syscall instructions - these WILL NOT WORK on XSC kernel
2. **No XSC glibc built**: Sysdeps code exists but glibc hasn't been compiled
3. **No XSC toolchain**: Can't build XSC binaries without XSC-aware gcc/binutils
4. **ELF tagging missing**: No way to identify XSC binaries vs standard x86_64

### Immediate Next Steps (Phase 2 - Option A)
1. Build minimal XSC glibc with basic syscalls (read, write, open, close, exit)
2. Implement XSC ring submission library (`libxsc.so`)
3. Build xsc_ring_test against XSC glibc
4. Create minimal test ISO with XSC binary
5. Boot and verify XSC syscalls work

---

## File Locations

### Build Scripts
- `/Users/jgowdy/flexsc/build-debian-docker.sh` - Debian ISO build
- `/Users/jgowdy/flexsc/build-alma-docker.sh` - AlmaLinux ISO build
- `/Users/jgowdy/flexsc/build-rocky-docker.sh` - Rocky ISO build
- `/Users/jgowdy/flexsc/PROJECT_PLAN.md` - **THIS FILE**

### Server Locations
- Build directory: `/storage/icloud-backup/build/`
- Kernel source: `/storage/icloud-backup/build/linux-6.1/`
- XSC driver: `/storage/icloud-backup/build/linux-6.1/drivers/xsc/`
- Glibc sysdeps: `/storage/icloud-backup/build/create_glibc_xsc.sh`
- Test program: `/storage/icloud-backup/build/xsc_ring_test.c`
- ISOs: `/storage/icloud-backup/build/iso/*.iso`

### Built ISOs (with XSC kernel but standard userland)
- `xsc-debian-bookworm-amd64.iso` - 92MB
- `xsc-almalinux-9-amd64.iso` - 840MB
- `xsc-rockylinux-9-amd64.iso` - 797MB

---

## Technical References

### XSC Ring Structure
```c
struct xsc_sqe {  // Submission Queue Entry
    uint8_t opcode;      // Syscall opcode
    int32_t fd;          // File descriptor
    uint64_t addr;       // Buffer address
    uint32_t len;        // Buffer length
    uint64_t user_data;  // User context
    // ... additional fields
};

struct xsc_cqe {  // Completion Queue Entry
    uint64_t user_data;  // Matches SQE
    int32_t res;         // Return value
    uint32_t flags;      // Status flags
};
```

### XSC Glibc Syscall Pattern
```c
// Example: read() wrapper
ssize_t __libc_read(int fd, void *buf, size_t count) {
    #ifdef __GLIBC_XSC__
        return __xsc_submit_read(fd, buf, count);
    #else
        // Fallback for non-XSC systems
        return syscall(SYS_read, fd, buf, count);
    #endif
}
```

### Package Naming
- Debian: `package-name_version_xsc-amd64.deb`
- RPM: `package-name-version.x86_64-xsc.rpm`

---

## Key Decisions

### Why Ring-Based Syscalls?
1. **Performance**: Batching syscalls reduces context switches
2. **Async I/O**: Natural async syscall model
3. **Security**: Syscall instructions can be completely disabled in hardware
4. **Flexibility**: Easier to add new syscalls without kernel recompilation

### Why Complete ABI Change?
- Can't mix XSC and standard binaries (incompatible syscall mechanisms)
- Requires complete ecosystem: kernel + glibc + toolchain + packages
- Architecture lockdown prevents accidental installation of incompatible packages

### Why Three Distros?
- Debian: Community standard, dpkg/apt ecosystem
- AlmaLinux: RHEL clone, enterprise support
- Rocky: RHEL clone, alternative to Alma
- Demonstrates XSC works across distro families

---

## Success Criteria

### Phase 2 Success (Option A)
- [ ] XSC kernel boots in QEMU
- [ ] XSC module loads successfully
- [ ] xsc_ring_test binary executes
- [ ] Syscalls complete via rings (not instructions)
- [ ] CQE returns correct results

### Phase 5 Success (Option B - Ultimate Goal)
- [ ] Full desktop environment boots
- [ ] All standard utilities work
- [ ] Can compile and run new programs
- [ ] Package manager can install XSC packages
- [ ] Network stack fully functional
- [ ] Performance comparable to standard Linux

---

## Notes for Context Compaction

**CRITICAL**: When summarizing this conversation for context compaction, ALWAYS include the full content of this PROJECT_PLAN.md file. The project is complex and losing this context will result in confusion and wasted work.

Current phase: **Phase 2 (Option A) - Minimal XSC Test Environment**

Last updated: 2025-10-12
