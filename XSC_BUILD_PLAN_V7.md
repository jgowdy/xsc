# XSC v7 Build Plan - Basic (Non-CET) Debian ISO

**Based on**: `XSC_OS_Design_v7.md` and `XSC_Technical_Supplement_v7-D.md`
**Target**: Bootable Debian ISO with XSC base syscall mechanism
**Platform**: x86_64 only (arm64 later)

---

## Phase 1: Kernel Implementation ✅ (Already Done)

**Status**: Basic XSC kernel drivers exist in `kernel-patches/drivers/xsc/`

**What we have**:
- `xsc_core.c` - Core SR/CR/CTRL mapping
- `xsc_consume_*.c` - Syscall handlers (fs, net, sync, timer)
- Basic worker thread implementation

**What needs updating for v7**:
- Ensure adaptive polling is the primary subsystem
- Remove/disable null doorbell (CONFIG_XSC_SOFT_DOORBELL=n)
- Add Lockdown LSM config (for future CET build)
- Update Kconfig to match v7 spec

---

## Phase 2: Toolchain (XSC Base)

**Goal**: Build cross-compiler that forbids syscall/svc instructions

**Current status**: We have `xsc-toolchain-x86_64-base` built

**Actions needed**:
1. ✅ Verify toolchain enforces no inline syscall instructions
2. ✅ Test with simple programs
3. Continue using existing toolchain

---

## Phase 3: Minimal Userspace

**Goal**: Core utilities compiled for XSC ABI

**Current status**: 100 packages built via cross-compilation

**What we need for minimal ISO**:
- ✅ bash, coreutils, util-linux, findutils, grep, sed
- ✅ ncurses
- ⏳ systemd OR sysvinit (systemd failed, need to fix or use busybox init)
- ⏳ network tools (iproute2, net-tools)
- ⏳ Essential libraries (openssl, zlib, libssl)

**Actions**:
1. Use existing 100 built packages as base
2. Build minimal set for bootable system
3. Create initramfs with busybox + XSC libc shim

---

## Phase 4: glibc XSC Integration

**Goal**: glibc with `syscall()` routed to SR/CR rings

**Status**: Need to implement

**Actions**:
1. Patch glibc to:
   - Read SR/CR/CTRL addresses from auxv (AT_XSC_*)
   - Implement `syscall()` as ring enqueue/wait
   - Add vDSO for clock_gettime, gettimeofday
2. Build `glibc-xsc` package
3. Test with simple programs

---

## Phase 5: Build Minimal Bootable ISO

**Goal**: ISO that boots with XSC kernel + minimal userspace

**Components**:
- XSC kernel 6.1 with drivers/xsc/
- Initramfs with:
  - Busybox (statically linked or with XSC glibc)
  - Essential tools
  - Init script that mounts real rootfs
- Rootfs with:
  - 100 already-built packages
  - Boot manager (grub or syslinux)
  - Basic network config

**Build script**: Adapt existing `build-xsc-iso.sh`

---

## Phase 6: Testing & Validation

**Tests**:
1. Boot ISO in QEMU
2. Verify XSC syscalls working (strace equivalent)
3. Run simple programs (ls, cat, echo)
4. Test network connectivity
5. Verify no trap-based syscalls occur

**Success criteria**:
- ISO boots to shell prompt
- Basic commands work
- Can inspect ring activity
- System is stable

---

## Phase 7: Delivery

**Actions**:
1. Copy ISO from bx.ee to `/Users/jgowdy/Desktop/`
2. Document what works and what doesn't
3. Create next steps for full Debian

---

## Implementation Plan

### Step 1: Update kernel config for v7 spec
- Set CONFIG_XSC_SOFT_DOORBELL=n (disable null doorbell)
- Ensure adaptive polling enabled
- Enable RANDKSTACK, HARDENED_USERCOPY
- Prepare Lockdown config (for future CET)

### Step 2: Build XSC glibc shim
- Create minimal syscall shim
- Route syscall() to SR/CR operations
- Build and test

### Step 3: Create minimal package set
- Essential: bash, coreutils, util-linux, procps, systemd/sysvinit
- Network: iproute2, openssh
- Libraries: ncurses, openssl, zlib
- Total: ~50-100 packages

### Step 4: Build ISO
- Kernel: XSC 6.1
- Initramfs: busybox + mount scripts
- Rootfs: debootstrap base + XSC packages
- Bootloader: grub2

### Step 5: Test and iterate
- Fix boot issues
- Test XSC syscalls
- Verify transparency (strace, audit)

### Step 6: Copy to local
```bash
scp bx.ee:/storage/icloud-backup/build/xsc-debian-base.iso ~/Desktop/
```

---

## Current Assets We Can Leverage

✅ **100 XSC packages already built** in:
- `/storage/icloud-backup/build/xsc-debian-full/results/stage*/`

✅ **XSC kernel 6.1** with drivers in:
- `kernel-patches/drivers/xsc/`

✅ **Cross-compilation toolchain**:
- `/storage/icloud-backup/build/xsc-toolchain-x86_64-base/`

✅ **Build infrastructure**:
- bx.ee server with 80 cores
- Build scripts and automation

---

## Timeline Estimate

| Phase | Time | Dependencies |
|-------|------|--------------|
| Kernel config update | 30 min | None |
| glibc XSC shim | 2-4 hours | Kernel ready |
| Minimal packages | 1-2 hours | Use existing 100 |
| ISO build script | 1-2 hours | All packages ready |
| Testing | 2-4 hours | ISO built |
| **Total** | **6-12 hours** | |

---

## Success Criteria

**Minimum Viable ISO**:
- Boots in QEMU/bare metal
- Reaches shell prompt
- XSC syscalls functional
- Basic commands work (ls, cat, ps, etc.)
- Can demonstrate ring-based syscalls

**Documentation**:
- Boot instructions
- Known limitations
- Next steps for full Debian

---

**Status**: Ready to execute Phase 1 (kernel config update)
