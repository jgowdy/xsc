#!/bin/bash
# Create XSC Offline APT Repository
#
# This script creates an offline Debian repository from XSC-compiled packages
# for inclusion in the XSC ISO

set -e

# Paths
PACKAGES_SOURCE="/storage/icloud-backup/build/xsc-full-native-build/debs"
REPO_DIR="/storage/icloud-backup/build/xsc-v7-iso/apt-repo"
REPO_CONF="$REPO_DIR/conf/distributions"

echo "=== Creating XSC Offline APT Repository ==="

# Check if we're on the server
if [ ! -d "$PACKAGES_SOURCE" ]; then
    echo "Error: Package source directory not found: $PACKAGES_SOURCE"
    echo "This script must run on bx.ee where packages are built"
    exit 1
fi

# Count packages
PKG_COUNT=$(find "$PACKAGES_SOURCE" -name "*.deb" | wc -l)
echo "Found $PKG_COUNT XSC packages"

# Create repository structure
echo "Creating repository structure..."
mkdir -p "$REPO_DIR"/{conf,dists/stable/main/binary-amd64,pool/main}

# Create distributions file
cat > "$REPO_CONF" << 'EOF'
Origin: XSC Project
Label: XSC Debian Repository
Suite: stable
Codename: xsc-v7
Version: 7.0
Architectures: amd64
Components: main
Description: XSC-compiled Debian packages for exception-less Linux
SignWith: no
EOF

echo "Repository configuration created"

# Copy packages to pool
echo "Copying packages to repository pool..."
find "$PACKAGES_SOURCE" -name "*.deb" -exec cp {} "$REPO_DIR/pool/main/" \;

echo "Copied $PKG_COUNT packages to pool"

# Check if reprepro is available
if ! command -v reprepro &> /dev/null; then
    echo "WARNING: reprepro not installed"
    echo "Installing required packages..."
    apt-get update && apt-get install -y reprepro
fi

# Build repository index
echo "Building repository index with reprepro..."
cd "$REPO_DIR"
reprepro includedeb stable pool/main/*.deb

# Create sources.list entry
cat > "$REPO_DIR/sources.list.xsc" << EOF
# XSC Offline Repository
# Copy this to /etc/apt/sources.list.d/xsc.list
deb [trusted=yes] file://$REPO_DIR stable main
EOF

# Create README
cat > "$REPO_DIR/README.md" << 'EOF'
# XSC Offline APT Repository

This is an offline Debian repository containing $PKG_COUNT packages compiled for XSC (eXtended Syscall).

## What's Different?

All packages in this repository are compiled for the `x86_64-xsc-linux-gnu` target:
- Syscalls use XSC ring mechanism instead of trap-based `syscall` instruction
- glibc linked with XSC syscall shim
- Compatible with XSC v7 kernel

## Usage

### Add Repository

```bash
# Copy sources list
sudo cp sources.list.xsc /etc/apt/sources.list.d/xsc.list

# Update package index
sudo apt update
```

### Install Packages

```bash
# Use xsc-apt wrapper for automatic JIT allowlist management
sudo xsc-apt install nodejs
sudo xsc-apt install openjdk-17-jdk

# Or use regular apt (manual allowlist management required)
sudo apt install bash coreutils
```

### Package Count

Total packages: $PKG_COUNT

### Repository Structure

```
apt-repo/
├── conf/
│   └── distributions  # Repository configuration
├── dists/
│   └── stable/
│       └── main/
│           └── binary-amd64/
│               ├── Packages      # Package index
│               └── Packages.gz   # Compressed index
├── pool/
│   └── main/
│       └── *.deb     # All package files
└── sources.list.xsc  # APT sources entry
```

### Offline Usage

Mount this directory or copy to target system:

```bash
# On target system
sudo mount /dev/cdrom /mnt
sudo cp /mnt/apt-repo/sources.list.xsc /etc/apt/sources.list.d/xsc.list
sudo apt update
sudo apt install <package>
```
EOF

echo ""
echo "=== Repository Created Successfully ==="
echo ""
echo "Location: $REPO_DIR"
echo "Packages: $PKG_COUNT"
echo ""
echo "To use on XSC system:"
echo "  1. Copy sources.list.xsc to /etc/apt/sources.list.d/xsc.list"
echo "  2. Run: sudo apt update"
echo "  3. Install packages: sudo xsc-apt install <package>"
echo ""
echo "To include in ISO:"
echo "  rsync -a $REPO_DIR /path/to/iso/rootfs/opt/xsc-repo"
echo ""
