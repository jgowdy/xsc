# XSC OS Project Status

**"All I want to do is... an take your syscall"**

## What We're Building

A complete Linux distribution with ring-based syscalls instead of CPU trap instructions. Full control, no dependencies on Red Hat, Canonical, or anyone else.

## Current Status (2025-10-12)

### ✅ Phase 1: XSC Kernel (COMPLETE)
- **Location**: `/storage/icloud-backup/build/linux-6.1/`
- **Driver**: `drivers/xsc/xsc_main.c`
- **Device**: `/dev/xsc` (major 248, minor 0)
- **Status**: Tested and working
  - Kernel boots in QEMU
  - Module loads successfully
  - Device created properly
  - Ring buffers allocate and map

### 🏗️ Phase 2: XSC Toolchain (IN PROGRESS)
- **Location**: `/storage/icloud-backup/build/xsc-toolchain/`
- **Target**: `x86_64-xsc-linux-gnu`
- **Status**: Building (started 2025-10-12)

#### Components:
1. **Binutils 2.41** - Building
2. **GCC 13.2.0** - Downloading
3. **Glibc 2.38** - Pending
4. **XSC Syscall Wrappers** - Designed

**Build Time**: ~8-12 hours with 80 cores

### 📋 Phase 3: Package Repositories (READY)
- **Location**: `/storage/icloud-backup/repos/xsc/`
- **Structure**:
  ```
  /storage/icloud-backup/repos/xsc/
  ├── debian/bookworm/
  │   ├── dists/bookworm/main/binary-xsc-amd64/
  │   └── pool/
  └── alma/9/
      ├── BaseOS/x86_64-xsc/os/
      └── AppStream/x86_64-xsc/os/
  ```

### 🎯 Next Steps

#### Immediate (after toolchain completes):
1. Test toolchain with hello world
2. Build core packages:
   - bash
   - coreutils
   - systemd
   - util-linux
3. Create XSC userland ISO

#### Short-term:
1. Rebuild ~500 essential packages
2. Set up nginx on bx.ee for package hosting
3. Generate bootable ISO with full XSC system

#### Medium-term:
1. Add x32 support (`x86_64-xsc-linux-gnux32`)
2. Add ARM64 support (`aarch64-xsc-linux-gnu`)
3. Expand package coverage

## Architecture Support

### Implemented:
- **x86-64 Base**: `x86_64-xsc-linux-gnu` (in progress)

### Planned:
- **x86-64 Hardened**: Base + CET (shadow stack + IBT)
- **ARM64 Base**: `aarch64-xsc-linux-gnu`
- **ARM64 Hardened**: Base + PAC (pointer authentication)
- **x32**: `x86_64-xsc-linux-gnux32` (32-bit pointers, 64-bit ISA)

## Build Variants

### Base (Required)
- **Full RHEL 9 API compatibility**
- Ring-based syscalls
- Standard hardening (stack protector, FORTIFY_SOURCE, RELRO)
- Works on all CPUs

### Hardened (Optional)
- **Same API compatibility**
- Ring-based syscalls
- All base hardening
- **Plus hardware CFI:**
  - x86-64: Intel CET (Tiger Lake+, Zen 3+)
  - ARM64: PAC (ARMv8.3-A+, M1+, Graviton3+)

## Package Naming

### Debian:
**Base:**
- `xsc-amd64` - x86-64
- `xsc-arm64` - ARM64 (future)

**Hardened:**
- `xsc-amd64-hardened` - x86-64 + CET
- `xsc-arm64-hardened` - ARM64 + PAC (future)

### RPM (AlmaLinux):
**Base:**
- `x86_64-xsc` - x86-64
- `aarch64-xsc` - ARM64 (future)

**Hardened:**
- `x86_64-xsc-hardened` - x86-64 + CET
- `aarch64-xsc-hardened` - ARM64 + PAC (future)

## What Makes This Different

**Standard Linux**:
```asm
syscall  ; CPU trap to kernel
```

**XSC Linux**:
```c
sqe->opcode = XSC_OP_WRITE;
sqe->fd = 1;
sqe->addr = (uint64_t)buf;
// Kernel polls ring, processes async
```

## Key Technical Points

1. **Not a new architecture** - Same ISA, ELF, calling conventions
2. **Only change**: Syscall mechanism (rings vs instructions)
3. **Everything else**: Standard x86-64/ARM64 Linux
4. **Challenge**: Rebuilding entire ecosystem
5. **Advantage**: Full control, no upstream dependencies

## Files and Locations

### Build Infrastructure:
- `/Users/jgowdy/flexsc/` - Build scripts
- `/storage/icloud-backup/build/` - Build artifacts
- `/storage/icloud-backup/repos/xsc/` - Package repos

### Key Scripts:
- `build-xsc-toolchain.sh` - Toolchain build
- `build-debian-netinstall.sh` - Debian net install ISO
- `build-debian-archive.sh` - Debian full archive ISO
- `build-alma-netinstall.sh` - AlmaLinux net install ISO
- `build-alma-archive.sh` - AlmaLinux full archive ISO
- `build-all-isos.sh` - Build all ISOs
- `test-xsc.sh` - Quick kernel test

### Documentation:
- `XSC-ABI-SPEC.md` - Formal ABI specification
- `PROJECT_PLAN.md` - Complete project plan
- `XSC-STATUS.md` - This file

## Dependencies

**External**: None. We compile everything ourselves.

**Hosting**: Self-hosted on bx.ee

**Build System**: Docker containers on bx.ee (80 cores)

## Timeline Estimate

- **Toolchain**: ~12 hours (in progress)
- **Core userland**: ~8 hours (after toolchain)
- **Full system**: ~48 hours compile time
- **Total to bootable OS**: ~3-4 days wall clock

## Why This Matters

We're proving that a small team can build a complete, modern Linux distribution with architectural changes that the mainline kernel won't accept. No dependency on corporate Linux vendors. No waiting for upstream approval. Full control over the entire stack.

---

**Status**: Actively building
**Last Updated**: 2025-10-12
**Maintainer**: XSC Project
