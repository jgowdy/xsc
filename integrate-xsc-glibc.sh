#!/bin/bash
# Integrate XSC ring-based syscalls into glibc-2.38 source tree
set -e

echo "=== Integrating XSC Syscalls into glibc-2.38 ==="

# First, copy the xsc-glibc-syscalls.c implementation to the server
echo "Copying XSC implementation files to server..."
scp xsc-glibc-syscalls.c bx.ee:/storage/icloud-backup/build/

# Now create the comprehensive XSC sysdeps integration script
cat > /tmp/integrate_xsc_glibc.sh <<'EOFSCRIPT'
#!/bin/bash
set -e

GLIBC_SRC="/storage/icloud-backup/build/src/glibc-2.38"
XSC_IMPL="/storage/icloud-backup/build/xsc-glibc-syscalls.c"

echo "Creating XSC sysdeps for both x86_64 and aarch64..."

# Function to create XSC sysdeps for a target
create_xsc_sysdeps() {
    local TARGET=$1
    local SYSDEPS="$GLIBC_SRC/sysdeps/unix/sysv/linux/${TARGET}"

    echo "Creating sysdeps for $TARGET..."
    mkdir -p "$SYSDEPS"

    # Copy the main XSC implementation
    cp "$XSC_IMPL" "$SYSDEPS/"

    # Create Makefile
    cat > "$SYSDEPS/Makefile" <<'EOF'
# XSC-specific makefile rules

ifeq ($(subdir),misc)
sysdep_routines += xsc-glibc-syscalls
CFLAGS-xsc-glibc-syscalls.c = -fPIC -O2
endif

ifeq ($(subdir),posix)
# Process creation syscalls handled by XSC
endif

ifeq ($(subdir),nptl)
# Futex operations handled by XSC
endif

ifeq ($(subdir),io)
# Poll/select/epoll handled by XSC
endif
EOF

    # Create configure file
    cat > "$SYSDEPS/configure" <<'EOF'
# XSC-specific configuration
# Enables ring-based syscall mechanism for improved performance
GLIBC_PROVIDES dnl See aclocal.m4 in the top level source directory.
EOF
    chmod +x "$SYSDEPS/configure"

    # Create Implies file based on architecture
    case "$TARGET" in
        x86_64-xsc-linux-gnu)
            cat > "$SYSDEPS/Implies" <<'EOF'
unix/sysv/linux/x86_64
x86_64
EOF
            ;;
        aarch64-xsc-linux-gnu)
            cat > "$SYSDEPS/Implies" <<'EOF'
unix/sysv/linux/aarch64
aarch64
EOF
            ;;
    esac

    echo "Created XSC sysdeps in $SYSDEPS"
}

# Create sysdeps for both architectures
create_xsc_sysdeps "x86_64-xsc-linux-gnu"
create_xsc_sysdeps "aarch64-xsc-linux-gnu"

echo ""
echo "XSC integration complete!"
echo "glibc-2.38 now includes XSC ring-based syscall implementation"
ls -la "$GLIBC_SRC/sysdeps/unix/sysv/linux/x86_64-xsc-linux-gnu/"
ls -la "$GLIBC_SRC/sysdeps/unix/sysv/linux/aarch64-xsc-linux-gnu/"

EOFSCRIPT

# Execute on server
echo "Executing integration script on server..."
scp /tmp/integrate_xsc_glibc.sh bx.ee:/storage/icloud-backup/build/
ssh bx.ee "chmod +x /storage/icloud-backup/build/integrate_xsc_glibc.sh && /storage/icloud-backup/build/integrate_xsc_glibc.sh"

echo ""
echo "=== XSC glibc Integration Complete ==="
echo "You can now rebuild the toolchains to include XSC syscall support"
echo ""
echo "To rebuild all variants:"
echo "  XSC_ARCH=x86_64 XSC_VARIANT=base ./build-xsc-toolchain.sh"
echo "  XSC_ARCH=x86_64 XSC_VARIANT=cfi-strict ./build-xsc-toolchain.sh"
echo "  XSC_ARCH=aarch64 XSC_VARIANT=base ./build-xsc-toolchain.sh"
echo "  XSC_ARCH=aarch64 XSC_VARIANT=cfi-strict ./build-xsc-toolchain.sh"
