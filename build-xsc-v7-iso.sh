#!/bin/bash
# Build XSC v7 Minimal Debian ISO
# Resource-conscious build using nice/ionice and -j30

set -e

# Set low priority for entire script
renice -n 19 -p $$ > /dev/null 2>&1 || true
ionice -c 3 -p $$ > /dev/null 2>&1 || true

# Configuration
BUILD_DIR="/storage/icloud-backup/build/xsc-v7-iso"
ISO_NAME="xsc-debian-v7-base.iso"
KERNEL_VERSION="6.1.0-xsc"
MAX_JOBS=30

# Paths
TOOLCHAIN_DIR="/storage/icloud-backup/build/xsc-toolchain-x86_64-base"
KERNEL_SRC="/storage/icloud-backup/build/linux-6.1"
PACKAGES_DIR="/storage/icloud-backup/build/xsc-debian-full/results"

echo "=== XSC v7 Minimal ISO Builder ==="
echo "Build directory: $BUILD_DIR"
echo "ISO name: $ISO_NAME"
echo "Using up to $MAX_JOBS parallel jobs"
echo ""

# Check load before starting
check_load() {
    load=$(uptime | awk '{print $10}' | cut -d, -f1 | cut -d. -f1)
    if [ "$load" -gt 60 ]; then
        echo "Load too high ($load), waiting 60s..."
        sleep 60
        check_load
    fi
}

check_load

# Create build structure
mkdir -p "$BUILD_DIR"/{iso,rootfs,initramfs,boot}
cd "$BUILD_DIR"

echo "=== Phase 1: Build XSC Kernel ==="
if [ ! -f "$KERNEL_SRC/.config" ]; then
    echo "Configuring kernel..."
    cd "$KERNEL_SRC"
    make defconfig

    # Enable XSC
    scripts/config --enable XSC
    scripts/config --enable XSC_ADAPTIVE_POLL
    scripts/config --disable XSC_SOFT_DOORBELL
    scripts/config --enable XSC_TRACE
    scripts/config --enable XSC_SECCOMP

    # Enable required features
    scripts/config --enable TRACING
    scripts/config --enable RANDOMIZE_KSTACK_OFFSET_DEFAULT
    scripts/config --enable HARDENED_USERCOPY
fi

cd "$KERNEL_SRC"
echo "Building kernel with -j$MAX_JOBS..."
nice -n 19 ionice -c 3 make -j$MAX_JOBS bzImage modules

echo "Installing kernel modules..."
nice -n 19 ionice -c 3 make INSTALL_MOD_PATH="$BUILD_DIR/rootfs" modules_install

# Copy kernel
cp arch/x86/boot/bzImage "$BUILD_DIR/boot/vmlinuz-$KERNEL_VERSION"
echo "Kernel built: $BUILD_DIR/boot/vmlinuz-$KERNEL_VERSION"

echo ""
echo "=== Phase 2: Create Minimal Root Filesystem ==="

# Install essential packages from already-built debs
ESSENTIAL_PACKAGES=(
    "bash"
    "coreutils"
    "util-linux"
    "ncurses-base"
    "ncurses-bin"
    "readline-common"
    "libc6"
    "libncurses6"
    "libreadline8"
)

echo "Installing essential packages..."
for pkg in "${ESSENTIAL_PACKAGES[@]}"; do
    echo "  - $pkg"
    # Find and extract .deb files
    find "$PACKAGES_DIR" -name "${pkg}_*.deb" -exec dpkg-deb -x {} "$BUILD_DIR/rootfs" \; 2>/dev/null || true
done

# Create essential directories
mkdir -p "$BUILD_DIR/rootfs"/{dev,proc,sys,tmp,run,var,etc,root,home}
chmod 1777 "$BUILD_DIR/rootfs/tmp"

# Create minimal /etc/fstab
cat > "$BUILD_DIR/rootfs/etc/fstab" <<'EOF'
proc  /proc  proc  defaults  0  0
sysfs /sys   sysfs defaults  0  0
devpts /dev/pts devpts gid=5,mode=620 0 0
tmpfs /tmp   tmpfs defaults  0  0
EOF

# Create /etc/hostname
echo "xsc-v7" > "$BUILD_DIR/rootfs/etc/hostname"

# Create /etc/hosts
cat > "$BUILD_DIR/rootfs/etc/hosts" <<'EOF'
127.0.0.1   localhost
127.0.1.1   xsc-v7
EOF

# Create minimal inittab for busybox init
cat > "$BUILD_DIR/rootfs/etc/inittab" <<'EOF'
::sysinit:/etc/init.d/rcS
::respawn:/bin/bash
::ctrlaltdel:/sbin/reboot
::shutdown:/sbin/swapoff -a
EOF

echo ""
echo "=== Phase 3: Create Initramfs ==="

cd "$BUILD_DIR/initramfs"

# Create directory structure
mkdir -p bin sbin usr/bin usr/sbin

# Copy busybox (if available) or create minimal init
if command -v busybox &> /dev/null; then
    cp $(which busybox) bin/busybox
    for applet in sh mount umount switch_root; do
        ln -sf busybox "bin/$applet"
    done
else
    echo "Warning: busybox not found, using simple init script"
fi

# Create init script
cat > init <<'EOF'
#!/bin/sh
# XSC v7 initramfs init script

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

echo "XSC v7 Minimal System - Mounting root filesystem..."

# Mount root (assuming first disk)
mkdir -p /newroot
mount /dev/sda1 /newroot 2>/dev/null || mount /dev/vda1 /newroot

# Switch to real root
exec switch_root /newroot /sbin/init
EOF

chmod +x init

# Create initramfs
echo "Creating initramfs..."
find . | cpio -o -H newc | gzip > "$BUILD_DIR/boot/initrd.img-$KERNEL_VERSION"

echo ""
echo "=== Phase 4: Create Bootable ISO ==="

cd "$BUILD_DIR/iso"

# Copy kernel and initrd
mkdir -p boot/grub
cp "$BUILD_DIR/boot/vmlinuz-$KERNEL_VERSION" boot/
cp "$BUILD_DIR/boot/initrd.img-$KERNEL_VERSION" boot/

# Create GRUB config
cat > boot/grub/grub.cfg <<EOF
set timeout=5
set default=0

menuentry 'XSC v7 Debian (Basic)' {
    linux /boot/vmlinuz-$KERNEL_VERSION root=/dev/sda1 ro quiet
    initrd /boot/initrd.img-$KERNEL_VERSION
}

menuentry 'XSC v7 Debian (Recovery)' {
    linux /boot/vmlinuz-$KERNEL_VERSION root=/dev/sda1 ro single
    initrd /boot/initrd.img-$KERNEL_VERSION
}
EOF

# Copy rootfs
echo "Copying root filesystem to ISO..."
rsync -a "$BUILD_DIR/rootfs/" "$BUILD_DIR/iso/"

# Create ISO (simple, for use with QEMU direct kernel boot)
echo "Creating ISO image..."
echo "Note: This ISO is designed for QEMU with direct kernel boot"
nice -n 19 ionice -c 3 genisoimage \
    -o "$BUILD_DIR/$ISO_NAME" \
    -R -J -v -T \
    -V "XSC_V7_BASE" \
    -input-charset utf-8 \
    "$BUILD_DIR/iso" 2>&1 || true

echo ""
echo "=== Build Complete ==="
ls -lh "$BUILD_DIR/$ISO_NAME"
echo ""
echo "ISO created at: $BUILD_DIR/$ISO_NAME"
echo ""
echo "To test in QEMU (with direct kernel boot):"
echo "  qemu-system-x86_64 -m 2G -smp 4 \\"
echo "    -kernel $BUILD_DIR/boot/vmlinuz-$KERNEL_VERSION \\"
echo "    -initrd $BUILD_DIR/boot/initrd.img-$KERNEL_VERSION \\"
echo "    -append 'root=/dev/sda1 console=ttyS0' \\"
echo "    -drive file=$BUILD_DIR/$ISO_NAME,format=raw \\"
echo "    -enable-kvm -nographic"
echo ""
echo "To copy to local machine:"
echo "  scp bx.ee:$BUILD_DIR/$ISO_NAME ~/Desktop/"
