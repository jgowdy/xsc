#!/bin/bash
set -e
export TMPDIR=/storage/icloud-backup/build/tmp

echo '=== Building XSC Toolchain ==='
echo 'Target: x86_64-xsc-linux-gnu'
echo ''

# Build on bx.ee with all cores
ssh bx.ee 'bash -s' << 'EOF'
set -e
export TMPDIR=/storage/icloud-backup/build/tmp
cd /storage/icloud-backup/build

# Setup
export TARGET=x86_64-xsc-linux-gnu
export PREFIX=/storage/icloud-backup/build/xsc-toolchain
export PATH=$PREFIX/bin:$PATH
export MAKEFLAGS="-j80"

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
rm -rf build-binutils
mkdir build-binutils
cd build-binutils

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
make ARCH=x86_64 INSTALL_HDR_PATH=$PREFIX/$TARGET/usr headers_install

# Stage 3: GCC (bootstrap)
echo ""
echo "=== Stage 3: Building GCC (bootstrap) ==="
cd /storage/icloud-backup/build
rm -rf build-gcc-bootstrap
mkdir build-gcc-bootstrap
cd build-gcc-bootstrap

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
    --disable-libquadmath

make $MAKEFLAGS all-gcc
make $MAKEFLAGS all-target-libgcc
make install-gcc
make install-target-libgcc

# Stage 4: Glibc (with XSC sysdeps - minimal for now)
echo ""
echo "=== Stage 4: Building Glibc ==="

# Create XSC sysdeps directory structure
mkdir -p /storage/icloud-backup/build/src/glibc-2.38/sysdeps/unix/sysv/linux/x86_64-xsc

# For now, use standard x86_64 syscalls - we'll implement XSC rings later
# Create a minimal configure fragment
cat > /storage/icloud-backup/build/src/glibc-2.38/sysdeps/unix/sysv/linux/x86_64-xsc/configure << 'GLIBC_EOF'
# XSC-specific configuration
# For now, inherits from x86_64
GLIBC_EOF

cat > /storage/icloud-backup/build/src/glibc-2.38/sysdeps/unix/sysv/linux/x86_64-xsc/Implies << 'GLIBC_EOF'
unix/sysv/linux/x86_64
x86_64
GLIBC_EOF

cd /storage/icloud-backup/build
rm -rf build-glibc
mkdir build-glibc
cd build-glibc

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
rm -rf build-gcc
mkdir build-gcc
cd build-gcc

../src/gcc-13.2.0/configure \
    --prefix=$PREFIX \
    --target=$TARGET \
    --enable-languages=c,c++ \
    --disable-multilib \
    --disable-nls \
    --with-sysroot=$PREFIX/$TARGET \
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
