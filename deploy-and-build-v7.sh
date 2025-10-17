#!/bin/bash
# Complete XSC v7 Deployment and Build Script
# Run this when server is back: ./deploy-and-build-v7.sh

set -e

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║         XSC v7 Complete Deployment and Build                ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# Step 1: Check server connectivity
echo "=== Step 1: Checking Server Connectivity ==="
if ! ssh bx.ee "echo OK" > /dev/null 2>&1; then
    echo "❌ ERROR: Cannot connect to bx.ee"
    echo "Please ensure server is online and SSH is working"
    exit 1
fi
echo "✅ Server is reachable"
echo ""

# Step 2: Deploy all files
echo "=== Step 2: Deploying Files to Server ==="
echo "Copying XSC v7 files..."

scp xsc-glibc-syscalls-v7.c bx.ee:/storage/icloud-backup/build/
scp integrate-xsc-glibc.sh bx.ee:/storage/icloud-backup/build/
scp build-xsc-v7-iso.sh bx.ee:/storage/icloud-backup/build/
scp xsc-build-governor.sh bx.ee:/storage/icloud-backup/build/
scp start-build-governor.sh bx.ee:/storage/icloud-backup/build/
scp watch-build-status.sh bx.ee:/storage/icloud-backup/build/
scp test-xsc-hello.c bx.ee:/storage/icloud-backup/build/
scp test-xsc-fork.c bx.ee:/storage/icloud-backup/build/

ssh bx.ee "chmod +x /storage/icloud-backup/build/*.sh"

echo "✅ Files deployed"
echo ""

# Step 3: Start Build Governor
echo "=== Step 3: Starting Build Governor ==="
echo "The governor will monitor and control builds to prevent overload"
ssh bx.ee "cd /storage/icloud-backup/build && doas ./start-build-governor.sh" || {
    echo "⚠️  Warning: Could not start governor with doas"
    echo "Trying without doas (lower priority)..."
    ssh bx.ee "cd /storage/icloud-backup/build && ./start-build-governor.sh" || {
        echo "⚠️  Governor failed to start, continuing without it"
    }
}
echo ""

# Step 4: Integrate glibc XSC shim
echo "=== Step 4: Integrating glibc XSC Shim ==="
ssh bx.ee "cd /storage/icloud-backup/build && nice -n 19 ionice -c 3 ./integrate-xsc-glibc.sh"
echo "✅ glibc integration complete"
echo ""

# Step 5: Rebuild glibc in toolchain
echo "=== Step 5: Rebuilding Toolchain glibc ==="
ssh bx.ee "cd /storage/icloud-backup/build/xsc-toolchain-x86_64-base/build/glibc && \
    nice -n 19 ionice -c 3 make -j30 && \
    nice -n 19 make install"
echo "✅ Toolchain glibc rebuilt"
echo ""

# Step 6: Test with simple program
echo "=== Step 6: Testing XSC Syscalls ==="
ssh bx.ee "cd /storage/icloud-backup/build && \
    /storage/icloud-backup/build/xsc-toolchain-x86_64-base/bin/x86_64-xsc-linux-gnu-gcc \
    -o test-xsc-hello test-xsc-hello.c && \
    echo 'Test program compiled successfully'"
echo "✅ Test program compiled"
echo ""

# Step 7: Build ISO
echo "=== Step 7: Building XSC v7 ISO ==="
echo "This will take 2-6 hours..."
echo "Monitor progress with: ssh bx.ee './watch-build-status.sh'"
echo ""

# Start build in background
ssh bx.ee "cd /storage/icloud-backup/build && \
    nohup nice -n 19 ionice -c 3 ./build-xsc-v7-iso.sh > xsc-v7-build.log 2>&1 &"

echo "✅ ISO build started in background"
echo ""
echo "Build is running on bx.ee"
echo ""
echo "To monitor:"
echo "  ssh bx.ee 'tail -f /storage/icloud-backup/build/xsc-v7-build.log'"
echo "  ssh bx.ee './watch-build-status.sh'"
echo ""
echo "When build completes, copy ISO with:"
echo "  scp bx.ee:/storage/icloud-backup/build/xsc-v7-iso/xsc-debian-v7-base.iso ~/Desktop/"
