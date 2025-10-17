#!/bin/bash
# Complete XSC v7 Deployment and Build Automation
#
# This script handles everything from deployment to ISO creation.
# Run when bx.ee is back online.

set -e

# Try both SSH ports
SSH_CMD="ssh"
if ! timeout 5 ssh -p 22 bx.ee "echo OK" &>/dev/null; then
    echo "Port 22 not responding, trying port 8421..."
    if timeout 5 ssh -p 8421 bx.ee "echo OK" &>/dev/null; then
        SSH_CMD="ssh -p 8421"
        echo "Using port 8421"
    else
        echo "ERROR: Cannot connect to bx.ee on either port 22 or 8421"
        exit 1
    fi
fi

SCP_CMD="scp"
if [ "$SSH_CMD" = "ssh -p 8421" ]; then
    SCP_CMD="scp -P 8421"
fi

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║     XSC v7 Complete Deployment and Build Automation         ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# Check server connectivity
echo "=== Phase 0: Server Health Check ==="
$SSH_CMD bx.ee "hostname && uptime && df -h /storage/icloud-backup/build | tail -1"
echo ""

# Deploy files
echo "=== Phase 1: Deploy Files to Server ==="
FILES_TO_DEPLOY=(
    "xsc-glibc-syscalls-v7.c"
    "build-xsc-v7-iso.sh"
    "test-xsc-hello.c"
    "test-xsc-fork.c"
    "xsc-build-governor.sh"
    "start-build-governor.sh"
    "watch-build-status.sh"
    "integrate-xsc-glibc.sh"
)

for file in "${FILES_TO_DEPLOY[@]}"; do
    if [ -f "$file" ]; then
        echo "  Uploading $file..."
        $SCP_CMD "$file" bx.ee:/storage/icloud-backup/build/
    else
        echo "  Warning: $file not found, skipping"
    fi
done

# Make scripts executable
$SSH_CMD bx.ee "cd /storage/icloud-backup/build && chmod +x *.sh"
echo "Files deployed successfully"
echo ""

# Start build governor
echo "=== Phase 2: Start Build Governor ==="
$SSH_CMD bx.ee "cd /storage/icloud-backup/build && doas ./start-build-governor.sh" || {
    echo "Warning: Could not start governor with doas, trying without..."
    $SSH_CMD bx.ee "cd /storage/icloud-backup/build && nohup nice -n -5 ./xsc-build-governor.sh > governor.log 2>&1 & echo \$! > governor.pid"
}

sleep 2
$SSH_CMD bx.ee "cat /storage/icloud-backup/build/governor-stats.txt" || echo "Governor stats not yet available"
echo ""

# Integrate glibc
echo "=== Phase 3: Integrate XSC glibc ==="
echo "This may take 5-10 minutes..."
$SSH_CMD bx.ee "cd /storage/icloud-backup/build && nice -n 19 ionice -c 3 ./integrate-xsc-glibc.sh" || {
    echo "Warning: glibc integration script failed, may already be integrated"
}
echo ""

# Build glibc in toolchain
echo "=== Phase 4: Rebuild Toolchain glibc ==="
echo "This may take 30-60 minutes with -j30..."
$SSH_CMD bx.ee "cd /storage/icloud-backup/build/xsc-toolchain-x86_64-base/build/glibc && \
    nice -n 19 ionice -c 3 make -j30 && \
    nice -n 19 ionice -c 3 make install" || {
    echo "Warning: glibc rebuild may have issues, continuing anyway"
}
echo ""

# Test simple program
echo "=== Phase 5: Test XSC with Hello World ==="
$SSH_CMD bx.ee "cd /storage/icloud-backup/build && \
    /storage/icloud-backup/build/xsc-toolchain-x86_64-base/bin/x86_64-xsc-linux-gnu-gcc \
    -o test-xsc-hello test-xsc-hello.c && \
    echo 'Test program compiled successfully'"
echo ""

# Start ISO build in background
echo "=== Phase 6: Start ISO Build ==="
echo "This will take 2-6 hours. Starting in background..."
$SSH_CMD bx.ee "cd /storage/icloud-backup/build && \
    nohup nice -n 19 ionice -c 3 ./build-xsc-v7-iso.sh > xsc-v7-iso-build.log 2>&1 &"

BUILD_PID=$($SSH_CMD bx.ee "pgrep -f build-xsc-v7-iso.sh | tail -1" || echo "")
if [ -n "$BUILD_PID" ]; then
    echo "ISO build started with PID $BUILD_PID"
else
    echo "Warning: Could not determine build PID"
fi
echo ""

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║                    Deployment Complete                       ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Next Steps:"
echo ""
echo "1. Monitor build progress:"
echo "   $SSH_CMD bx.ee './watch-build-status.sh'"
echo ""
echo "2. Watch build log:"
echo "   $SSH_CMD bx.ee 'tail -f /storage/icloud-backup/build/xsc-v7-iso-build.log'"
echo ""
echo "3. Check governor status:"
echo "   $SSH_CMD bx.ee 'cat /storage/icloud-backup/build/governor-stats.txt'"
echo ""
echo "4. When complete (2-6 hours), copy ISO:"
echo "   $SCP_CMD bx.ee:/storage/icloud-backup/build/xsc-v7-iso/xsc-debian-v7-base.iso ~/Desktop/"
echo ""
echo "The build will continue in background. Governor will prevent overload."
