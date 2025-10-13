#!/bin/bash
# Final XSC Build Commands - Use storage mount for temp files

set -e

echo "=== XSC Final Build Script ==="
echo "Using /storage mount for temporary files"

# Set temp directory to storage mount
export TMPDIR=/storage/icloud-backup/build/tmp
mkdir -p $TMPDIR

# 1. Build Kernel
echo "Building Linux Kernel with XSC support..."
cd ~/xsc-build/kernel/linux-6.1

# Configure
make defconfig
scripts/kconfig/merge_config.sh .config ../../xsc.config

# Build using 80 cores
make -j80

# Build modules
make -j80 modules

echo "Kernel build complete!"
echo "bzImage: arch/x86/boot/bzImage"
echo "Modules: drivers/xsc/*.ko"

# 2. Verify XSC Driver
echo ""
echo "Verifying XSC driver..."
ls -lh drivers/xsc/xsc*.ko || echo "Driver not built as module (built-in)"

# 3. Build Documentation suggests building glibc if needed
echo ""
echo "=== Build Complete ==="
echo ""
echo "Next steps:"
echo "1. Install kernel: sudo make install"
echo "2. Install modules: sudo make modules_install"
echo "3. Reboot into new kernel"
echo "4. Load XSC module: sudo modprobe xsc_core"
echo "5. Verify: ls -l /dev/xsc"
echo ""
echo "Documentation available in:"
echo "- ~/xsc-build/DEPLOYMENT_STATUS.md"
echo "- ~/xsc-build/XSC_SECURITY_ARCHITECTURE.md"
echo "- ~/xsc-build/PROJECT_SUMMARY.md"
echo "- /storage/icloud-backup/build/ (backup location)"
