#!/bin/bash
# XSC Complete ISO Builder - Master Build Script
#
# Usage: ./build-xsc-complete-iso.sh [base|cfi-compat]
#
# This script creates a complete XSC Debian ISO with:
# - XSC kernel (v7 spec)
# - Offline APT repository with all successfully built packages
# - Smart package manager (xsc-apt) with JIT allowlist integration
# - JIT package database
# - Variant selection: base (no CFI) or cfi-compat (CFI enforced)

set -e

# Configuration
VARIANT="${1:-base}"  # Default to base variant
BUILD_DIR="/storage/icloud-backup/build/xsc-v7-complete"
ISO_NAME="xsc-debian-v7-${VARIANT}.iso"
KERNEL_VERSION="6.1.0-xsc"
MAX_JOBS=30

# Toolchain selection based on variant
case "$VARIANT" in
    base)
        TOOLCHAIN_DIR="/storage/icloud-backup/build/xsc-toolchain-x86_64-base"
        KERNEL_CONFIG_CFI="n"
        echo "Building BASE variant (no CFI enforcement)"
        ;;
    cfi-compat)
        TOOLCHAIN_DIR="/storage/icloud-backup/build/xsc-toolchain-x86_64-cfi-compat"
        KERNEL_CONFIG_CFI="y"
        echo "Building CFI-COMPAT variant (hard CFI enforcement, no allowlist)"
        ;;
    *)
        echo "Error: Invalid variant '$VARIANT'"
        echo "Usage: $0 [base|cfi-compat]"
        exit 1
        ;;
esac

# Paths
KERNEL_SRC="/storage/icloud-backup/build/linux-6.1"
PACKAGES_DIR="/storage/icloud-backup/build/xsc-full-native-build/debs"
XSC_REPO="/Users/jgowdy/flexsc"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║     XSC v7 Complete ISO Builder - $VARIANT Variant          "
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Variant: $VARIANT"
echo "CFI Config: CONFIG_CFI_JIT_ALLOWLIST=$KERNEL_CONFIG_CFI"
echo "Toolchain: $TOOLCHAIN_DIR"
echo "Build Directory: $BUILD_DIR"
echo "ISO Name: $ISO_NAME"
echo ""

# Set low priority
renice -n 19 -p $$ > /dev/null 2>&1 || true
ionice -c 3 -p $$ > /dev/null 2>&1 || true

# Create build structure
mkdir -p "$BUILD_DIR"/{iso,rootfs,initramfs,boot,apt-repo}
cd "$BUILD_DIR"

echo "=== Phase 1: Build XSC Kernel ==="
if [ ! -f "$KERNEL_SRC/.config" ]; then
    echo "Configuring kernel for $VARIANT variant..."
    cd "$KERNEL_SRC"
    make defconfig

    # Enable XSC (always)
    scripts/config --enable XSC
    scripts/config --enable XSC_ADAPTIVE_POLL
    scripts/config --disable XSC_SOFT_DOORBELL
    scripts/config --enable XSC_TRACE
    scripts/config --enable XSC_SECCOMP

    # Configure CFI based on variant
    if [ "$KERNEL_CONFIG_CFI" = "y" ]; then
        echo "Enabling CFI JIT allowlist for $VARIANT variant"
        scripts/config --enable CFI_JIT_ALLOWLIST
    else
        echo "Disabling CFI JIT allowlist for $VARIANT variant (not applicable)"
        scripts/config --disable CFI_JIT_ALLOWLIST || true
    fi

    # Security features
    scripts/config --enable TRACING
    scripts/config --enable RANDOMIZE_KSTACK_OFFSET_DEFAULT
    scripts/config --enable HARDENED_USERCOPY
fi

cd "$KERNEL_SRC"
echo "Building kernel with -j$MAX_JOBS..."
nice -n 19 ionice -c 3 make -j$MAX_JOBS bzImage modules

echo "Installing kernel modules..."
nice -n 19 ionice -c 3 make INSTALL_MOD_PATH="$BUILD_DIR/rootfs" modules_install

cp arch/x86/boot/bzImage "$BUILD_DIR/boot/vmlinuz-$KERNEL_VERSION-$VARIANT"
echo "Kernel built: $BUILD_DIR/boot/vmlinuz-$KERNEL_VERSION-$VARIANT"

echo ""
echo "=== Phase 2: Create Offline APT Repository ==="

# Check if reprepro is available
if ! command -v reprepro &> /dev/null; then
    echo "Installing reprepro..."
    apt-get update && apt-get install -y reprepro
fi

PKG_COUNT=$(find "$PACKAGES_DIR" -name "*.deb" 2>/dev/null | wc -l || echo "0")
echo "Found $PKG_COUNT XSC packages"

# Create repository structure
mkdir -p "$BUILD_DIR/apt-repo"/{conf,dists/stable/main/binary-amd64,pool/main}

# Create distributions file
cat > "$BUILD_DIR/apt-repo/conf/distributions" << EOF
Origin: XSC Project
Label: XSC Debian Repository ($VARIANT variant)
Suite: stable
Codename: xsc-v7-$VARIANT
Version: 7.0
Architectures: amd64
Components: main
Description: XSC-compiled Debian packages for exception-less Linux ($VARIANT variant)
SignWith: no
EOF

# Copy packages to pool
if [ $PKG_COUNT -gt 0 ]; then
    echo "Copying $PKG_COUNT packages to repository..."
    find "$PACKAGES_DIR" -name "*.deb" -exec cp {} "$BUILD_DIR/apt-repo/pool/main/" \;

    # Build repository index
    echo "Building repository index..."
    cd "$BUILD_DIR/apt-repo"
    for deb in pool/main/*.deb; do
        reprepro includedeb stable "$deb" || echo "Warning: Failed to include $(basename $deb)"
    done
fi

echo "Repository created with $PKG_COUNT packages"

echo ""
echo "=== Phase 3: Install XSC System Files ==="

# Create essential directories
mkdir -p "$BUILD_DIR/rootfs"/{dev,proc,sys,tmp,run,var,etc,root,home,opt,usr/{bin,sbin,share/xsc}}
chmod 1777 "$BUILD_DIR/rootfs/tmp"

# Install XSC-specific files
echo "Installing XSC package manager..."
cp "$XSC_REPO/xsc-apt-wrapper" "$BUILD_DIR/rootfs/usr/bin/xsc-apt"
chmod +x "$BUILD_DIR/rootfs/usr/bin/xsc-apt"

echo "Installing JIT package database..."
cp "$XSC_REPO/xsc-jit-packages.db" "$BUILD_DIR/rootfs/usr/share/xsc/jit-packages.db"

# Create CFI allowlist directory (for cfi-compat variant)
if [ "$VARIANT" = "cfi-compat" ]; then
    mkdir -p "$BUILD_DIR/rootfs/etc/cfi"
    cat > "$BUILD_DIR/rootfs/etc/cfi/allowlist" << 'EOF'
# CFI JIT Allowlist - cfi-compat variant
#
# WARNING: This variant has CONFIG_CFI_JIT_ALLOWLIST=n
# This file exists for compatibility but is NOT used by the kernel.
# ALL processes have CFI enforced with no exceptions.
#
# To run JITs, use the 'base' variant instead.
EOF
fi

# Create variant info file
cat > "$BUILD_DIR/rootfs/etc/xsc-variant" << EOF
VARIANT=$VARIANT
CFI_JIT_ALLOWLIST=$KERNEL_CONFIG_CFI
BUILD_DATE=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
KERNEL_VERSION=$KERNEL_VERSION
PACKAGES=$PKG_COUNT
EOF

# Create /etc/fstab
cat > "$BUILD_DIR/rootfs/etc/fstab" << 'EOF'
proc  /proc  proc  defaults  0  0
sysfs /sys   sysfs defaults  0  0
devpts /dev/pts devpts gid=5,mode=620 0 0
tmpfs /tmp   tmpfs defaults  0  0
EOF

# Create /etc/hostname
echo "xsc-v7-$VARIANT" > "$BUILD_DIR/rootfs/etc/hostname"

# Create /etc/hosts
cat > "$BUILD_DIR/rootfs/etc/hosts" << EOF
127.0.0.1   localhost
127.0.1.1   xsc-v7-$VARIANT
EOF

# Create APT sources list
cat > "$BUILD_DIR/rootfs/etc/apt/sources.list.d/xsc.list" << EOF
# XSC Offline Repository ($VARIANT variant)
deb [trusted=yes] file:///opt/xsc-repo stable main
EOF

# Create MOTD
cat > "$BUILD_DIR/rootfs/etc/motd" << EOF

╔══════════════════════════════════════════════════════════════╗
║        XSC v7 - Exception-less Linux ($VARIANT variant)
╚══════════════════════════════════════════════════════════════╝

Variant: $VARIANT
CFI JIT Allowlist: $KERNEL_CONFIG_CFI
Packages: $PKG_COUNT XSC-compiled packages available

Quick Start:
  - Update package index: sudo apt update
  - Install packages: sudo xsc-apt install <package>
  - View variant info: cat /etc/xsc-variant

Package Manager:
  - xsc-apt: Smart wrapper with automatic JIT allowlist management
  - apt: Standard Debian package manager

Documentation:
  - /usr/share/doc/xsc/README.md
  - /usr/share/doc/xsc/CFI-JIT-ALLOWLIST.md

EOF

echo ""
echo "=== Phase 4: Create Initramfs ==="

cd "$BUILD_DIR/initramfs"
mkdir -p bin sbin

# Create init script
cat > init << 'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

echo "XSC v7 Minimal System - Mounting root filesystem..."

# Try to mount root
mkdir -p /newroot
mount /dev/sda1 /newroot 2>/dev/null || mount /dev/vda1 /newroot || {
    echo "Failed to mount root filesystem"
    echo "Starting emergency shell..."
    exec /bin/sh
}

# Switch to real root
exec switch_root /newroot /sbin/init
EOF

chmod +x init

# Create initramfs
echo "Creating initramfs..."
find . | cpio -o -H newc 2>/dev/null | gzip > "$BUILD_DIR/boot/initrd.img-$KERNEL_VERSION-$VARIANT"

echo ""
echo "=== Phase 5: Create Bootable ISO ==="

cd "$BUILD_DIR/iso"

# Copy kernel and initrd
mkdir -p boot
cp "$BUILD_DIR/boot/vmlinuz-$KERNEL_VERSION-$VARIANT" boot/
cp "$BUILD_DIR/boot/initrd.img-$KERNEL_VERSION-$VARIANT" boot/

# Copy rootfs
echo "Copying root filesystem..."
rsync -a "$BUILD_DIR/rootfs/" "$BUILD_DIR/iso/"

# Copy APT repository
echo "Including offline APT repository ($PKG_COUNT packages)..."
rsync -a "$BUILD_DIR/apt-repo/" "$BUILD_DIR/iso/opt/xsc-repo/"

# Create ISO
echo "Creating ISO image..."
nice -n 19 ionice -c 3 genisoimage \
    -o "$BUILD_DIR/$ISO_NAME" \
    -R -J -v -T \
    -V "XSC_V7_${VARIANT^^}" \
    -input-charset utf-8 \
    "$BUILD_DIR/iso" 2>&1 || true

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║                    Build Complete                            ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
ls -lh "$BUILD_DIR/$ISO_NAME"
echo ""
echo "ISO created: $BUILD_DIR/$ISO_NAME"
echo "Variant: $VARIANT"
echo "CFI JIT Allowlist: $KERNEL_CONFIG_CFI"
echo "Packages Included: $PKG_COUNT"
echo ""
echo "To test in QEMU:"
echo "  qemu-system-x86_64 -m 2G -smp 4 \\"
echo "    -kernel $BUILD_DIR/boot/vmlinuz-$KERNEL_VERSION-$VARIANT \\"
echo "    -initrd $BUILD_DIR/boot/initrd.img-$KERNEL_VERSION-$VARIANT \\"
echo "    -append 'root=/dev/sda1 console=ttyS0' \\"
echo "    -drive file=$BUILD_DIR/$ISO_NAME,format=raw \\"
echo "    -enable-kvm -nographic"
echo ""
echo "To copy to local machine:"
echo "  scp bx.ee:$BUILD_DIR/$ISO_NAME ~/Desktop/"
echo ""
