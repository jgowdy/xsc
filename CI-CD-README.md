# XSC CI/CD Build System

Complete automated build pipeline for XSC-enabled Linux distributions.

## Overview

This CI/CD system builds bootable ISO images of Debian, AlmaLinux, and Rocky Linux with the XSC (eXtended Syscall) kernel and glibc modifications.

**Location:** `/storage/icloud-backup/build/`

## Architecture

```
/storage/icloud-backup/build/
├── Makefile                    # Main build orchestration
├── xsc-cicd-build.sh          # Build pipeline script
├── linux-6.1/                  # Kernel source with XSC driver
├── patches/
│   ├── kernel/                 # Quilt patch series for kernel
│   └── glibc/                  # Quilt patch series for glibc
├── debian-build/               # Debian build artifacts
├── alma-build/                 # AlmaLinux build artifacts
├── rocky-build/                # Rocky Linux build artifacts
├── iso/                        # Output ISOs
│   ├── debian/
│   ├── alma/
│   └── rocky/
└── logs/                       # Build logs
```

## Quick Start

### Build Everything

```bash
cd /storage/icloud-backup/build
make all
```

### Build Specific Distro

```bash
# Build XSC kernel
make kernel

# Build Debian ISO
make debian

# Build AlmaLinux ISO
make alma

# Build Rocky Linux ISO
make rocky

# Build all ISOs
make isos
```

### Check Status

```bash
make status
```

## Build Targets

| Target | Description |
|--------|-------------|
| `all` | Build kernel and Debian ISO (default) |
| `kernel` | Build XSC-enabled Linux kernel |
| `glibc` | Build XSC-enabled glibc |
| `debian` | Build Debian ISO with XSC |
| `alma` | Build AlmaLinux ISO with XSC |
| `rocky` | Build Rocky Linux ISO with XSC |
| `isos` | Build all three ISOs |
| `patches` | Generate quilt patch series |
| `clean` | Clean build artifacts |
| `distclean` | Deep clean including kernel |
| `status` | Show build status |
| `help` | Show available targets |

## Patch Management with Quilt

XSC uses quilt for managing patches to keep diffstats minimal and changes reviewable.

### Kernel Patches

Located in `patches/kernel/series`:

```
0001-xsc-add-kconfig-and-makefile.patch
0002-xsc-add-uapi-headers.patch
0003-xsc-add-core-driver.patch
0004-xsc-add-filesystem-operations.patch
0005-xsc-add-network-operations.patch
0006-xsc-add-sync-operations.patch
0007-xsc-add-timer-operations.patch
0008-xsc-add-exec-operations.patch
```

### Applying Patches

```bash
cd /storage/icloud-backup/build/linux-6.1
quilt push -a  # Apply all patches
quilt pop -a   # Unapply all patches
```

### Creating New Patches

```bash
quilt new 0009-xsc-new-feature.patch
quilt add drivers/xsc/file.c
# Make your changes
quilt refresh
```

## Build Process

### Stage 1: Kernel Build

1. Apply quilt patches to Linux 6.1 source
2. Configure with `CONFIG_XSC=y`
3. Build bzImage and modules with `-j$(nproc)`
4. Output: `linux-6.1/arch/x86/boot/bzImage`

### Stage 2: Glibc Build

1. Apply XSC sysdeps patches
2. Configure with `--enable-xsc`
3. Build glibc with XSC support
4. Output: glibc packages for each distro

### Stage 3: Debian Build

1. Bootstrap Debian bookworm with debootstrap
2. Install XSC kernel and modules
3. Install XSC glibc
4. Configure system (initramfs, bootloader)
5. Create bootable ISO with genisoimage
6. Output: `iso/debian/xsc-debian-amd64.iso`

### Stage 4: AlmaLinux Build

1. Bootstrap AlmaLinux with mock
2. Install XSC kernel RPM
3. Install XSC glibc RPM
4. Create kickstart configuration
5. Build ISO with lorax
6. Output: `iso/alma/xsc-alma-amd64.iso`

### Stage 5: Rocky Linux Build

Similar to AlmaLinux, using Rocky base packages.

## CI/CD Integration

### GitHub Actions (Planned)

```yaml
name: XSC Build
on: [push, pull_request]
jobs:
  build:
    runs-on: self-hosted
    steps:
      - uses: actions/checkout@v3
      - name: Build All
        run: cd /storage/icloud-backup/build && make all
      - uses: actions/upload-artifact@v3
        with:
          name: xsc-isos
          path: /storage/icloud-backup/build/iso/*/*.iso
```

### GitLab CI (Planned)

```yaml
stages:
  - kernel
  - distros

kernel:
  stage: kernel
  script:
    - cd /storage/icloud-backup/build
    - make kernel

debian:
  stage: distros
  script:
    - cd /storage/icloud-backup/build
    - make debian
  artifacts:
    paths:
      - iso/debian/*.iso
```

## Dependencies

### Debian Build

```bash
apt-get install -y \
    debootstrap \
    genisoimage \
    isolinux \
    syslinux-common \
    quilt \
    build-essential \
    bc \
    bison \
    flex \
    libelf-dev \
    libssl-dev
```

### AlmaLinux/Rocky Build

```bash
apt-get install -y \
    mock \
    lorax \
    anaconda \
    rpm-build
```

## Configuration

### Build Variables

Edit `Makefile` or set environment variables:

```bash
export TMPDIR=/storage/icloud-backup/build/tmp
export KERNEL_VERSION=6.1.0
export GLIBC_VERSION=2.36
```

### Kernel Configuration

XSC kernel config options:

```
CONFIG_XSC=y                    # Enable XSC driver
CONFIG_XSC_TRAP_GUARD=y         # Enable syscall trap guard
CONFIG_XSC_DEBUG=y              # Enable debug logging
```

## Testing

### Test Built Kernel

```bash
cd /storage/icloud-backup/build
qemu-system-x86_64 \
    -kernel linux-6.1/arch/x86/boot/bzImage \
    -append "console=ttyS0" \
    -m 2G \
    -nographic
```

### Test ISO

```bash
qemu-system-x86_64 \
    -cdrom iso/debian/xsc-debian-amd64.iso \
    -m 4G \
    -boot d
```

### Test XSC Ring Mechanism

```bash
# Boot into XSC system
# Run test program
./xsc_ring_test
```

## Troubleshooting

### /tmp Full Error

The build system uses `/storage/icloud-backup/build/tmp`:

```bash
export TMPDIR=/storage/icloud-backup/build/tmp
mkdir -p $TMPDIR
```

### Kernel Build Fails

Check logs:

```bash
tail -f /storage/icloud-backup/build/logs/kernel-build-*.log
```

### ISO Build Fails

Ensure dependencies installed:

```bash
apt-get install debootstrap genisoimage isolinux
```

## Performance

### Build Times (40-core, 80-thread, 256GB RAM)

- Kernel: ~3-5 minutes
- Glibc: ~10-15 minutes
- Debian ISO: ~20-30 minutes
- AlmaLinux ISO: ~30-40 minutes
- Full build (all ISOs): ~60-90 minutes

### Parallel Builds

The Makefile uses `-j$(nproc)` automatically.

## Output Artifacts

After successful build:

```
iso/
├── debian/
│   └── xsc-debian-amd64.iso        # Debian with XSC (~2-3GB)
├── alma/
│   └── xsc-alma-amd64.iso          # AlmaLinux with XSC (~2-3GB)
└── rocky/
    └── xsc-rocky-amd64.iso         # Rocky with XSC (~2-3GB)
```

## Next Steps

1. **Test ISOs in QEMU/KVM**
2. **Set up automated testing**
3. **Implement glibc patches**
4. **Add AlmaLinux/Rocky mock configs**
5. **Integrate with CI/CD platform**
6. **Add checksums and signatures**
7. **Create release automation**

## Support

- **Build location:** `bx.ee:/storage/icloud-backup/build/`
- **Logs:** `/storage/icloud-backup/build/logs/`
- **Status:** `make status`

---

*XSC CI/CD Build System - Automated Distro Builds with Ring-Based Syscalls*
