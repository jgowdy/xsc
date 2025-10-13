#!/bin/bash
set -e
export TMPDIR=/storage/icloud-backup/build/tmp

echo '=== Building AlmaLinux Net Install ISO with XSC ==='

docker run --rm \
    --cpus=80 \
    -v /storage/icloud-backup/build:/build \
    -w /build \
    almalinux:9 bash -c '
dnf install -y genisoimage syslinux squashfs-tools createrepo_c

mkdir -p alma-netinstall/rootfs alma-netinstall/iso/images/pxeboot iso

# Minimal rootfs
echo "Creating minimal AlmaLinux rootfs..."
dnf --installroot=/build/alma-netinstall/rootfs \
    --releasever=9 \
    --setopt=install_weak_deps=False \
    --setopt=max_parallel_downloads=20 \
    -y install basesystem dnf

# Copy XSC kernel
if [ -f linux-6.1/arch/x86/boot/bzImage ]; then
    cp linux-6.1/arch/x86/boot/bzImage alma-netinstall/iso/images/pxeboot/vmlinuz
fi

# Configure XSC architecture
mkdir -p alma-netinstall/rootfs/etc/xsc
echo "x86_64-xsc-linux-gnu" > alma-netinstall/rootfs/etc/xsc/triplet

mkdir -p alma-netinstall/rootfs/etc/rpm
cat > alma-netinstall/rootfs/etc/rpm/macros.xsc << "EOF"
%_arch x86_64-xsc
%_target_cpu x86_64
EOF

# Network repos enabled
cat > alma-netinstall/rootfs/etc/yum.repos.d/alma.repo << "EOF"
[baseos]
name=AlmaLinux 9 BaseOS
baseurl=https://repo.almalinux.org/almalinux/9/BaseOS/x86_64/os/
enabled=1
gpgcheck=0
EOF

# Create initrd
dracut --force alma-netinstall/iso/images/pxeboot/initrd.img || \
    cp /boot/initramfs-* alma-netinstall/iso/images/pxeboot/initrd.img 2>/dev/null || true

# Squashfs
mkdir -p alma-netinstall/iso/LiveOS
mksquashfs alma-netinstall/rootfs alma-netinstall/iso/LiveOS/squashfs.img -comp xz -processors 80

# Bootloader
mkdir -p alma-netinstall/iso/isolinux
cp /usr/share/syslinux/isolinux.bin alma-netinstall/iso/isolinux/
cp /usr/share/syslinux/*.c32 alma-netinstall/iso/isolinux/ 2>/dev/null || true

cat > alma-netinstall/iso/isolinux/isolinux.cfg << "EOF"
DEFAULT linux
TIMEOUT 30
LABEL linux
  KERNEL /images/pxeboot/vmlinuz
  APPEND initrd=/images/pxeboot/initrd.img
EOF

# Generate ISO
genisoimage -r -J -joliet-long -l \
    -V "XSC AlmaLinux NetInstall" \
    -b isolinux/isolinux.bin \
    -c isolinux/boot.cat \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o iso/xsc-almalinux-netinstall.iso \
    alma-netinstall/iso/

ls -lh iso/xsc-almalinux-netinstall.iso
'

echo "AlmaLinux Net Install ISO created"
ls -lh /storage/icloud-backup/build/iso/xsc-almalinux-netinstall.iso
