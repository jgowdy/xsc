#!/bin/bash
set -e
export TMPDIR=/storage/icloud-backup/build/tmp

echo '=== Building AlmaLinux Full Archive ISO with XSC ==='

docker run --rm \
    --cpus=80 \
    -v /storage/icloud-backup/build:/build \
    -w /build \
    almalinux:9 bash -c '
dnf install -y genisoimage syslinux squashfs-tools createrepo_c

mkdir -p alma-archive/rootfs alma-archive/iso/images/pxeboot alma-archive/iso/Packages iso

# Full rootfs
echo "Creating full AlmaLinux rootfs..."
dnf --installroot=/build/alma-archive/rootfs \
    --releasever=9 \
    --setopt=install_weak_deps=False \
    --setopt=max_parallel_downloads=20 \
    -y groupinstall minimal-environment

# Copy XSC kernel
if [ -f linux-6.1/arch/x86/boot/bzImage ]; then
    cp linux-6.1/arch/x86/boot/bzImage alma-archive/iso/images/pxeboot/vmlinuz
fi

# Configure XSC architecture
mkdir -p alma-archive/rootfs/etc/xsc
echo "x86_64-xsc-linux-gnu" > alma-archive/rootfs/etc/xsc/triplet

mkdir -p alma-archive/rootfs/etc/rpm
cat > alma-archive/rootfs/etc/rpm/macros.xsc << "EOF"
%_arch x86_64-xsc
%_target_cpu x86_64
EOF

# Offline only
cat > alma-archive/rootfs/etc/yum.repos.d/XSC-LOCAL.repo << "EOF"
# XSC Archive ISO - Offline only
# Standard x86_64 packages are INCOMPATIBLE with XSC
EOF

# Download full package set
echo "Downloading full package archive..."
mkdir -p /var/cache/dnf
dnf install -y --downloadonly --downloaddir=/var/cache/dnf \
    kernel systemd NetworkManager openssh-server \
    vim nano curl wget git make gcc gcc-c++ \
    python3 perl httpd nginx \
    2>/dev/null || true

# Copy packages to ISO
cp /var/cache/dnf/*.rpm alma-archive/iso/Packages/ 2>/dev/null || true

# Create repository
cd alma-archive/iso
createrepo_c Packages
cd /build

# Create initrd
dracut --force alma-archive/iso/images/pxeboot/initrd.img || \
    cp /boot/initramfs-* alma-archive/iso/images/pxeboot/initrd.img 2>/dev/null || true

# Squashfs
mkdir -p alma-archive/iso/LiveOS
mksquashfs alma-archive/rootfs alma-archive/iso/LiveOS/squashfs.img -comp xz -processors 80

# Bootloader
mkdir -p alma-archive/iso/isolinux
cp /usr/share/syslinux/isolinux.bin alma-archive/iso/isolinux/
cp /usr/share/syslinux/*.c32 alma-archive/iso/isolinux/ 2>/dev/null || true

cat > alma-archive/iso/isolinux/isolinux.cfg << "EOF"
DEFAULT linux
TIMEOUT 30
LABEL linux
  KERNEL /images/pxeboot/vmlinuz
  APPEND initrd=/images/pxeboot/initrd.img
EOF

# Generate ISO
genisoimage -r -J -joliet-long -l \
    -V "XSC AlmaLinux Archive" \
    -b isolinux/isolinux.bin \
    -c isolinux/boot.cat \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o iso/xsc-almalinux-archive.iso \
    alma-archive/iso/

ls -lh iso/xsc-almalinux-archive.iso
'

echo "AlmaLinux Archive ISO created"
ls -lh /storage/icloud-backup/build/iso/xsc-almalinux-archive.iso
