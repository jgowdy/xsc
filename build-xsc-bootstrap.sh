#!/bin/bash
# Build XSC Bootstrap Environment
# Phase 1: Build minimal userspace with XSC toolchain
# Phase 2: Create bootable rootfs
# Phase 3: Bootstrap Debian inside XSC

set -e

BUILD_DIR=${BUILD_DIR:-/storage/icloud-backup/build}
TOOLCHAIN=$BUILD_DIR/xsc-toolchain-x86_64-base
BOOTSTRAP_DIR=$BUILD_DIR/xsc-bootstrap
ROOTFS=$BOOTSTRAP_DIR/rootfs
SOURCES=$BOOTSTRAP_DIR/sources

# Versions
BUSYBOX_VERSION=1.36.1
BASH_VERSION=5.2.21
COREUTILS_VERSION=9.4
NCURSES_VERSION=6.4

# Create directories including tmp
mkdir -p $BOOTSTRAP_DIR/{sources,build,rootfs,tmp}

# CRITICAL: Use BUILD_DIR/tmp instead of /tmp
# Set ALL possible temp directory variables for GCC and make
export TMPDIR=$BOOTSTRAP_DIR/tmp
export TEMP=$BOOTSTRAP_DIR/tmp
export TMP=$BOOTSTRAP_DIR/tmp
export TEMPDIR=$BOOTSTRAP_DIR/tmp

export PATH=$TOOLCHAIN/bin:$PATH
export CC=x86_64-xsc-linux-gnu-gcc
export CXX=x86_64-xsc-linux-gnu-g++
export AR=x86_64-xsc-linux-gnu-ar
export RANLIB=x86_64-xsc-linux-gnu-ranlib
export STRIP=x86_64-xsc-linux-gnu-strip

cd $BOOTSTRAP_DIR

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a $BOOTSTRAP_DIR/bootstrap.log
}

log "========================================="
log "XSC Bootstrap Environment Builder"
log "========================================="
log "Toolchain: $TOOLCHAIN"
log "Bootstrap: $BOOTSTRAP_DIR"
log "Rootfs:    $ROOTFS"
log "========================================="

# Phase 1: Build busybox (provides most basic utilities)
build_busybox() {
    log "Phase 1.1: Building busybox $BUSYBOX_VERSION for XSC..."

    cd $SOURCES
    if [ ! -f busybox-${BUSYBOX_VERSION}.tar.bz2 ]; then
        wget https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2
    fi

    rm -rf busybox-${BUSYBOX_VERSION}
    tar xf busybox-${BUSYBOX_VERSION}.tar.bz2
    cd busybox-${BUSYBOX_VERSION}

    # Configure busybox for static linking
    make defconfig

    # Enable static linking and disable features that need dynamic libs
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
    sed -i 's/CONFIG_FEATURE_PREFER_APPLETS=y/# CONFIG_FEATURE_PREFER_APPLETS is not set/' .config

    # Build
    make -j$(nproc) CROSS_COMPILE=x86_64-xsc-linux-gnu- 2>&1 | tee $BOOTSTRAP_DIR/busybox-build.log

    # Install to rootfs
    make CONFIG_PREFIX=$ROOTFS install 2>&1 | tee -a $BOOTSTRAP_DIR/busybox-build.log

    log "Busybox built and installed successfully"
}

# Phase 2: Build bash (needed for package build scripts)
build_bash() {
    log "Phase 1.2: Building bash $BASH_VERSION for XSC..."

    cd $SOURCES
    if [ ! -f bash-${BASH_VERSION}.tar.gz ]; then
        wget https://ftp.gnu.org/gnu/bash/bash-${BASH_VERSION}.tar.gz
    fi

    rm -rf bash-${BASH_VERSION}
    tar xf bash-${BASH_VERSION}.tar.gz
    cd bash-${BASH_VERSION}

    # Configure for cross-compilation
    ./configure \
        --host=x86_64-xsc-linux-gnu \
        --prefix=/usr \
        --without-bash-malloc \
        bash_cv_wcwidth_broken=no \
        2>&1 | tee $BOOTSTRAP_DIR/bash-config.log

    make -j$(nproc) 2>&1 | tee $BOOTSTRAP_DIR/bash-build.log
    make DESTDIR=$ROOTFS install 2>&1 | tee -a $BOOTSTRAP_DIR/bash-build.log

    # Create /bin/bash symlink
    mkdir -p $ROOTFS/bin
    ln -sf /usr/bin/bash $ROOTFS/bin/bash
    ln -sf bash $ROOTFS/bin/sh

    log "Bash built and installed successfully"
}

# Phase 3: Build ncurses (required by many tools)
build_ncurses() {
    log "Phase 1.3: Building ncurses $NCURSES_VERSION for XSC..."

    cd $SOURCES
    if [ ! -f ncurses-${NCURSES_VERSION}.tar.gz ]; then
        wget https://ftp.gnu.org/gnu/ncurses/ncurses-${NCURSES_VERSION}.tar.gz
    fi

    rm -rf ncurses-${NCURSES_VERSION}
    tar xf ncurses-${NCURSES_VERSION}.tar.gz
    cd ncurses-${NCURSES_VERSION}

    ./configure \
        --host=x86_64-xsc-linux-gnu \
        --prefix=/usr \
        --with-shared \
        --without-debug \
        --enable-pc-files \
        --with-pkg-config-libdir=/usr/lib/pkgconfig \
        2>&1 | tee $BOOTSTRAP_DIR/ncurses-config.log

    make -j$(nproc) 2>&1 | tee $BOOTSTRAP_DIR/ncurses-build.log
    make DESTDIR=$ROOTFS install 2>&1 | tee -a $BOOTSTRAP_DIR/ncurses-build.log

    log "Ncurses built and installed successfully"
}

# Phase 4: Set up rootfs structure
setup_rootfs() {
    log "Phase 2: Setting up rootfs structure..."

    cd $ROOTFS

    # Create standard directory structure
    mkdir -p {dev,proc,sys,tmp,var,home,root}
    mkdir -p etc/{init.d,network}
    mkdir -p usr/{bin,sbin,lib}
    mkdir -p var/{log,tmp}

    chmod 1777 tmp var/tmp

    # Create basic /etc/passwd
    cat > etc/passwd << 'EOF'
root:x:0:0:root:/root:/bin/bash
builder:x:1000:1000:Builder:/home/builder:/bin/bash
EOF

    # Create basic /etc/group
    cat > etc/group << 'EOF'
root:x:0:
builder:x:1000:
EOF

    # Create basic /etc/fstab
    cat > etc/fstab << 'EOF'
proc  /proc  proc  defaults  0  0
sysfs /sys   sysfs defaults  0  0
tmpfs /tmp   tmpfs defaults  0  0
EOF

    # Create init script
    cat > etc/init.d/rcS << 'EOF'
#!/bin/sh
# Mount essential filesystems
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t tmpfs tmpfs /tmp

# Set up basic networking
ip link set lo up

echo "XSC Bootstrap Environment Ready"
echo "==============================="
echo "Hostname: xsc-bootstrap"
echo "Kernel: $(uname -r)"
echo "==============================="

# Drop to shell
exec /bin/sh
EOF
    chmod +x etc/init.d/rcS

    # Create inittab for busybox init
    cat > etc/inittab << 'EOF'
::sysinit:/etc/init.d/rcS
::respawn:/bin/sh
::ctrlaltdel:/bin/umount -a -r
EOF

    # Copy toolchain libraries
    log "Copying toolchain libraries to rootfs..."
    mkdir -p lib lib64
    cp -a $TOOLCHAIN/x86_64-xsc-linux-gnu/lib/*.so* lib/ 2>/dev/null || true
    cp -a $TOOLCHAIN/x86_64-xsc-linux-gnu/lib64/*.so* lib64/ 2>/dev/null || true

    # Create dynamic linker symlink
    if [ -f lib64/ld-linux-x86-64.so.2 ]; then
        ln -sf /lib64/ld-linux-x86-64.so.2 lib64/ld-xsc-x86-64.so.2 2>/dev/null || true
    fi

    log "Rootfs structure created successfully"
}

# Phase 5: Create bootable image
create_boot_image() {
    log "Phase 3: Creating bootable image..."

    cd $BOOTSTRAP_DIR

    # Create initramfs
    cd $ROOTFS
    find . -print0 | cpio --null -ov --format=newc | gzip -9 > $BOOTSTRAP_DIR/initramfs-xsc.cpio.gz

    log "Initramfs created: $BOOTSTRAP_DIR/initramfs-xsc.cpio.gz"
    log "Size: $(du -h $BOOTSTRAP_DIR/initramfs-xsc.cpio.gz | cut -f1)"

    # Create QEMU launch script with virtfs for host access
    cat > $BOOTSTRAP_DIR/boot-xsc.sh << 'EOF'
#!/bin/bash
# Boot XSC kernel with bootstrap rootfs

KERNEL=${XSC_KERNEL:-/storage/icloud-backup/build/linux-6.1/arch/x86/boot/bzImage}
INITRAMFS=${XSC_INITRAMFS:-./initramfs-xsc.cpio.gz}
BUILD_DIR=${BUILD_DIR:-/storage/icloud-backup/build}

if [ ! -f "$KERNEL" ]; then
    echo "ERROR: XSC kernel not found at $KERNEL"
    echo "Set XSC_KERNEL environment variable to kernel path"
    exit 1
fi

if [ ! -f "$INITRAMFS" ]; then
    echo "ERROR: Initramfs not found at $INITRAMFS"
    exit 1
fi

echo "Booting XSC kernel with bootstrap environment..."
echo "Kernel:   $KERNEL"
echo "Initramfs: $INITRAMFS"
echo "Build dir: $BUILD_DIR"
echo ""

qemu-system-x86_64 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -m 64G \
    -smp 80 \
    -append "console=ttyS0 init=/etc/init.d/rcS" \
    -virtfs local,path=$BUILD_DIR,mount_tag=build,security_model=passthrough,id=build \
    -nographic \
    -enable-kvm \
    -cpu host
EOF
    chmod +x $BOOTSTRAP_DIR/boot-xsc.sh

    log "Boot script created: $BOOTSTRAP_DIR/boot-xsc.sh"
}

# Main execution
main() {
    log "Starting XSC bootstrap build process..."

    # Check toolchain exists
    if [ ! -d "$TOOLCHAIN" ]; then
        log "ERROR: Toolchain not found at $TOOLCHAIN"
        exit 1
    fi

    # Verify toolchain works
    if ! x86_64-xsc-linux-gnu-gcc --version >/dev/null 2>&1; then
        log "ERROR: XSC toolchain not in PATH or not working"
        exit 1
    fi

    log "Toolchain verified: $(x86_64-xsc-linux-gnu-gcc --version | head -1)"

    # Build components
    build_busybox
    build_ncurses
    build_bash

    # Set up rootfs
    setup_rootfs

    # Create bootable image
    create_boot_image

    log "========================================="
    log "XSC Bootstrap Build Complete!"
    log "========================================="
    log "Rootfs location: $ROOTFS"
    log "Initramfs:       $BOOTSTRAP_DIR/initramfs-xsc.cpio.gz"
    log "Boot script:     $BOOTSTRAP_DIR/boot-xsc.sh"
    log ""
    log "Next steps:"
    log "  1. Verify XSC kernel is available"
    log "  2. Run: $BOOTSTRAP_DIR/boot-xsc.sh"
    log "  3. Inside XSC environment, install Debian build tools"
    log "  4. Build all packages natively"
    log "========================================="
}

main "$@"
