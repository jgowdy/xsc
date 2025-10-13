#!/bin/bash
# Build all installation ISOs

set -e

echo "Building all ISOs..."

# Use existing ISO build scripts
./build-debian-netinstall.sh &
PID1=$!

./build-debian-archive.sh &
PID2=$!

./build-alma-netinstall.sh &
PID3=$!

./build-alma-archive.sh &
PID4=$!

wait $PID1 && echo "✓ Debian netinstall ISO complete"
wait $PID2 && echo "✓ Debian full archive ISO complete"
wait $PID3 && echo "✓ AlmaLinux netinstall ISO complete"
wait $PID4 && echo "✓ AlmaLinux full archive ISO complete"

echo ""
echo "All ISOs built in /storage/icloud-backup/build/iso/"
ls -lh /storage/icloud-backup/build/iso/*.iso
