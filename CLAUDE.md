# Claude Code Session Context

**CRITICAL: Read this file before every context compression**

## Current Mission (Started: 2025-10-14)

### PRIMARY OBJECTIVE
**Build FULL production Debian ISO with all 2,600+ packages compiled from source using XSC toolchain**

This is a **multi-day operation** using 80 cores on bx.ee.

### Status: IN PROGRESS ⚙️

**Location**: `bx.ee:/storage/icloud-backup/build/`

**Current Stage**: Generating package stage lists (prerequisite for master builder)

**Script Running**:
```bash
ssh bx.ee "cd /storage/icloud-backup/build && ./scripts/generate-stage-lists.sh"
```

**Next Step**: Once stage lists complete, start master builder:
```bash
ssh bx.ee "cd /storage/icloud-backup/build && nohup ./xsc-master-builder.sh all > xsc-master-builder.log 2>&1 &"
```

**Expected Duration**: 48-72 hours on 80 cores

**Output**: `/storage/icloud-backup/build/xsc-debian-full/results/` will contain ~2,600 .deb packages

### Build Progress Tracking

Monitor with:
```bash
# Check overall progress
ssh bx.ee "tail -f /storage/icloud-backup/build/xsc-debian-full/logs/master-build.log"

# Check completed packages
ssh bx.ee "wc -l /storage/icloud-backup/build/xsc-debian-full/completed/stage*.txt"

# Check failed packages
ssh bx.ee "ls -lh /storage/icloud-backup/build/xsc-debian-full/failed/"
```

### What We've Completed So Far

✅ All 4 XSC toolchains built:
- `xsc-toolchain-x86_64-base` (no CFI)
- `xsc-toolchain-x86_64-cfi-compat` (hard CFI enforcement, no allowlist)
- `xsc-toolchain-aarch64-base` (no CFI)
- `xsc-toolchain-aarch64-cfi-compat` (hard CFI enforcement, no allowlist)

✅ XSC Linux kernel 6.1 compiled

✅ CFI JIT Allowlist system implemented in kernel-patches/

✅ 207 essential packages built natively in `/storage/icloud-backup/build/xsc-full-native-build/debs/`

### Critical Naming Convention

**Variant Names** (DO NOT confuse these):
- **base**: No CFI enforcement, works on all CPUs
- **cfi-compat**: Hard CFI enforcement, NO allowlist (CONFIG_CFI_JIT_ALLOWLIST=n)
  - Requires Intel CET (Tiger Lake+) or ARM PAC (M1+)
  - JITs will NOT work on this variant
  - Maximum security, zero attack surface

**CFI Allowlist** (separate from variants):
- Lives in `/etc/cfi/allowlist`
- Only matters if kernel has CONFIG_CFI_JIT_ALLOWLIST=y
- Allows specific JIT engines (Java, Node.js) to run with CFI disabled
- **"cfi-compat" variant does NOT have allowlist support** - CFI is always enforced

### Why Building Everything From Source?

Standard Debian packages are compiled for `x86_64-linux-gnu`.
We need `x86_64-xsc-linux-gnu` (ring-based syscalls, not trap-based).

**XSC is fundamentally incompatible** with standard binaries:
- Standard: `syscall` instruction (CPU trap)
- XSC: Ring buffer submit/complete (shared memory)

This means we must rebuild the entire distribution from source.

### The Master Builder Approach

`xsc-master-builder.sh` orchestrates building all packages in dependency order:

**Stage 1** (Bootstrap): Build essential build tools sequentially (1 job)
- binutils, gcc, glibc, make, dpkg, etc.

**Stage 2** (Core): Build core libraries (20 parallel jobs)
- All libraries that stage 3/4 depend on

**Stage 3** (Applications): Build most applications (40 parallel jobs)
- User-facing apps, tools, etc.

**Stage 4** (Final): Build remaining packages (80 parallel jobs)
- Everything else that has dependencies satisfied

### When The Build Completes

1. **Create APT repository** from built packages
2. **Build final ISO** with all packages using `build-xsc-iso.sh`
3. **Test ISO in QEMU**
4. ✅ **Naming normalized to "cfi-compat"** - variant names now clearly indicate hard CFI enforcement

### Build Server Details

**Server**: bx.ee (SSH key-based, no password)
**CPUs**: 80 cores
**RAM**: 251GB
**Disk**: 882GB free in `/storage/icloud-backup/build/`
**Toolchain**: `/storage/icloud-backup/build/xsc-toolchain-x86_64-base/bin/`

### DO NOT

❌ Stop the build once started (it's days of work)
❌ Try to build on local Mac (wrong arch, no space, /storage read-only)
❌ Create "test" ISOs with standard x86_64 packages (they won't work with XSC kernel)
❌ Confuse "cfi-compat" (CFI variant) with "CFI allowlist" (separate kernel feature for base variant)

### DO

✅ Let the 80-core build run for days
✅ Monitor progress regularly
✅ Document any failures for retry
✅ Keep this file updated as mission progresses

## Recent Major Changes

**2025-10-14**:
- Removed XSC allowlist concept (JITs can be recompiled for XSC easily)
- Implemented CFI JIT allowlist (JITs fundamentally incompatible with CET/PAC)
- Started full 2,600-package build process

## Critical Files

- `/kernel-patches/` - All XSC kernel modifications
- `/kernel-patches/security/cfi_allowlist.c` - CFI JIT allowlist implementation
- `/kernel-patches/docs/CFI-JIT-ALLOWLIST.md` - Complete CFI documentation
- `/xsc-master-builder.sh` - Orchestrates full package build
- `/build-xsc-iso.sh` - Creates final bootable ISO

## Build Logs To Check

- `xsc-master-builder.log` - Overall build progress
- `xsc-debian-full/logs/master-build.log` - Detailed master log
- `xsc-debian-full/logs/*.log` - Per-package build logs
- `xsc-debian-full/failed/*.txt` - Lists of failed builds

## Recovery Commands

If context compresses mid-build:

```bash
# Check if build is still running
ssh bx.ee "ps aux | grep xsc-master-builder | grep -v grep"

# Check progress
ssh bx.ee "ls /storage/icloud-backup/build/xsc-debian-full/results/stage*/*.deb | wc -l"

# Generate build report
ssh bx.ee "cd /storage/icloud-backup/build && ./xsc-master-builder.sh report"
```

If build failed/stopped:
```bash
# Retry failed packages
ssh bx.ee "cd /storage/icloud-backup/build && nohup ./xsc-master-builder.sh retry > retry.log 2>&1 &"
```

---

**Last Updated**: 2025-10-14 17:00 UTC
**Status**: Full package build in progress
**ETA**: 48-72 hours from start
