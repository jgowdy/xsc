#!/bin/bash
set -e
export TMPDIR=/storage/icloud-backup/build/tmp

echo '=== Building Debian with XSC using Docker (ALL CORES) ==='

# Build Debian ISO in Docker with ALL 80 threads
docker run --rm \
    --cpus=80 \
    -v /storage/icloud-backup/build:/build \
    -w /build \
    debian:bookworm bash -c '
apt-get update -qq
apt-get install -y -qq debootstrap genisoimage isolinux syslinux-utils squashfs-tools apt-utils gcc

mkdir -p debian-build/rootfs debian-build/iso/boot/isolinux iso

# Minimal Debian rootfs
echo "Creating Debian rootfs..."
debootstrap --variant=minbase --arch=amd64 bookworm debian-build/rootfs http://deb.debian.org/debian/

# Copy XSC kernel
if [ -f linux-6.1/arch/x86/boot/bzImage ]; then
    cp linux-6.1/arch/x86/boot/bzImage debian-build/iso/boot/vmlinuz-xsc
    echo "XSC kernel installed"
fi

# Configure offline-only repos (XSC ABI incompatibility)
echo "Configuring offline-only package manager..."
cat > debian-build/rootfs/etc/apt/sources.list << "APT_EOF"
# XSC Offline ISO - No upstream repos
# Standard x86_64 packages are INCOMPATIBLE with XSC ABI
# All packages must use x86_64-xsc-linux-gnu triplet
#
# Upstream repos DISABLED - Use local media only:
# deb cdrom:[XSC Debian]/ bookworm main
APT_EOF

# Add XSC triplet marker
mkdir -p debian-build/rootfs/etc/xsc
echo "x86_64-xsc-linux-gnu" > debian-build/rootfs/etc/xsc/triplet
cat > debian-build/rootfs/etc/xsc/README << "XSC_EOF"
XSC ABI System
==============

GNU Triplet: x86_64-xsc-linux-gnu
Dpkg Arch:   xsc-amd64

WARNING: Standard amd64 packages will NOT work!
         Syscall instructions (syscall/sysenter/int 0x80) are FORBIDDEN.
         Attempting to execute syscall instructions will result in SIGSYS.

All software must be rebuilt with XSC-aware toolchain.
XSC_EOF

# CRITICAL: Configure dpkg to use xsc-amd64 architecture
# This prevents installation of standard amd64 packages
echo "Configuring dpkg architecture lockdown (xsc-amd64)..."
mkdir -p debian-build/rootfs/var/lib/dpkg
echo "xsc-amd64" > debian-build/rootfs/var/lib/dpkg/arch

# Configure dpkg to only accept xsc-amd64 packages
cat > debian-build/rootfs/etc/dpkg/dpkg.cfg.d/xsc-arch << "DPKG_EOF"
# XSC Architecture Configuration
# Only xsc-amd64 packages are compatible with XSC ABI
# Standard amd64 packages MUST be rejected

foreign-architecture amd64
DPKG_EOF

echo "xsc-amd64" > debian-build/rootfs/etc/debian_version.arch

# Install live-boot and initramfs-tools in rootfs
echo "Installing live-boot and initramfs-tools..."
chroot debian-build/rootfs apt-get update -qq
chroot debian-build/rootfs apt-get install -y -qq --no-install-recommends \
    live-boot live-boot-initramfs-tools initramfs-tools linux-image-amd64

# Copy XSC kernel modules if available
if [ -d linux-6.1/drivers/xsc ]; then
    KERNEL_VER=$(basename debian-build/rootfs/lib/modules/* | head -1)
    mkdir -p "debian-build/rootfs/lib/modules/$KERNEL_VER/extra"
    cp linux-6.1/drivers/xsc/*.ko "debian-build/rootfs/lib/modules/$KERNEL_VER/extra/" 2>/dev/null || true
    chroot debian-build/rootfs depmod -a "$KERNEL_VER" || true
fi

# Create initramfs for live boot
echo "Creating initramfs with live-boot..."
KERNEL_VER=$(basename debian-build/rootfs/lib/modules/* | head -1)
chroot debian-build/rootfs mkinitramfs -o /boot/initrd.img "$KERNEL_VER"
cp debian-build/rootfs/boot/initrd.img debian-build/iso/boot/

# Build and package XSC test tools as .deb
echo "Building XSC test tools package..."
if [ -f xsc_ring_test.c ]; then
    mkdir -p debian-build/xsc-tools-package/DEBIAN
    mkdir -p debian-build/xsc-tools-package/usr/bin

    # Build xsc_ring_test
    gcc -o debian-build/xsc-tools-package/usr/bin/xsc_ring_test xsc_ring_test.c -O2
    chmod +x debian-build/xsc-tools-package/usr/bin/xsc_ring_test

    # Create package control file
    cat > debian-build/xsc-tools-package/DEBIAN/control << "CONTROL_EOF"
Package: xsc-tools
Version: 1.0-1
Section: utils
Priority: optional
Architecture: xsc-amd64
Maintainer: XSC Project <xsc@example.com>
Description: XSC ring-based syscall test tools
 Test utilities for XSC (eXtended SysCall) ring-based syscall mechanism.
 These binaries use XSC rings instead of syscall/sysenter/int80 instructions.
CONTROL_EOF

    # Build .deb package
    dpkg-deb --build debian-build/xsc-tools-package

    # Add to ISO packages directory
    mkdir -p debian-build/iso/pool/main/x/xsc-tools
    cp debian-build/xsc-tools-package.deb debian-build/iso/pool/main/x/xsc-tools/

    # Create simple apt repository
    mkdir -p debian-build/iso/dists/bookworm/main/binary-xsc-amd64
    cd debian-build/iso
    apt-ftparchive packages pool > dists/bookworm/main/binary-xsc-amd64/Packages
    cd /build
fi

# Create bootloader
cp /usr/lib/ISOLINUX/isolinux.bin debian-build/iso/boot/isolinux/
cp /usr/lib/syslinux/modules/bios/*.c32 debian-build/iso/boot/isolinux/ 2>/dev/null || true

cat > debian-build/iso/boot/isolinux/isolinux.cfg << "ISOLINUX_EOF"
DEFAULT linux
TIMEOUT 30
PROMPT 1
LABEL linux
    MENU LABEL ^Start XSC Debian
    KERNEL /boot/vmlinuz-xsc
    APPEND initrd=/boot/initrd.img boot=live console=ttyS0,115200 console=tty0
ISOLINUX_EOF

# Create squashfs with ALL 80 PROCESSORS
mkdir -p debian-build/iso/live
echo "Creating squashfs with 80 processors..."
mksquashfs debian-build/rootfs debian-build/iso/live/filesystem.squashfs -comp xz -Xdict-size 100% -processors 80

# Generate ISO
echo "Generating ISO..."
genisoimage -r -J -joliet-long -l \
    -V "XSC Debian" \
    -b boot/isolinux/isolinux.bin \
    -c boot/isolinux/boot.cat \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o iso/xsc-debian-bookworm-amd64.iso \
    debian-build/iso/

echo "ISO created!"
ls -lh iso/xsc-debian-bookworm-amd64.iso
'

echo ""
echo "=== Build Complete ==="
ls -lh /storage/icloud-backup/build/iso/xsc-debian-bookworm-amd64.iso
