#!/bin/bash
set -e
export TMPDIR=/storage/icloud-backup/build/tmp

echo '=== Building Debian Full Archive ISO with XSC ==='

docker run --rm \
    --cpus=80 \
    -v /storage/icloud-backup/build:/build \
    -w /build \
    debian:bookworm bash -c '
apt-get update -qq
apt-get install -y -qq debootstrap genisoimage isolinux syslinux-tools squashfs-tools apt-utils dpkg-dev

mkdir -p debian-archive/rootfs debian-archive/iso/boot/isolinux debian-archive/iso/pool iso

# Full rootfs with common packages
echo "Creating full Debian rootfs..."
debootstrap --variant=minbase --arch=amd64 bookworm debian-archive/rootfs http://deb.debian.org/debian/

# Copy XSC kernel
if [ -f linux-6.1/arch/x86/boot/bzImage ]; then
    cp linux-6.1/arch/x86/boot/bzImage debian-archive/iso/boot/vmlinuz-xsc
fi

# Configure XSC architecture
mkdir -p debian-archive/rootfs/etc/xsc
echo "x86_64-xsc-linux-gnu" > debian-archive/rootfs/etc/xsc/triplet
echo "xsc-amd64" > debian-archive/rootfs/var/lib/dpkg/arch

cat > debian-archive/rootfs/etc/apt/sources.list << "EOF"
# XSC Archive ISO - Offline only
# Standard amd64 packages are INCOMPATIBLE with XSC
# deb cdrom:[XSC Debian Archive]/ bookworm main
EOF

# Download all packages to ISO
echo "Downloading full package archive..."
mkdir -p /var/cache/apt/archives
apt-get update -qq

# Download essential package sets
apt-get install -y -qq --download-only \
    linux-image-amd64 firmware-linux build-essential \
    vim nano curl wget ssh openssh-server \
    net-tools iproute2 iptables \
    systemd-sysv udev dbus \
    python3 perl git make cmake \
    2>/dev/null || true

# Copy all downloaded packages to ISO
cp /var/cache/apt/archives/*.deb debian-archive/iso/pool/ 2>/dev/null || true

# Create package repository
cd debian-archive/iso
mkdir -p dists/bookworm/main/binary-xsc-amd64
apt-ftparchive packages pool > dists/bookworm/main/binary-xsc-amd64/Packages
gzip -k dists/bookworm/main/binary-xsc-amd64/Packages
cd /build

# Create initramfs
KERNEL_VER=$(basename debian-archive/rootfs/lib/modules/* | head -1)
chroot debian-archive/rootfs apt-get install -y -qq initramfs-tools
chroot debian-archive/rootfs mkinitramfs -o /boot/initrd.img "$KERNEL_VER"
cp debian-archive/rootfs/boot/initrd.img debian-archive/iso/boot/

# Bootloader
cp /usr/lib/ISOLINUX/isolinux.bin debian-archive/iso/boot/isolinux/
cp /usr/lib/syslinux/modules/bios/*.c32 debian-archive/iso/boot/isolinux/ 2>/dev/null || true

cat > debian-archive/iso/boot/isolinux/isolinux.cfg << "EOF"
DEFAULT linux
TIMEOUT 30
LABEL linux
    KERNEL /boot/vmlinuz-xsc
    APPEND initrd=/boot/initrd.img
EOF

# Squashfs
mkdir -p debian-archive/iso/live
mksquashfs debian-archive/rootfs debian-archive/iso/live/filesystem.squashfs -comp xz -processors 80

# Generate ISO
genisoimage -r -J -joliet-long -l \
    -V "XSC Debian Archive" \
    -b boot/isolinux/isolinux.bin \
    -c boot/isolinux/boot.cat \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o iso/xsc-debian-archive.iso \
    debian-archive/iso/

ls -lh iso/xsc-debian-archive.iso
'

echo "Debian Archive ISO created"
ls -lh /storage/icloud-backup/build/iso/xsc-debian-archive.iso
