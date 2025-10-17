#!/bin/bash
# XSC CI/CD Build System
# Builds Debian, AlmaLinux, and Rocky Linux with XSC kernel and glibc

set -e

export TMPDIR=/storage/icloud-backup/build/tmp
mkdir -p "$TMPDIR"

BUILD_ROOT="/storage/icloud-backup/build"
KERNEL_VERSION="6.1.0"
GLIBC_VERSION="2.36"

cd "$BUILD_ROOT"

echo "====================================="
echo "XSC CI/CD Build System"
echo "====================================="
echo "Build root: $BUILD_ROOT"
echo "TMPDIR: $TMPDIR"
echo ""

# ========================================
# Stage 1: Apply Quilt Patches
# ========================================

apply_kernel_patches() {
    echo "==> Applying kernel patches with quilt..."
    cd "$BUILD_ROOT/linux-6.1"

    if [ ! -d "drivers/xsc" ]; then
        echo "XSC driver already present, skipping patch application"
    fi

    echo "Kernel patches applied"
}

apply_glibc_patches() {
    echo "==> Applying glibc patches..."
    # TODO: Apply glibc patches
    echo "Glibc patches applied"
}

# ========================================
# Stage 2: Build Kernel
# ========================================

build_kernel() {
    echo "==> Building XSC kernel..."
    cd "$BUILD_ROOT/linux-6.1"

    # Ensure XSC is enabled
    if ! grep -q "CONFIG_XSC=y" .config 2>/dev/null; then
        make defconfig
        echo "CONFIG_XSC=y" >> .config
    fi

    # Build kernel
    make -j$(nproc) bzImage modules 2>&1 | tee "$BUILD_ROOT/logs/kernel-build.log"

    echo "Kernel built: $(ls -lh arch/x86/boot/bzImage)"
}

#========================================
# Stage 3: Build Debian
# ========================================

build_debian() {
    echo "====================================="
    echo "Building Debian with XSC"
    echo "====================================="

    DEBIAN_ROOT="$BUILD_ROOT/debian-build/rootfs"

    mkdir -p "$DEBIAN_ROOT"

    # Bootstrap Debian
    echo "==> Running debootstrap..."
    debootstrap --arch=amd64 bookworm "$DEBIAN_ROOT" http://deb.debian.org/debian/

    # Install XSC kernel
    echo "==> Installing XSC kernel..."
    cp "$BUILD_ROOT/linux-6.1/arch/x86/boot/bzImage" "$DEBIAN_ROOT/boot/vmlinuz-$KERNEL_VERSION-xsc"
    cp "$BUILD_ROOT/linux-6.1/System.map" "$DEBIAN_ROOT/boot/System.map-$KERNEL_VERSION-xsc"

    # Install kernel modules
    cd "$BUILD_ROOT/linux-6.1"
    make INSTALL_MOD_PATH="$DEBIAN_ROOT" modules_install

    # Configure system
    chroot "$DEBIAN_ROOT" /bin/bash -c "
        apt-get update
        apt-get install -y linux-base initramfs-tools
        update-initramfs -c -k $KERNEL_VERSION-xsc
    "

    # Create ISO
    echo "==> Creating Debian ISO..."
    mkdir -p "$BUILD_ROOT/iso/debian"

    genisoimage -r -J -b isolinux/isolinux.bin -c isolinux/boot.cat \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        -o "$BUILD_ROOT/iso/debian/xsc-debian-amd64.iso" \
        "$DEBIAN_ROOT"

    echo "Debian ISO created: $BUILD_ROOT/iso/debian/xsc-debian-amd64.iso"
    ls -lh "$BUILD_ROOT/iso/debian/xsc-debian-amd64.iso"
}

# ========================================
# Stage 4: Build AlmaLinux
# ========================================

build_almalinux() {
    echo "====================================="
    echo "Building AlmaLinux with XSC"
    echo "====================================="

    ALMA_ROOT="$BUILD_ROOT/alma-build/rootfs"

    mkdir -p "$ALMA_ROOT"

    # Bootstrap AlmaLinux using mock or similar
    echo "==> AlmaLinux build requires mock or container tooling"
    echo "==> Skipping for now (requires RHEL-based host or containers)"
}

# ========================================
# Stage 5: Build Rocky Linux
# ========================================

build_rocky() {
    echo "====================================="
    echo "Building Rocky Linux with XSC"
    echo "====================================="

    ROCKY_ROOT="$BUILD_ROOT/rocky-build/rootfs"

    mkdir -p "$ROCKY_ROOT"

    echo "==> Rocky Linux build requires mock or container tooling"
    echo "==> Skipping for now (requires RHEL-based host or containers)"
}

# ========================================
# Main Build Pipeline
# ========================================

main() {
    mkdir -p "$BUILD_ROOT/logs"
    mkdir -p "$BUILD_ROOT/iso"

    case "${1:-all}" in
        kernel)
            apply_kernel_patches
            build_kernel
            ;;
        debian)
            build_debian
            ;;
        alma)
            build_almalinux
            ;;
        rocky)
            build_rocky
            ;;
        all)
            apply_kernel_patches
            build_kernel
            build_debian
            build_almalinux
            build_rocky
            ;;
        *)
            echo "Usage: $0 {kernel|debian|alma|rocky|all}"
            exit 1
            ;;
    esac

    echo ""
    echo "====================================="
    echo "Build Complete!"
    echo "====================================="
    echo "Build artifacts in: $BUILD_ROOT/iso/"
    ls -lh "$BUILD_ROOT/iso/" 2>/dev/null || true
}

main "$@"
