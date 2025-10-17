#!/bin/bash
set -euo pipefail

# Setup sbuild for XSC cross-compilation
# This script creates a build chroot configured for the XSC architecture

TOOLCHAIN=${TOOLCHAIN:-/storage/icloud-backup/build/xsc-toolchain-x86_64-base}
CHROOT_DIR=/var/lib/sbuild
CHROOT_NAME=bookworm-xsc-amd64

echo "=== Setting up XSC sbuild environment ==="
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This script must be run as root"
    echo "Run: sudo $0"
    exit 1
fi

# Check for XSC toolchain
if [ ! -d "$TOOLCHAIN" ]; then
    echo "ERROR: XSC toolchain not found at $TOOLCHAIN"
    echo "Please build the XSC toolchain first"
    exit 1
fi

echo "[1/6] Installing sbuild dependencies..."
apt-get update -qq
apt-get install -y \
    sbuild \
    schroot \
    debootstrap \
    eatmydata \
    ccache \
    autopkgtest \
    lintian \
    parallel \
    jigdo-file \
    grep-dctrl \
    dose-builddebcheck \
    apt-rdepends

echo

# Add user to sbuild group
echo "[2/6] Configuring user permissions..."
if [ -n "${SUDO_USER:-}" ]; then
    usermod -aG sbuild "$SUDO_USER"
    echo "  Added $SUDO_USER to sbuild group"
    echo "  Note: You may need to log out and back in for group changes to take effect"
fi

echo

# Create sbuild configuration
echo "[3/6] Creating sbuild configuration..."

mkdir -p /root/.sbuild

cat > /root/.sbuild/sbuildrc << 'EOF'
# XSC Cross-Compilation Configuration for sbuild

# Build options
$build_arch_all = 1;
$build_source = 0;
$distribution = 'bookworm';

# Maintainer
$maintainer_name = 'XSC Build System <xsc@example.com>';

# Build environment
$build_environment = {
    'DEB_BUILD_OPTIONS' => 'parallel=80 nocheck',
    'DEB_BUILD_PROFILES' => 'cross nocheck',
    'MAKEFLAGS' => '-j80',
};

# Use eatmydata for faster builds
$eatmydata = 1;

# Use ccache for faster rebuilds
$ccache = 1;

# Log verbosity
$log_colour = 1;
$verbose = 1;

# Chroot mode
$chroot_mode = 'unshare';

# Apt options
$apt_update = 1;
$apt_distupgrade = 0;
$apt_allow_unauthenticated = 1;

# External commands
$external_commands = {
    'post-build-commands' => [
        [
            'cleanup-build-deps',
            'apt-get', 'clean'
        ],
    ],
};

# Archive (don't compress logs)
$compress_build_log_mails = 'never';

1; # Return true
EOF

echo "  Created /root/.sbuild/sbuildrc"
echo

# Create base chroot
echo "[4/6] Creating base Debian bookworm chroot..."
echo "  This may take 10-15 minutes..."

if [ ! -f "$CHROOT_DIR/$CHROOT_NAME.tar.gz" ]; then
    # Create temporary chroot directory
    TMP_CHROOT=$(mktemp -d)

    # Bootstrap minimal Debian
    debootstrap \
        --variant=buildd \
        --arch=amd64 \
        --include=eatmydata,ccache,apt-utils,ca-certificates \
        bookworm \
        "$TMP_CHROOT" \
        http://deb.debian.org/debian

    echo "  Base system bootstrapped"

    # Configure chroot for XSC
    echo "  Configuring chroot for XSC architecture..."

    # Add XSC architecture
    chroot "$TMP_CHROOT" dpkg --add-architecture xsc-amd64

    # Configure dpkg to recognize xsc-amd64
    cat > "$TMP_CHROOT/etc/dpkg/dpkg.cfg.d/xsc" << 'DPKG_EOF'
# XSC Architecture Configuration
# This allows dpkg to recognize xsc-amd64 as a valid architecture
DPKG_EOF

    # Add sources.list
    cat > "$TMP_CHROOT/etc/apt/sources.list" << 'SOURCES_EOF'
deb http://deb.debian.org/debian bookworm main contrib
deb http://deb.debian.org/debian bookworm-updates main contrib
deb http://security.debian.org/debian-security bookworm-security main contrib

# XSC local repository (will be populated during builds)
deb [trusted=yes] file:///build/xsc-packages ./
SOURCES_EOF

    # Install build dependencies
    echo "  Installing build dependencies..."
    chroot "$TMP_CHROOT" apt-get update -qq
    chroot "$TMP_CHROOT" apt-get install -y --no-install-recommends \
        build-essential \
        fakeroot \
        devscripts \
        quilt \
        dpkg-dev \
        debhelper \
        dh-autoreconf \
        autoconf \
        automake \
        libtool \
        pkg-config \
        cmake \
        ninja-build \
        meson \
        bison \
        flex \
        gettext \
        texinfo \
        gawk \
        groff \
        less \
        libncurses-dev \
        zlib1g-dev \
        libssl-dev \
        libreadline-dev

    # Copy XSC toolchain into chroot
    echo "  Installing XSC toolchain..."
    mkdir -p "$TMP_CHROOT/opt/xsc-toolchain"
    cp -a "$TOOLCHAIN"/* "$TMP_CHROOT/opt/xsc-toolchain/"

    # Add XSC toolchain to PATH
    cat > "$TMP_CHROOT/etc/profile.d/xsc-toolchain.sh" << 'PROFILE_EOF'
# XSC Toolchain
export PATH=/opt/xsc-toolchain/bin:$PATH
export CC=x86_64-xsc-linux-gnu-gcc
export CXX=x86_64-xsc-linux-gnu-g++
export LD=x86_64-xsc-linux-gnu-ld
export AR=x86_64-xsc-linux-gnu-ar
export RANLIB=x86_64-xsc-linux-gnu-ranlib
export STRIP=x86_64-xsc-linux-gnu-strip
PROFILE_EOF

    # Create local package repository directory
    mkdir -p "$TMP_CHROOT/build/xsc-packages"

    # Create tarball
    echo "  Creating chroot tarball..."
    tar czf "$CHROOT_DIR/$CHROOT_NAME.tar.gz" -C "$TMP_CHROOT" .

    # Cleanup
    rm -rf "$TMP_CHROOT"

    echo "  Chroot created: $CHROOT_DIR/$CHROOT_NAME.tar.gz"
else
    echo "  Chroot already exists: $CHROOT_DIR/$CHROOT_NAME.tar.gz"
fi

echo

# Create schroot configuration
echo "[5/6] Creating schroot configuration..."

cat > "/etc/schroot/chroot.d/$CHROOT_NAME" << EOF
[$CHROOT_NAME]
description=Debian bookworm XSC build environment
type=file
file=$CHROOT_DIR/$CHROOT_NAME.tar.gz
users=sbuild
root-users=root
source-root-users=root
profile=sbuild
union-type=overlay
EOF

echo "  Created /etc/schroot/chroot.d/$CHROOT_NAME"
echo

# Test the chroot
echo "[6/6] Testing chroot..."

if schroot -c source:$CHROOT_NAME -u root -- /bin/bash -c 'echo "Chroot test successful"'; then
    echo "  Chroot is working correctly"
else
    echo "  ERROR: Chroot test failed"
    exit 1
fi

echo

echo "========================================="
echo "XSC sbuild environment setup complete!"
echo "========================================="
echo
echo "Configuration:"
echo "  Chroot: $CHROOT_DIR/$CHROOT_NAME.tar.gz"
echo "  Toolchain: /opt/xsc-toolchain (inside chroot)"
echo "  Architecture: xsc-amd64"
echo
echo "Test build a package:"
echo "  sbuild --host=xsc-amd64 -c $CHROOT_NAME <package>.dsc"
echo
echo "Build all packages:"
echo "  ./generate-stage-lists.sh"
echo "  ./xsc-master-builder.sh"
echo
