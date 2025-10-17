# XSC Full Debian ISO Build - Quick Start Guide

## Prerequisites

- Server: bx.ee (80 cores, 128GB RAM, 200GB+ free disk space)
- XSC toolchain: `/storage/icloud-backup/build/xsc-toolchain-x86_64-base`
- XSC kernel: `/storage/icloud-backup/build/linux-6.1`
- Root access for sbuild setup

## Step-by-Step Build Process

### Step 1: Upload Build Scripts to Server

```bash
# From your local machine
scp *.sh bx.ee:/storage/icloud-backup/build/scripts/
```

### Step 2: Setup Build Environment (One-Time)

```bash
# SSH to server
ssh bx.ee

# Navigate to build directory
cd /storage/icloud-backup/build/scripts

# Make scripts executable
chmod +x *.sh

# Setup sbuild (requires root)
sudo ./setup-xsc-sbuild.sh
```

This will:
- Install sbuild, schroot, and dependencies
- Create a Debian Bookworm chroot configured for XSC
- Install the XSC toolchain in the chroot
- Configure build environment for 80-core parallelism

**Time:** ~15-20 minutes

### Step 3: Generate Package Lists

```bash
# Generate the 4-stage build lists
./generate-stage-lists.sh
```

This will:
- Download Debian package metadata
- Extract DVD-1 package list from jigdo
- Create stage1-4 package lists (~2,600 packages total)
- Save dependency-ordered build lists

**Time:** ~5 minutes

Output files:
- `stage1-packages.txt` - 30 packages (build-essential)
- `stage2-packages.txt` - 160 packages (essential+required)
- `stage3-packages.txt` - 800 packages (important+standard)
- `stage4-packages.txt` - 1,640 packages (optional DVD-1)

### Step 4: Start the Build

```bash
# Start master build process (runs in background)
nohup ./xsc-master-builder.sh > build.log 2>&1 &

# Get process ID
echo $! > build.pid
```

**Time:** 60-120 hours (2.5-5 days)

The build runs autonomously through all 4 stages:
- **Stage 1** (30 pkg):  Sequential build, ~30 minutes
- **Stage 2** (160 pkg): 20 parallel jobs, ~4 hours
- **Stage 3** (800 pkg): 40 parallel jobs, ~20 hours
- **Stage 4** (1,640 pkg): 80 parallel jobs, ~35-100 hours

### Step 5: Monitor Progress

```bash
# Real-time monitoring dashboard
./monitor-xsc-build.sh

# Or check logs
tail -f /storage/icloud-backup/build/xsc-debian-full/logs/master-build.log

# Or check build report
./xsc-master-builder.sh report
```

### Step 6: Handle Failures (if needed)

```bash
# Retry all failed builds
./xsc-master-builder.sh retry

# Check failed packages
cat /storage/icloud-backup/build/xsc-debian-full/failed/*.txt
```

### Step 7: Build the ISO

```bash
# After all packages are built
./build-xsc-iso.sh
```

This will:
- Copy all .deb packages to ISO structure
- Generate repository metadata
- Install XSC kernel and modules
- Create initramfs
- Configure bootloader (BIOS + UEFI)
- Generate ISO image

**Time:** ~30 minutes

Output: `/storage/icloud-backup/build/iso-output/xsc-debian-12.8-dvd1-amd64.iso`

### Step 8: Test the ISO

```bash
# Test in QEMU
qemu-system-x86_64 \
    -m 4096 \
    -smp 4 \
    -cdrom /storage/icloud-backup/build/iso-output/xsc-debian-12.8-dvd1-amd64.iso \
    -boot d \
    -serial stdio
```

## Build Stages Explained

### Stage 1: Bootstrap Toolchain (30 packages)
**Parallelism:** Sequential (1 job)
**Time:** 30 minutes

Builds the core compilation tools needed for all other packages:
- gcc-13, g++-13, binutils
- make, autoconf, automake
- dpkg-dev, debhelper
- perl, gettext

### Stage 2: Essential System (160 packages)
**Parallelism:** 20 concurrent builds
**Time:** 4 hours

Builds packages with Priority: essential or required:
- bash, coreutils, util-linux
- libc6, libgcc, libstdc++
- systemd, dpkg, apt
- tar, gzip, xz-utils

These create a minimal bootable system.

### Stage 3: Base System (800 packages)
**Parallelism:** 40 concurrent builds
**Time:** 20 hours

Builds packages with Priority: important or standard:
- vim, nano, openssh
- build-essential, gcc, make
- python3, perl, ruby
- network tools, filesystems

These create a complete base system.

### Stage 4: Applications (1,640 packages)
**Parallelism:** 80 concurrent builds (full server utilization)
**Time:** 35-100 hours

Builds most popular optional packages from DVD-1:
- Desktop environments
- Web servers (apache2, nginx)
- Databases (postgresql, mysql)
- Development tools
- Libraries and -dev packages

## Resource Requirements

### Disk Space
- Source packages: 15 GB
- Build directories: 80 GB (temporary)
- Built .deb files: 12 GB
- ISO image: 4.7 GB
- Logs: 5 GB
- **Total: 120 GB minimum, 200 GB recommended**

### Network
- Initial downloads: ~20 GB
- Recommend: Pre-download sources to avoid network bottlenecks

### Time Estimates

| Scenario | Time | Notes |
|----------|------|-------|
| Optimistic | 48 hours | Perfect parallelism, no failures |
| Realistic | 60-72 hours | Some build failures, retries |
| Pessimistic | 96-120 hours | Many failures, dependency issues |

**Plan for:** 4-5 days of continuous building

## Troubleshooting

### Build Fails Immediately
```bash
# Check sbuild is configured
schroot -l | grep xsc-amd64

# Test chroot
schroot -c source:bookworm-xsc-amd64 -u root -- /bin/bash

# Verify toolchain
ls -l /storage/icloud-backup/build/xsc-toolchain-x86_64-base/bin/
```

### Package Build Fails
```bash
# Check logs
less /storage/icloud-backup/build/xsc-debian-full/logs/<package>-build.log

# Check for missing dependencies
grep "Unmet build dependencies" /storage/icloud-backup/build/xsc-debian-full/logs/*.log

# Manually build a package
cd /storage/icloud-backup/build/xsc-debian-full
apt-get source <package>
sbuild --host=xsc-amd64 <package>_*.dsc
```

### Out of Disk Space
```bash
# Clean up build directories
find /storage/icloud-backup/build/xsc-debian-full/sources -name '*.build' -type d -exec rm -rf {} +

# Remove old logs
find /storage/icloud-backup/build/xsc-debian-full/logs -name '*.log' -mtime +7 -delete
```

### Build Too Slow
```bash
# Reduce parallelism to avoid memory pressure
export DEB_BUILD_OPTIONS="parallel=40 nocheck"

# Or build only specific stages
./xsc-master-builder.sh stage1
./xsc-master-builder.sh stage2
# etc.
```

## Advanced Usage

### Incremental Builds
```bash
# Only build packages that changed
./xsc-master-builder.sh incremental
```

### Build Only Essential Packages
```bash
# For testing, build only stages 1-2
./xsc-master-builder.sh stage1
./xsc-master-builder.sh stage2
./build-xsc-iso.sh
```

This creates a ~400MB minimal ISO with just essential packages.

### Custom Package List
```bash
# Edit stage lists to include/exclude packages
nano /storage/icloud-backup/build/xsc-debian-full/stage4-packages.txt

# Then rebuild
./xsc-master-builder.sh stage4
```

### Parallel Server Builds
```bash
# Build on multiple servers simultaneously
# Server 1: Stages 1-2
./xsc-master-builder.sh stage1 && ./xsc-master-builder.sh stage2

# Server 2: Stage 3
./xsc-master-builder.sh stage3

# Server 3: Stage 4
./xsc-master-builder.sh stage4

# Then combine results and build ISO
```

## Quality Assurance

### Verify XSC Compliance
```bash
# Check for syscall instructions (should find none)
./verify-no-syscalls.sh
```

### Test Package Installation
```bash
# Create test chroot
debootstrap --arch=xsc-amd64 bookworm /tmp/test \
    file:///storage/icloud-backup/build/xsc-iso-build

# Install a package
chroot /tmp/test apt-get install vim

# Test binary
chroot /tmp/test vim --version
```

### Verify Repository
```bash
cd /storage/icloud-backup/build/xsc-iso-build
apt-ftparchive check dists/bookworm/Release
```

## Success Criteria

### Minimum Viable Product
- [ ] Stages 1-2 complete (~190 packages)
- [ ] ISO boots to shell
- [ ] Basic commands work (ls, cat, grep)
- [ ] No syscall instructions found

### Full Success
- [ ] All 2,600 packages built
- [ ] ISO boots with installer
- [ ] All packages installable
- [ ] XSC kernel loads
- [ ] System fully functional

## Getting Help

### Check Status
```bash
./xsc-master-builder.sh report
```

### View Recent Logs
```bash
tail -100 /storage/icloud-backup/build/xsc-debian-full/logs/master-build.log
```

### List Failed Packages
```bash
cat /storage/icloud-backup/build/xsc-debian-full/failed/*.txt
```

## File Locations Reference

```
/storage/icloud-backup/build/
├── scripts/                          # Build scripts
│   ├── setup-xsc-sbuild.sh
│   ├── generate-stage-lists.sh
│   ├── xsc-master-builder.sh
│   ├── build-xsc-iso.sh
│   └── monitor-xsc-build.sh
│
├── xsc-debian-full/                  # Build directory
│   ├── stage1-packages.txt
│   ├── stage2-packages.txt
│   ├── stage3-packages.txt
│   ├── stage4-packages.txt
│   ├── sources/                      # Downloaded .dsc and .tar.gz
│   ├── results/
│   │   ├── stage1/                   # Built .deb files
│   │   ├── stage2/
│   │   ├── stage3/
│   │   └── stage4/
│   ├── logs/                         # Build logs
│   ├── completed/                    # Completed package lists
│   └── failed/                       # Failed package lists
│
├── xsc-iso-build/                    # ISO staging directory
│   ├── pool/main/                    # All .deb packages
│   ├── dists/bookworm/               # Repository metadata
│   └── boot/                         # Kernel and bootloader
│
└── iso-output/
    └── xsc-debian-12.8-dvd1-amd64.iso  # Final ISO
```

---

**Last Updated:** 2025-10-14
**Build System:** bx.ee (80 cores, 128GB RAM)
**Target:** Debian 12.8 Bookworm (XSC architecture)
