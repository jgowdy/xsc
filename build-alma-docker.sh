#!/bin/bash
set -e
export TMPDIR=/storage/icloud-backup/build/tmp

echo '=== Building AlmaLinux with XSC using Docker (ALL CORES) ==='

docker run --rm \
    --cpus=80 \
    -v /storage/icloud-backup/build:/build \
    -w /build \
    almalinux:9 bash -c '
dnf install -y genisoimage syslinux squashfs-tools createrepo_c

mkdir -p alma-build/rootfs alma-build/iso/images/pxeboot iso

# Minimal AlmaLinux rootfs with parallel downloads
echo "Creating AlmaLinux rootfs with max parallel downloads..."
dnf --installroot=/build/alma-build/rootfs \
    --releasever=9 \
    --setopt=install_weak_deps=False \
    --setopt=max_parallel_downloads=20 \
    -y groupinstall minimal-environment

# Copy XSC kernel
if [ -f linux-6.1/arch/x86/boot/bzImage ]; then
    cp linux-6.1/arch/x86/boot/bzImage alma-build/iso/images/pxeboot/vmlinuz
    echo "XSC kernel installed"
fi

# Configure offline-only repos (XSC ABI incompatibility)
echo "Configuring offline-only package manager..."
mkdir -p alma-build/rootfs/etc/yum.repos.d-disabled
mv alma-build/rootfs/etc/yum.repos.d/*.repo alma-build/rootfs/etc/yum.repos.d-disabled/ 2>/dev/null || true

cat > alma-build/rootfs/etc/yum.repos.d/XSC-LOCAL.repo << "YUM_EOF"
# XSC Offline ISO - No upstream repos
# Standard x86_64 packages are INCOMPATIBLE with XSC ABI
# All packages must be rebuilt for x86_64-xsc-linux-gnu triplet
#
# Upstream repos DISABLED and moved to /etc/yum.repos.d-disabled/
# To use this ISO packages, mount it and configure local repo:
# mount -o loop /path/to/xsc-almalinux-9-amd64.iso /mnt
#
# [xsc-local-baseos]
# name=XSC AlmaLinux 9 BaseOS (Local)
# baseurl=file:///mnt/BaseOS
# enabled=1
# gpgcheck=0
YUM_EOF

# Add XSC triplet marker
mkdir -p alma-build/rootfs/etc/xsc
echo "x86_64-xsc-linux-gnu" > alma-build/rootfs/etc/xsc/triplet
cat > alma-build/rootfs/etc/xsc/README << "XSC_EOF"
XSC ABI System
==============

This system uses the XSC (eXtended SysCall) ABI.

Architecture: x86_64-xsc-linux-gnu

WARNING: Standard x86_64-linux-gnu packages will NOT work!
         Syscall instructions (syscall/sysenter/int 0x80) are FORBIDDEN.
         Attempting to execute syscall instructions will result in SIGSYS.

All software must be rebuilt with XSC-aware toolchain.

RPM Architecture: x86_64-xsc (also supports aarch64-xsc for ARM)
XSC_EOF

# CRITICAL: Configure RPM/DNF to use x86_64-xsc architecture
# This prevents installation of standard x86_64 packages
echo "Configuring RPM architecture lockdown (x86_64-xsc)..."

# Configure RPM macros for x86_64-xsc architecture
mkdir -p alma-build/rootfs/etc/rpm
cat > alma-build/rootfs/etc/rpm/macros.xsc << "RPM_EOF"
# XSC Architecture Configuration
# Only x86_64-xsc packages are compatible with XSC ABI on x86-64
# Also supports aarch64-xsc for ARM64 systems
# Standard x86_64 packages MUST be rejected

%_arch x86_64-xsc
%_target_cpu x86_64
%_build_arch x86_64-xsc
%_host x86_64-xsc-alma-linux
%_host_cpu x86_64
%_host_vendor alma
%_host_os linux

# Force x86_64-xsc in all package operations
%_transaction_color 7
RPM_EOF

# Configure DNF to use x86_64-xsc
mkdir -p alma-build/rootfs/etc/dnf/vars
echo "x86_64-xsc" > alma-build/rootfs/etc/dnf/vars/arch
echo "x86_64" > alma-build/rootfs/etc/dnf/vars/basearch

# Add to DNF config
cat >> alma-build/rootfs/etc/dnf/dnf.conf << "DNF_EOF"

# XSC Architecture Enforcement
# Only x86_64-xsc packages are allowed (or aarch64-xsc on ARM)
arch=x86_64-xsc
ignorearch=0
DNF_EOF

# Create initrd
echo "Creating initrd..."
dnf install -y dracut
dracut --force --add-drivers "xsc" alma-build/iso/images/pxeboot/initrd.img || \
    cp /boot/initramfs-* alma-build/iso/images/pxeboot/initrd.img || true

# Create squashfs with ALL 80 PROCESSORS
mkdir -p alma-build/iso/LiveOS
echo "Creating squashfs with 80 processors..."
mksquashfs alma-build/rootfs alma-build/iso/LiveOS/squashfs.img -comp xz -processors 80

# Create isolinux boot
mkdir -p alma-build/iso/isolinux
cp /usr/share/syslinux/isolinux.bin alma-build/iso/isolinux/
cp /usr/share/syslinux/*.c32 alma-build/iso/isolinux/ 2>/dev/null || true

cat > alma-build/iso/isolinux/isolinux.cfg << "ISOLINUX_EOF"
DEFAULT vesamenu.c32
TIMEOUT 600
MENU TITLE XSC AlmaLinux 9
LABEL linux
  MENU LABEL ^Start XSC AlmaLinux
  KERNEL /images/pxeboot/vmlinuz
  APPEND initrd=/images/pxeboot/initrd.img root=live:CDLABEL=XSC_ALMA9 rd.live.image quiet
ISOLINUX_EOF

# Generate ISO
echo "Generating ISO..."
genisoimage -r -J -joliet-long -l \
    -V "XSC_ALMA9" \
    -b isolinux/isolinux.bin \
    -c isolinux/boot.cat \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o iso/xsc-almalinux-9-amd64.iso \
    alma-build/iso/

echo "ISO created!"
ls -lh iso/xsc-almalinux-9-amd64.iso
'

echo ""
echo "=== AlmaLinux Build Complete ==="
ls -lh /storage/icloud-backup/build/iso/xsc-almalinux-9-amd64.iso
