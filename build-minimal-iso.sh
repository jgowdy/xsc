#!/bin/bash
# Build minimal bootable ISO with XSC kernel for testing

set -e
export TMPDIR=/storage/icloud-backup/build/tmp

BUILD_ROOT=/storage/icloud-backup/build
ISO_DIR=$BUILD_ROOT/iso/minimal
KERNEL=$BUILD_ROOT/linux-6.1/arch/x86/boot/bzImage

echo "Building minimal XSC test ISO..."
mkdir -p $ISO_DIR/boot/isolinux
mkdir -p $TMPDIR

# Copy kernel
echo "Copying XSC kernel..."
cp $KERNEL $ISO_DIR/boot/vmlinuz-xsc

# Create minimal init script
echo "Creating initramfs..."
cat > $TMPDIR/init << 'INITEOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

echo ""
echo "========================================"
echo "XSC Kernel Test Boot"
echo "========================================"
echo "Kernel: $(uname -r)"
echo "Architecture: $(uname -m)"
echo ""

# Check for XSC driver
if [ -c /dev/xsc ]; then
    echo "SUCCESS: /dev/xsc device found!"
    ls -l /dev/xsc
else
    echo "INFO: /dev/xsc not found (module may need loading)"
    echo "Available devices:"
    ls -l /dev/ | head -20
fi

echo ""
echo "System booted successfully with XSC kernel!"
echo "Dropping to shell..."
exec /bin/sh
INITEOF

chmod +x $TMPDIR/init

# Create initramfs
cd $TMPDIR
rm -rf initramfs
mkdir -p initramfs/{bin,sbin,etc,proc,sys,dev,usr/bin,usr/sbin}
cp /bin/busybox initramfs/bin/
cd initramfs
for cmd in sh ls cat mount uname dmesg grep; do
    ln -sf bin/busybox bin/$cmd
done
cp $TMPDIR/init init

find . | cpio -o -H newc 2>/dev/null | gzip > $ISO_DIR/boot/initrd.gz
cd $TMPDIR
rm -rf initramfs

# Create isolinux config
cat > $ISO_DIR/boot/isolinux/isolinux.cfg << 'ISOLEOF'
DEFAULT xsc
PROMPT 0
TIMEOUT 30
LABEL xsc
    KERNEL /boot/vmlinuz-xsc
    APPEND initrd=/boot/initrd.gz console=ttyS0 console=tty0
ISOLEOF

# Copy isolinux files
echo "Copying bootloader files..."
cp /usr/lib/ISOLINUX/isolinux.bin $ISO_DIR/boot/isolinux/
cp /usr/lib/syslinux/modules/bios/ldlinux.c32 $ISO_DIR/boot/isolinux/ 2>/dev/null || \
cp /usr/lib/ISOLINUX/ldlinux.c32 $ISO_DIR/boot/isolinux/ 2>/dev/null || true

# Create ISO
echo "Creating ISO image..."
genisoimage -r -J -b boot/isolinux/isolinux.bin -c boot/isolinux/boot.cat \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -o $BUILD_ROOT/iso/xsc-minimal-test.iso $ISO_DIR 2>&1 | grep -v "Warning: creating filesystem"

echo ""
echo "========================================"
echo "ISO BUILD COMPLETE"
echo "========================================"
echo "ISO: $BUILD_ROOT/iso/xsc-minimal-test.iso"
ls -lh $BUILD_ROOT/iso/xsc-minimal-test.iso
echo ""
echo "To test: qemu-system-x86_64 -cdrom iso/xsc-minimal-test.iso -m 2G -nographic"
