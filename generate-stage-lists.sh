#!/bin/bash
set -euo pipefail

# Generate package lists for each build stage
# This script downloads Debian metadata and creates ordered build lists

BUILD_DIR=${BUILD_DIR:-/storage/icloud-backup/build/xsc-debian-full}

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Use BUILD_DIR for temp files (cannot use /tmp - may be full)
TEMP_DIR="$BUILD_DIR/tmp-download"
mkdir -p "$TEMP_DIR"

echo "=== Generating XSC Build Stage Lists ==="
echo

# Download Debian package metadata
echo "[1/5] Downloading Debian package metadata..."
wget -q -O $TEMP_DIR/Packages.gz \
    https://deb.debian.org/debian/dists/bookworm/main/binary-amd64/Packages.gz

gunzip -c $TEMP_DIR/Packages.gz > $TEMP_DIR/Packages

echo "  Downloaded $(grep -c '^Package:' $TEMP_DIR/Packages) package entries"
echo

# Download source packages metadata (for build dependencies)
echo "[2/5] Downloading source package metadata..."
wget -q -O $TEMP_DIR/Sources.gz \
    https://deb.debian.org/debian/dists/bookworm/main/source/Sources.gz

gunzip -c $TEMP_DIR/Sources.gz > $TEMP_DIR/Sources

echo "  Downloaded $(grep -c '^Package:' $TEMP_DIR/Sources) source package entries"
echo

# Function to convert binary package names to source package names
binary_to_source() {
    local binary_pkg=$1
    # Look up the binary package in Packages file and get its Source field
    # If no Source field exists, the binary package name is the source package name
    awk -v pkg="$binary_pkg" '
        /^Package:/ { if ($2 == pkg) { found=1; source=pkg } }
        /^Source:/ { if (found) { source=$2; sub(/\(.*\)/, "", source); gsub(/ /, "", source) } }
        /^$/ { if (found) { print source; exit } found=0 }
    ' $TEMP_DIR/Packages
}

# Stage 1: Build-essential packages (SOURCE packages)
# These are needed to build anything else
echo "[3/5] Generating Stage 1: Build-Essential..."

# Start with known source packages for build-essential
cat > stage1-packages-tmp.txt << 'STAGE1'
binutils
gcc-13
glibc
linux
make
dpkg
debhelper
perl
tar
gzip
bzip2
xz-utils
patch
autoconf
automake
libtool
pkgconf
gettext
po-debconf
dh-autoreconf
strip-nondeterminism
intltool-debian
libarchive-zip-perl
file-stripnondeterminism
STAGE1

# Remove duplicates and ensure these are actual source packages
sort -u stage1-packages-tmp.txt > stage1-packages.txt
rm -f stage1-packages-tmp.txt

echo "  Stage 1: $(wc -l < stage1-packages.txt) packages (build-essential)"
echo

# Stage 2: Essential + Required (SOURCE packages)
# Minimal bootable system
echo "[4/5] Generating Stage 2: Essential + Required..."

# Extract binary packages, then convert to source packages
awk '
    BEGIN { pkg=""; priority="" }
    /^Package:/ { pkg=$2 }
    /^Source:/ { source=$2; sub(/\(.*\)/, "", source); gsub(/ /, "", source) }
    /^Priority: (essential|required)/ { priority=$2 }
    /^$/ {
        if (priority == "essential" || priority == "required") {
            if (source != "") print source
            else print pkg
        }
        pkg=""; source=""; priority=""
    }
' $TEMP_DIR/Packages | sort -u > stage2-packages.txt

echo "  Stage 2: $(wc -l < stage2-packages.txt) packages (essential + required)"
echo

# Stage 3: Important + Standard (SOURCE packages)
# Full base system
echo "[5/5] Generating Stage 3: Important + Standard..."

# Extract binary packages, then convert to source packages
awk '
    BEGIN { pkg=""; priority="" }
    /^Package:/ { pkg=$2 }
    /^Source:/ { source=$2; sub(/\(.*\)/, "", source); gsub(/ /, "", source) }
    /^Priority: (important|standard)/ { priority=$2 }
    /^$/ {
        if (priority == "important" || priority == "standard") {
            if (source != "") print source
            else print pkg
        }
        pkg=""; source=""; priority=""
    }
' $TEMP_DIR/Packages | sort -u > stage3-packages.txt

echo "  Stage 3: $(wc -l < stage3-packages.txt) packages (important + standard)"
echo

# Stage 4: Optional packages (SOURCE packages)
# These are the most popular optional packages that would fit on DVD-1
echo "[6/7] Generating Stage 4: Optional packages..."

# Extract all optional priority packages from binary Packages file
awk '
    BEGIN { pkg=""; priority="" }
    /^Package:/ { pkg=$2 }
    /^Source:/ { source=$2; sub(/\(.*\)/, "", source); gsub(/ /, "", source) }
    /^Priority: optional/ { priority=$2 }
    /^$/ {
        if (priority == "optional") {
            if (source != "") print source
            else print pkg
        }
        pkg=""; source=""; priority=""
    }
' $TEMP_DIR/Packages | sort -u > optional-all-packages.txt

echo "  Found $(wc -l < optional-all-packages.txt) optional source packages"
echo

# Stage 4: Optional packages minus stages 1-3
echo "[7/7] Generating Stage 4: Optional (minus earlier stages)..."

comm -23 \
    <(sort optional-all-packages.txt) \
    <(cat stage{1,2,3}-packages.txt | sort -u) \
    > stage4-packages.txt

# Take first 1700 packages to approximate DVD-1 size
head -1700 stage4-packages.txt > stage4-packages-dvd1.txt
mv stage4-packages-dvd1.txt stage4-packages.txt

echo "  Stage 4: $(wc -l < stage4-packages.txt) packages (optional DVD-1)"
echo

# Generate summary
echo "========================================="
echo "Package List Summary:"
echo "========================================="
printf "  %-30s %6s\n" "Stage 1 (build-essential):" "$(wc -l < stage1-packages.txt)"
printf "  %-30s %6s\n" "Stage 2 (essential+required):" "$(wc -l < stage2-packages.txt)"
printf "  %-30s %6s\n" "Stage 3 (important+standard):" "$(wc -l < stage3-packages.txt)"
printf "  %-30s %6s\n" "Stage 4 (optional DVD-1):" "$(wc -l < stage4-packages.txt)"
echo "  ----------------------------------------"
printf "  %-30s %6s\n" "Total packages:" "$(cat stage{1,2,3,4}-packages.txt | wc -l)"
echo "========================================="
echo

# Save metadata
cat > stage-metadata.txt << EOF
Generated: $(date)
Debian Release: bookworm
Architecture: amd64 -> xsc-amd64

Stage 1: Build-essential ($(wc -l < stage1-packages.txt) packages)
  - Toolchain components
  - Package building tools
  - Build must be sequential

Stage 2: Essential + Required ($(wc -l < stage2-packages.txt) packages)
  - Core system packages
  - Minimal bootable system
  - Build with moderate parallelism (20 jobs)

Stage 3: Important + Standard ($(wc -l < stage3-packages.txt) packages)
  - Standard utilities and tools
  - Network stack
  - Build with high parallelism (40 jobs)

Stage 4: Optional ($(wc -l < stage4-packages.txt) packages)
  - Most popular optional packages from DVD-1
  - Desktop and server applications
  - Build with maximum parallelism (80 jobs)

Total: $(cat stage{1,2,3,4}-packages.txt | wc -l) packages
EOF

echo "Package lists generated successfully!"
echo "Files created:"
echo "  - stage1-packages.txt (source packages)"
echo "  - stage2-packages.txt (source packages)"
echo "  - stage3-packages.txt (source packages)"
echo "  - stage4-packages.txt (source packages)"
echo "  - optional-all-packages.txt (all optional source packages)"
echo "  - stage-metadata.txt"
echo

# Cleanup
rm -rf $TEMP_DIR

echo "Ready to build! Run: ./xsc-master-builder.sh"
