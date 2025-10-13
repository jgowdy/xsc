#!/bin/bash
set -e

# Source build configuration
source "$(dirname "$0")/xsc-build-config.sh"

echo '=== Building XSC Toolchain ==='
echo "Target: $XSC_TRIPLET"
echo "Variant: $XSC_VARIANT_NAME"
echo ''

# Build on bx.ee with all cores
# Use sed to substitute variables into the heredoc
sed -e "s|TARGET_PLACEHOLDER|$XSC_TRIPLET|g" \
    -e "s|PREFIX_PLACEHOLDER|/storage/icloud-backup/build/xsc-toolchain-${XSC_ARCH}-${XSC_VARIANT_NAME}|g" \
    -e "s|MAKEFLAGS_PLACEHOLDER|${MAKEFLAGS:--j80}|g" \
    -e "s|ARCH_PLACEHOLDER|${XSC_ARCH}|g" \
    -e "s|VARIANT_PLACEHOLDER|${XSC_VARIANT_NAME}|g" \
    << 'EOF' | ssh bx.ee "bash -s"
set -e
export TMPDIR=/storage/icloud-backup/build/tmp
cd /storage/icloud-backup/build

# Setup
export TARGET=TARGET_PLACEHOLDER
export PREFIX=PREFIX_PLACEHOLDER
export PATH=$PREFIX/bin:$PATH
export MAKEFLAGS="MAKEFLAGS_PLACEHOLDER"

# Determine Linux ARCH for headers
case "ARCH_PLACEHOLDER" in
    x86_64) LINUX_ARCH=x86_64 ;;
    aarch64) LINUX_ARCH=arm64 ;;
    *) echo "Unsupported arch: ARCH_PLACEHOLDER"; exit 1 ;;
esac

mkdir -p src xsc-toolchain

# Download sources if needed
cd src
if [ ! -d binutils-2.41 ]; then
    echo "Downloading binutils..."
    wget -q https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.xz
    tar xf binutils-2.41.tar.xz
fi

if [ ! -d gcc-13.2.0 ]; then
    echo "Downloading gcc..."
    wget -q https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
    tar xf gcc-13.2.0.tar.xz
    cd gcc-13.2.0
    ./contrib/download_prerequisites
    cd ..
fi

if [ ! -d glibc-2.38 ]; then
    echo "Downloading glibc..."
    wget -q https://ftp.gnu.org/gnu/glibc/glibc-2.38.tar.xz
    tar xf glibc-2.38.tar.xz
fi

# Stage 1: Binutils
echo ""
echo "=== Stage 1: Building Binutils ==="
cd /storage/icloud-backup/build
rm -rf build-binutils-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER
mkdir build-binutils-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER
cd build-binutils-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER

../src/binutils-2.41/configure \
    --prefix=$PREFIX \
    --target=$TARGET \
    --disable-nls \
    --disable-werror \
    --with-sysroot=$PREFIX/$TARGET

make $MAKEFLAGS
make install

# Stage 2: Linux Headers
echo ""
echo "=== Stage 2: Installing Linux Headers ==="
cd /storage/icloud-backup/build/linux-6.1
make ARCH=$LINUX_ARCH INSTALL_HDR_PATH=$PREFIX/$TARGET/usr headers_install

# Stage 3: GCC (bootstrap)
echo ""
echo "=== Stage 3: Building GCC (bootstrap) ==="
cd /storage/icloud-backup/build
rm -rf build-gcc-bootstrap-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER
mkdir build-gcc-bootstrap-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER
cd build-gcc-bootstrap-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER

../src/gcc-13.2.0/configure \
    --prefix=$PREFIX \
    --target=$TARGET \
    --enable-languages=c,c++ \
    --disable-multilib \
    --disable-nls \
    --with-sysroot=$PREFIX/$TARGET \
    --with-newlib \
    --without-headers \
    --disable-shared \
    --disable-threads \
    --disable-libssp \
    --disable-libgomp \
    --disable-libquadmath \
    --disable-libgcov

make $MAKEFLAGS all-gcc
make install-gcc

# Stage 4: Glibc (with XSC sysdeps - minimal for now)
echo ""
echo "=== Stage 4: Building Glibc ==="

# Create XSC sysdeps directory structure for the appropriate architecture
mkdir -p /storage/icloud-backup/build/src/glibc-2.38/sysdeps/unix/sysv/linux/${TARGET}

# For now, use standard syscalls - we'll implement XSC rings later
# Create a minimal configure fragment
cat > /storage/icloud-backup/build/src/glibc-2.38/sysdeps/unix/sysv/linux/${TARGET}/configure << 'GLIBC_EOF'
# XSC-specific configuration
# For now, inherits from parent architecture
GLIBC_EOF

# Set Implies based on architecture
case "$TARGET" in
    x86_64-xsc-linux-gnu)
        cat > /storage/icloud-backup/build/src/glibc-2.38/sysdeps/unix/sysv/linux/${TARGET}/Implies << 'GLIBC_EOF'
unix/sysv/linux/x86_64
x86_64
GLIBC_EOF
        ;;
    aarch64-xsc-linux-gnu)
        cat > /storage/icloud-backup/build/src/glibc-2.38/sysdeps/unix/sysv/linux/${TARGET}/Implies << 'GLIBC_EOF'
unix/sysv/linux/aarch64
aarch64
GLIBC_EOF
        ;;
esac

cd /storage/icloud-backup/build
rm -rf build-glibc-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER
mkdir build-glibc-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER
cd build-glibc-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER

../src/glibc-2.38/configure \
    --prefix=/usr \
    --host=$TARGET \
    --with-headers=$PREFIX/$TARGET/usr/include \
    --disable-werror \
    --enable-kernel=6.1.0 \
    libc_cv_forced_unwind=yes

make $MAKEFLAGS
make install DESTDIR=$PREFIX/$TARGET

# Stage 5: Full GCC
echo ""
echo "=== Stage 5: Building GCC (full) ==="
cd /storage/icloud-backup/build
rm -rf build-gcc-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER
mkdir build-gcc-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER
cd build-gcc-ARCH_PLACEHOLDER-VARIANT_PLACEHOLDER

../src/gcc-13.2.0/configure \
    --prefix=$PREFIX \
    --target=$TARGET \
    --enable-languages=c,c++ \
    --disable-multilib \
    --disable-nls \
    --with-sysroot=$PREFIX/$TARGET \
    --with-build-sysroot=$PREFIX/$TARGET \
    --with-native-system-header-dir=/usr/include

make $MAKEFLAGS
make install

echo ""
echo "=== XSC Toolchain Complete ==="
echo "Location: $PREFIX"
echo "Target: $TARGET"
$PREFIX/bin/$TARGET-gcc --version

EOF

echo ""
echo "Toolchain build complete"
