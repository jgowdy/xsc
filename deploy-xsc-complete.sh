#!/bin/bash
# Deploy and Build XSC Complete ISO
#
# Usage: ./deploy-xsc-complete.sh [base|cfi-compat]
#
# This script:
# 1. Deploys all files to build server
# 2. Starts build governor for server protection
# 3. Builds complete XSC ISO with APT repository
# 4. Copies result back to local Desktop

set -e

VARIANT="${1:-base}"

# Try both SSH ports
SSH_CMD="ssh"
SCP_CMD="scp"
if ! timeout 5 ssh -p 22 bx.ee "echo OK" &>/dev/null; then
    echo "Port 22 not responding, trying port 8421..."
    if timeout 5 ssh -p 8421 bx.ee "echo OK" &>/dev/null; then
        SSH_CMD="ssh -p 8421"
        SCP_CMD="scp -P 8421"
        echo "Using port 8421"
    else
        echo "ERROR: Cannot connect to bx.ee on either port 22 or 8421"
        exit 1
    fi
fi

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║   XSC v7 Complete Deployment - $VARIANT Variant              "
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# Phase 0: Server Health Check
echo "=== Phase 0: Server Health Check ==="
$SSH_CMD bx.ee "hostname && uptime && df -h /storage/icloud-backup/build | tail -1"
echo ""

# Phase 1: Deploy Files
echo "=== Phase 1: Deploy Files to Server ==="
FILES_TO_DEPLOY=(
    "build-xsc-complete-iso.sh"
    "xsc-apt-wrapper"
    "xsc-jit-packages.db"
    "xsc-build-governor.sh"
    "start-build-governor.sh"
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
$SSH_CMD bx.ee "cd /storage/icloud-backup/build && chmod +x *.sh xsc-apt-wrapper"
echo "Files deployed successfully"
echo ""

# Phase 2: Start Build Governor
echo "=== Phase 2: Start Build Governor ==="
GOVERNOR_RUNNING=$($SSH_CMD bx.ee "pgrep -f xsc-build-governor || echo 0")
if [ "$GOVERNOR_RUNNING" != "0" ]; then
    echo "Governor already running (PID $GOVERNOR_RUNNING)"
else
    $SSH_CMD bx.ee "cd /storage/icloud-backup/build && doas ./start-build-governor.sh" || {
        echo "Warning: Could not start governor with doas, trying without..."
        $SSH_CMD bx.ee "cd /storage/icloud-backup/build && nohup nice -n -5 ./xsc-build-governor.sh > governor.log 2>&1 & echo \$! > governor.pid"
    }
    sleep 2
    $SSH_CMD bx.ee "cat /storage/icloud-backup/build/governor-stats.txt" || echo "Governor stats not yet available"
fi
echo ""

# Phase 3: Build Complete ISO
echo "=== Phase 3: Build Complete ISO ($VARIANT variant) ===""
echo "This may take 1-2 hours depending on package count..."
$SSH_CMD bx.ee "cd /storage/icloud-backup/build && nice -n 19 ionice -c 3 ./build-xsc-complete-iso.sh $VARIANT" || {
    echo "Warning: Build may have failed, checking output..."
}
echo ""

# Phase 4: Copy ISO to Desktop
echo "=== Phase 4: Copy ISO to Desktop ===""
ISO_PATH="/storage/icloud-backup/build/xsc-v7-complete/xsc-debian-v7-$VARIANT.iso"
$SCP_CMD "bx.ee:$ISO_PATH" ~/Desktop/ && {
    echo "ISO copied to ~/Desktop/xsc-debian-v7-$VARIANT.iso"
} || {
    echo "Warning: Could not copy ISO (may not exist yet)"
}

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║                    Deployment Complete                       ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Variant: $VARIANT"
echo "ISO Location: ~/Desktop/xsc-debian-v7-$VARIANT.iso"
echo ""
echo "To check governor status:"
echo "  $SSH_CMD bx.ee 'cat /storage/icloud-backup/build/governor-stats.txt'"
echo ""
echo "To build other variant:"
echo "  ./deploy-xsc-complete.sh cfi-compat"
echo ""
