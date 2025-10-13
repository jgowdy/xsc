#!/bin/bash
set -e
export TMPDIR=/storage/icloud-backup/build/tmp

echo '=== Building Debian Net Install ISO with XSC ==='

docker run --rm \
    --cpus=80 \
    -v /storage/icloud-backup/build:/build \
    -w /build \
    debian:bookworm bash -c '
apt-get update -qq
apt-get install -y -qq debootstrap genisoimage isolinux syslinux-utils squashfs-tools

mkdir -p debian-netinstall/rootfs debian-netinstall/iso/boot/isolinux iso

# Minimal rootfs - no extra packages
echo "Creating minimal Debian rootfs..."
debootstrap --variant=minbase --arch=amd64 bookworm debian-netinstall/rootfs http://deb.debian.org/debian/

# Copy XSC kernel
if [ -f linux-6.1/arch/x86/boot/bzImage ]; then
    cp linux-6.1/arch/x86/boot/bzImage debian-netinstall/iso/boot/vmlinuz-xsc
fi

# Configure XSC architecture
mkdir -p debian-netinstall/rootfs/etc/xsc
echo "x86_64-xsc-linux-gnu" > debian-netinstall/rootfs/etc/xsc/triplet

echo "xsc-amd64" > debian-netinstall/rootfs/var/lib/dpkg/arch

cat > debian-netinstall/rootfs/etc/apt/sources.list << "EOF"
# XSC Net Install - Network repos enabled
deb http://deb.debian.org/debian/ bookworm main
deb http://security.debian.org/debian-security bookworm-security main
EOF

# Create initramfs
KERNEL_VER=$(basename debian-netinstall/rootfs/lib/modules/* | head -1)
chroot debian-netinstall/rootfs apt-get install -y -qq initramfs-tools
chroot debian-netinstall/rootfs mkinitramfs -o /boot/initrd.img "$KERNEL_VER"
cp debian-netinstall/rootfs/boot/initrd.img debian-netinstall/iso/boot/

# Bootloader
cp /usr/lib/ISOLINUX/isolinux.bin debian-netinstall/iso/boot/isolinux/
cp /usr/lib/syslinux/modules/bios/*.c32 debian-netinstall/iso/boot/isolinux/ 2>/dev/null || true

cat > debian-netinstall/iso/boot/isolinux/isolinux.cfg << "EOF"
DEFAULT linux
TIMEOUT 30
LABEL linux
    KERNEL /boot/vmlinuz-xsc
    APPEND initrd=/boot/initrd.img
EOF

# Squashfs
mkdir -p debian-netinstall/iso/live
mksquashfs debian-netinstall/rootfs debian-netinstall/iso/live/filesystem.squashfs -comp xz -processors 80

# Generate ISO
genisoimage -r -J -joliet-long -l \
    -V "XSC Debian NetInstall" \
    -b boot/isolinux/isolinux.bin \
    -c boot/isolinux/boot.cat \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o iso/xsc-debian-netinstall.iso \
    debian-netinstall/iso/

ls -lh iso/xsc-debian-netinstall.iso
'

echo "Debian Net Install ISO created"
ls -lh /storage/icloud-backup/build/iso/xsc-debian-netinstall.iso
