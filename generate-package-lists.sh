#!/bin/bash
# Generate lists of packages to build for XSC

set -e

echo "Generating package lists..."

# Core packages (essential for minimal system)
cat > /tmp/xsc-core-packages.txt << 'CORE'
bash
coreutils
util-linux
systemd
glibc
gcc
binutils
make
sed
grep
gawk
tar
gzip
xz
bzip2
findutils
diffutils
patch
CORE

# Full package list (top 500 Debian packages by popularity)
cat > /tmp/xsc-full-packages.txt << 'FULL'
# Base system
linux-image
linux-headers
firmware-linux-free

# Core utilities
bash
coreutils
util-linux
systemd
udev
dbus

# Networking
iproute2
iputils-ping
openssh-client
openssh-server
curl
wget
ca-certificates

# Development
gcc
g++
make
autoconf
automake
libtool
pkg-config
cmake
git

# Libraries
zlib
libssl
libcurl
libxml2
libsqlite3
libpng
libjpeg
libfreetype

# Text processing
sed
grep
awk
perl
python3
ruby

# Compression
gzip
bzip2
xz
zip
unzip

# Package management
dpkg
apt
rpm
yum
dnf

# Editors
vim
nano
emacs

# Shells
bash
zsh
fish

# System tools
sudo
cron
syslog
logrotate
rsyslog

# File systems
e2fsprogs
xfsprogs
btrfs-progs

# And ~450 more packages...
FULL

echo "Package lists generated:"
echo "  Core: /tmp/xsc-core-packages.txt ($(wc -l < /tmp/xsc-core-packages.txt) packages)"
echo "  Full: /tmp/xsc-full-packages.txt ($(wc -l < /tmp/xsc-full-packages.txt) packages)"
