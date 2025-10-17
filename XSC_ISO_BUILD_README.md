# XSC Full Debian ISO Build System

Complete automated build system for creating a Debian Bookworm DVD-1 ISO with all ~2,600 packages cross-compiled for the XSC architecture (x86_64-xsc-linux-gnu).

## What This Is

A production-ready build system that:
- Builds **2,630 Debian packages** from source using XSC cross-compilation toolchain
- Creates a **bootable DVD-1 ISO** (~4.7 GB) with all packages
- Runs **autonomously for 60-120 hours** on an 80-core server
- Uses **official Debian build tools** (sbuild, debootstrap, debian-cd)
- Handles **dependency resolution** automatically with 4-stage builds
- Generates **self-contained ISO** (no network required for installation)

## Quick Start

```bash
# 1. Upload to server
scp *.sh bx.ee:/storage/icloud-backup/build/scripts/

# 2. Setup environment (one-time, ~20 minutes)
ssh bx.ee
cd /storage/icloud-backup/build/scripts
sudo ./setup-xsc-sbuild.sh

# 3. Generate package lists (~5 minutes)
./generate-stage-lists.sh

# 4. Start build (60-120 hours, autonomous)
nohup ./xsc-master-builder.sh > build.log 2>&1 &

# 5. Monitor progress
./monitor-xsc-build.sh

# 6. Build ISO (~30 minutes, after packages complete)
./build-xsc-iso.sh

# 7. Test
qemu-system-x86_64 -m 4096 -cdrom /storage/icloud-backup/build/iso-output/*.iso
```

## File Structure

### Documentation (4 files - 61 KB total)

| File | Size | Purpose |
|------|------|---------|
| **XSC_ISO_BUILD_README.md** | This file | Quick overview and file guide |
| **QUICK_START_GUIDE.md** | 9.3 KB | Step-by-step instructions for building |
| **XSC_FULL_ISO_BUILD_STRATEGY.md** | 28 KB | Complete technical strategy and research |
| **XSC_BUILD_SUMMARY.md** | 14 KB | Executive summary and architecture |
| **BUILD_CHECKLIST.md** | 10 KB | Day-by-day execution checklist |

### Build Scripts (5 files - 38 KB total)

| File | Size | Purpose |
|------|------|---------|
| **setup-xsc-sbuild.sh** | 6.3 KB | One-time environment setup |
| **generate-stage-lists.sh** | 7.0 KB | Generate dependency-ordered package lists |
| **xsc-master-builder.sh** | 10 KB | Main build orchestrator (autonomous) |
| **build-xsc-iso.sh** | 8.8 KB | Create bootable ISO from packages |
| **monitor-xsc-build.sh** | 5.7 KB | Real-time progress monitoring |

## Documentation Guide

### Start Here

1. **XSC_ISO_BUILD_README.md** (this file)
   - Read this first for overview
   - Understand what files do what
   - Get quick start commands

### For Building

2. **QUICK_START_GUIDE.md** - Your primary reference during build
   - Step-by-step instructions
   - Troubleshooting guide
   - Resource requirements
   - Success criteria
   - **Use this while executing the build**

3. **BUILD_CHECKLIST.md** - Daily execution checklist
   - Day-by-day tasks
   - Checkboxes for progress tracking
   - Timeline with time estimates
   - Quick troubleshooting reference
   - **Print this and check off items as you go**

### For Understanding

4. **XSC_FULL_ISO_BUILD_STRATEGY.md** - Complete technical reference
   - Research on Debian ISO building
   - Dependency resolution algorithms
   - Build infrastructure design
   - Quality assurance procedures
   - **Read this to understand the "why" behind decisions**

5. **XSC_BUILD_SUMMARY.md** - Executive overview
   - High-level architecture
   - Resource estimates
   - Success metrics
   - **Read this for presentations or planning**

## Build Script Guide

### Setup Phase

**setup-xsc-sbuild.sh** - Run once before building
- Creates sbuild chroot for XSC cross-compilation
- Installs XSC toolchain in chroot
- Configures build environment
- **Requires sudo/root access**
- **Runtime: 15-20 minutes**

```bash
sudo ./setup-xsc-sbuild.sh
```

### Package List Phase

**generate-stage-lists.sh** - Creates package lists
- Downloads Debian metadata
- Extracts DVD-1 package list
- Creates 4-stage dependency-ordered lists
- **No special permissions needed**
- **Runtime: 5-10 minutes**

```bash
./generate-stage-lists.sh
```

Outputs:
- `stage1-packages.txt` - 30 build-essential packages
- `stage2-packages.txt` - 160 essential+required packages
- `stage3-packages.txt` - 800 important+standard packages
- `stage4-packages.txt` - 1,640 optional DVD-1 packages

### Build Phase

**xsc-master-builder.sh** - Main build orchestrator
- Builds all packages in dependency order
- Handles parallelization (1-80 jobs)
- Automatic retry on failures
- Progress tracking and logging
- **Runs autonomously**
- **Runtime: 60-120 hours**

```bash
# Build everything
nohup ./xsc-master-builder.sh > build.log 2>&1 &

# Or build specific stages
./xsc-master-builder.sh stage1
./xsc-master-builder.sh stage2
./xsc-master-builder.sh stage3
./xsc-master-builder.sh stage4

# Retry failed builds
./xsc-master-builder.sh retry

# Generate report
./xsc-master-builder.sh report
```

### Monitoring

**monitor-xsc-build.sh** - Live progress dashboard
- Shows build progress per stage
- Resource usage (CPU, RAM, disk)
- Active build processes
- Recent activity
- Time estimates
- **No special permissions needed**
- **Continuous refresh**

```bash
./monitor-xsc-build.sh
```

### ISO Creation

**build-xsc-iso.sh** - Creates bootable ISO
- Copies all built packages
- Generates repository metadata
- Installs XSC kernel
- Configures bootloader
- Creates ISO image
- **No special permissions needed**
- **Runtime: 20-30 minutes**

```bash
./build-xsc-iso.sh
```

Output: `/storage/icloud-backup/build/iso-output/xsc-debian-12.8-dvd1-amd64.iso`

## Build Overview

### Package Stages

The build is organized into 4 stages based on Debian package priorities:

```
Stage 1: Build-Essential (30 packages)
├── gcc, binutils, make
├── Sequential build (bootstrap requirement)
└── Time: ~30 minutes

Stage 2: Essential + Required (160 packages)
├── bash, coreutils, libc6, dpkg, apt
├── 20 parallel builds
└── Time: ~4 hours

Stage 3: Important + Standard (800 packages)
├── vim, openssh, gcc, python3, network tools
├── 40 parallel builds
└── Time: ~20 hours

Stage 4: Optional (1,640 packages)
├── Desktop, servers, development tools, libraries
├── 80 parallel builds
└── Time: 35-100 hours

Total: ~2,630 packages, 60-120 hours
```

### Build Flow

```
setup-xsc-sbuild.sh → Creates build environment
         ↓
generate-stage-lists.sh → Creates package lists (4 stages)
         ↓
xsc-master-builder.sh → Builds all packages
         ↓              (monitor with monitor-xsc-build.sh)
build-xsc-iso.sh → Creates bootable ISO
         ↓
ISO ready for testing
```

### Resource Requirements

**Minimum:**
- 40 cores
- 64 GB RAM
- 120 GB disk space

**Recommended (bx.ee):**
- 80 cores (full utilization in Stage 4)
- 128 GB RAM
- 200 GB disk space
- SSD for build directories

**Network:**
- 20 GB download (source packages)

## Timeline

| Phase | Duration | Active Time |
|-------|----------|-------------|
| Setup | Day 1 | 30 minutes |
| Package Lists | Day 1 | 10 minutes |
| Test Build | Day 1 | 15 minutes |
| Main Build | Days 1-5 | 60-120 hours (autonomous) |
| Monitoring | Days 2-5 | 10 min/day |
| ISO Creation | Day 5 | 30 minutes |
| Testing | Day 5-6 | 2 hours |
| **Total** | **5-6 days** | **~8 hours active work** |

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] Stages 1-2 complete (~190 packages)
- [ ] ISO boots to shell
- [ ] Basic commands work
- [ ] No syscall instructions

### Full Success
- [ ] All 2,630 packages built
- [ ] >95% success rate
- [ ] ISO boots with installer
- [ ] XSC kernel functional

## Output Files

After successful build:

```
/storage/icloud-backup/build/
├── xsc-debian-full/              # Build directory
│   ├── stage{1,2,3,4}-packages.txt
│   ├── results/
│   │   └── stage{1,2,3,4}/      # ~2,600 .deb files (12 GB)
│   ├── logs/                     # Build logs (5 GB)
│   ├── completed/                # Success tracking
│   └── failed/                   # Failure tracking
│
├── xsc-iso-build/                # ISO staging
│   ├── pool/main/                # All packages
│   ├── dists/bookworm/           # APT repository
│   └── boot/                     # Kernel + bootloader
│
└── iso-output/
    ├── xsc-debian-12.8-dvd1-amd64.iso      # Bootable ISO (4.7 GB)
    ├── xsc-debian-12.8-dvd1-amd64.iso.sha256
    └── xsc-debian-12.8-dvd1-amd64.iso.md5
```

## Key Features

1. **Automated Dependency Resolution**
   - Topological sort of package dependencies
   - Handles circular dependencies with build profiles
   - 4-stage bootstrap strategy

2. **Scalable Parallelization**
   - 1 → 20 → 40 → 80 concurrent builds
   - Full utilization of 80-core server
   - GNU parallel for job management

3. **Robust Error Handling**
   - Automatic retry on failures
   - Detailed per-package logging
   - Graceful degradation

4. **Production Quality**
   - Uses official Debian tools (sbuild)
   - Generates proper .deb packages
   - Creates APT-compatible repository
   - Bootable ISO with installer

## Troubleshooting

### Quick Fixes

**Build won't start:**
```bash
schroot -l | grep xsc  # Check chroot exists
sudo ./setup-xsc-sbuild.sh  # Re-run setup
```

**Out of disk space:**
```bash
rm -rf xsc-debian-full/sources/*  # Clean downloads
find . -name '*.build' -delete    # Clean build dirs
```

**Package fails:**
```bash
less logs/<package>-build.log     # Check log
./xsc-master-builder.sh retry     # Retry all failures
```

**Too slow:**
```bash
./monitor-xsc-build.sh            # Check resources
export DEB_BUILD_OPTIONS="parallel=40"  # Reduce if needed
```

See **QUICK_START_GUIDE.md** for detailed troubleshooting.

## Technical Details

**Architecture:** xsc-amd64 (Debian architecture for XSC)

**Toolchain:** x86_64-xsc-linux-gnu (cross-compilation)

**Build Tool:** sbuild (official Debian package builder)

**ISO Tool:** debian-cd + genisoimage

**Kernel:** Linux 6.1 with XSC driver

**Parallelization:** GNU parallel with resource management

## What Makes This Different

### vs Standard Debian Build
- **Standard:** Uses amd64 architecture, syscall instructions
- **XSC:** Uses xsc-amd64 architecture, ring-based syscalls
- **All packages must be recompiled** with XSC toolchain

### vs Manual Package Building
- **Manual:** Build packages one-by-one, handle dependencies manually
- **This System:** Automated dependency resolution, parallel builds, 80-core utilization

### vs Docker-Based Build
- **Docker:** Limited parallelism, container overhead
- **This System:** Native sbuild chroots, full 80-core parallelism

## Workflow Summary

```bash
# Day 1: Setup (1 hour active work)
ssh bx.ee
sudo ./setup-xsc-sbuild.sh
./generate-stage-lists.sh
nohup ./xsc-master-builder.sh &

# Days 2-4: Monitor (10 min/day)
./monitor-xsc-build.sh
./xsc-master-builder.sh report

# Day 5: Complete (2 hours active work)
./build-xsc-iso.sh
qemu-system-x86_64 -cdrom iso-output/*.iso

# Done!
```

## Getting Help

1. **Quick Start:** Read `QUICK_START_GUIDE.md`
2. **Daily Tasks:** Follow `BUILD_CHECKLIST.md`
3. **Technical Details:** Refer to `XSC_FULL_ISO_BUILD_STRATEGY.md`
4. **Overview:** Review `XSC_BUILD_SUMMARY.md`

## Prerequisites Checklist

Before starting, ensure you have:

- [ ] Server: bx.ee (80 cores, 128GB RAM)
- [ ] Disk space: 200+ GB free
- [ ] XSC toolchain: `/storage/icloud-backup/build/xsc-toolchain-x86_64-base`
- [ ] XSC kernel: `/storage/icloud-backup/build/linux-6.1`
- [ ] Root access for initial setup
- [ ] Network access for downloading packages

## License

Build scripts provided as-is for XSC project.
Individual Debian packages retain their respective licenses.

## Credits

- **Build System:** Based on Debian buildd infrastructure
- **Tools:** sbuild, debootstrap, debian-cd, GNU parallel
- **Server:** bx.ee (80-core build machine)
- **Created:** 2025-10-14

---

## Next Steps

1. **Read:** `QUICK_START_GUIDE.md` for detailed instructions
2. **Print:** `BUILD_CHECKLIST.md` for daily tracking
3. **Upload:** All `.sh` files to server
4. **Start:** `sudo ./setup-xsc-sbuild.sh`

**Questions?** Refer to the appropriate documentation file above.

**Ready to build!** Start with the Quick Start section.
