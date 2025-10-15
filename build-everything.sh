#!/bin/bash
# XSC Master Build - Build entire operating system
set -e

echo "========================================"
echo "XSC COMPLETE OS BUILD"
echo "========================================"
echo ""
echo "This builds the complete XSC operating system:"
echo "  - 4 toolchain variants (x86_64 + aarch64, base + cfi-compat)"
echo "  - ~500 packages per variant"
echo "  - Debian and AlmaLinux repositories"
echo "  - All installation ISOs"
echo ""
echo "Build time: 48-72 hours on 80 cores"
echo "Disk space: ~500GB"
echo ""

# Start toolchain builds in parallel on bx.ee
echo "========================================"
echo "PHASE 1: Building All Toolchains"
echo "========================================"

ssh bx.ee 'bash -s' << 'EOF_TOOLCHAINS'
set -e
cd /storage/icloud-backup/build

# Build all 4 toolchain variants in parallel
export MAKEFLAGS="-j20"  # 20 cores each = 80 total

build_toolchain() {
    ARCH=$1
    VARIANT=$2

    echo ""
    echo "=== Starting $ARCH $VARIANT toolchain ==="

    # Source the build configuration
    export XSC_ARCH=$ARCH
    export XSC_VARIANT=$VARIANT
    source /storage/icloud-backup/build/xsc-build-config.sh

    # Run toolchain build
    bash /storage/icloud-backup/build/build-xsc-toolchain.sh > /tmp/toolchain-$ARCH-$VARIANT.log 2>&1

    echo "=== Finished $ARCH $VARIANT toolchain ==="
}

# Launch all builds in parallel
build_toolchain x86_64 base &
PID_X64_BASE=$!

build_toolchain x86_64 cfi-compat &
PID_X64_CFI=$!

build_toolchain aarch64 base &
PID_ARM_BASE=$!

build_toolchain aarch64 cfi-compat &
PID_ARM_CFI=$!

# Wait for all toolchains
echo "Waiting for all toolchains to complete..."
wait $PID_X64_BASE && echo "✓ x86_64 base complete" || echo "✗ x86_64 base FAILED"
wait $PID_X64_CFI && echo "✓ x86_64 cfi-compat complete" || echo "✗ x86_64 cfi-compat FAILED"
wait $PID_ARM_BASE && echo "✓ aarch64 base complete" || echo "✗ aarch64 base FAILED"
wait $PID_ARM_CFI && echo "✓ aarch64 cfi-compat complete" || echo "✗ aarch64 cfi-compat FAILED"

echo ""
echo "All toolchains complete!"
EOF_TOOLCHAINS

echo ""
echo "========================================"
echo "PHASE 2: Building Package Lists"
echo "========================================"
./generate-package-lists.sh

echo ""
echo "========================================"
echo "PHASE 3: Building Core Packages"
echo "========================================"
./build-core-packages.sh

echo ""
echo "========================================"
echo "PHASE 4: Building Full Package Set"
echo "========================================"
./build-all-packages.sh

echo ""
echo "========================================"
echo "PHASE 5: Creating Repositories"
echo "========================================"
./create-repositories.sh

echo ""
echo "========================================"
echo "PHASE 6: Building ISOs"
echo "========================================"
./build-all-isos.sh

echo ""
echo "========================================"
echo "BUILD COMPLETE"
echo "========================================"
echo ""
echo "Toolchains:"
echo "  /storage/icloud-backup/build/xsc-toolchain-x86_64-base/"
echo "  /storage/icloud-backup/build/xsc-toolchain-x86_64-cfi-compat/"
echo "  /storage/icloud-backup/build/xsc-toolchain-aarch64-base/"
echo "  /storage/icloud-backup/build/xsc-toolchain-aarch64-cfi-compat/"
echo ""
echo "Repositories:"
echo "  http://bx.ee/repos/xsc/"
echo ""
echo "ISOs:"
echo "  /storage/icloud-backup/build/iso/"
echo ""
