#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="${BUILD_ROOT:-/storage/icloud-backup/build}"
INIT_BIN="${INIT_BIN:-xsc_ring_demo}"

if [ ! -f "$BUILD_ROOT/linux-6.1/arch/x86/boot/bzImage" ]; then
  echo "error: kernel image not found in $BUILD_ROOT/linux-6.1" >&2
  exit 1
fi

if [ ! -f "$SCRIPT_DIR/../samples/$INIT_BIN" ]; then
  echo "Building samples/$INIT_BIN ..."
  (cd "$SCRIPT_DIR/.." && make -C samples PREFIX=bin)
fi

mkdir -p "$BUILD_ROOT/initramfs/bin"
cp "$SCRIPT_DIR/../samples/$INIT_BIN" "$BUILD_ROOT/initramfs/bin/xsc_ring_test"

(cd "$BUILD_ROOT/initramfs" && find . | cpio -o -H newc | gzip) > "$BUILD_ROOT/initramfs.cpio.gz"

exec qemu-system-x86_64 \
  -kernel "$BUILD_ROOT/linux-6.1/arch/x86/boot/bzImage" \
  -initrd "$BUILD_ROOT/initramfs.cpio.gz" \
  -append 'console=ttyS0 rdinit=/bin/xsc_ring_test' \
  -m 2G \
  -smp 4 \
  -enable-kvm \
  -nographic
