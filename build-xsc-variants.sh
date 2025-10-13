#!/bin/bash
# Build both base and hardened XSC variants
set -e

cd /Users/jgowdy/flexsc

ARCH="${1:-x86_64}"

echo "=== Building XSC Variants for $ARCH ==="
echo ""

# Build base variant
echo "1/2 Building Base Variant..."
export XSC_VARIANT=base
export XSC_ARCH="$ARCH"
./build-xsc-toolchain.sh

echo ""
echo "2/2 Building Hardened Variant..."
export XSC_VARIANT=hardened
export XSC_ARCH="$ARCH"
./build-xsc-toolchain.sh

echo ""
echo "=== Both Variants Built ==="
echo "Base: /storage/icloud-backup/build/xsc-toolchain-$ARCH-base/"
echo "Hardened: /storage/icloud-backup/build/xsc-toolchain-$ARCH-hardened/"
