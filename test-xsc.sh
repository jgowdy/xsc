#!/bin/bash
# XSC Quick Test - Minimal fast boot
set -e

cd /storage/icloud-backup/build

echo "=== XSC Quick Test ==="

# Build static test binary (if needed)
if [ ! -f initramfs/bin/xsc_ring_test ] || [ xsc_ring_test.c -nt initramfs/bin/xsc_ring_test ]; then
    echo "Building static test binary..."
    TMPDIR=/storage/icloud-backup/build/tmp gcc -static -o initramfs/bin/xsc_ring_test xsc_ring_test.c -O2
fi

# Rebuild initramfs (only if changed)
echo "Packing initramfs..."
cd initramfs && find . 2>/dev/null | cpio -o -H newc 2>/dev/null | gzip > ../initramfs.cpio.gz

# Boot XSC kernel
cd /storage/icloud-backup/build
echo "Booting XSC kernel..."
timeout 30 qemu-system-x86_64 \
  -kernel linux-6.1/arch/x86/boot/bzImage \
  -initrd initramfs.cpio.gz \
  -append 'console=ttyS0 rdinit=/init' \
  -m 2G \
  -smp 4 \
  -enable-kvm \
  -nographic
