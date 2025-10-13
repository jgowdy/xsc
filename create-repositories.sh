#!/bin/bash
# Create package repositories for Debian and AlmaLinux

set -e

echo "Creating package repositories..."

ssh bx.ee 'bash -s' << 'REMOTE'
set -e
cd /storage/icloud-backup/repos/xsc

echo "Creating Debian repositories..."
for VARIANT in "" "-hardened"; do
    for ARCH in amd64 arm64; do
        DEBIAN_ARCH=xsc-$ARCH$VARIANT
        
        echo "  Creating $DEBIAN_ARCH repository..."
        mkdir -p debian/bookworm/dists/bookworm/main/binary-$DEBIAN_ARCH
        mkdir -p debian/bookworm/pool/main
        
        # Generate Packages file
        cd debian/bookworm
        dpkg-scanpackages pool/main /dev/null | gzip -9c > \
            dists/bookworm/main/binary-$DEBIAN_ARCH/Packages.gz
        
        # Generate Release file
        cd dists/bookworm
        apt-ftparchive release . > Release
        gpg --default-key xsc-archive@xsc-os.org -abs -o Release.gpg Release
        cd ../../..
    done
done

echo "Creating AlmaLinux repositories..."
for VARIANT in "" "-hardened"; do
    for ARCH in x86_64 aarch64; do
        RPM_ARCH=$ARCH-xsc$VARIANT
        
        echo "  Creating $RPM_ARCH repository..."
        mkdir -p alma/9/BaseOS/$RPM_ARCH/os/Packages
        mkdir -p alma/9/AppStream/$RPM_ARCH/os/Packages
        
        # Create repodata
        cd alma/9/BaseOS/$RPM_ARCH/os
        createrepo_c .
        cd ../../../../..
        
        cd alma/9/AppStream/$RPM_ARCH/os
        createrepo_c .
        cd ../../../../..
    done
done

echo "✓ All repositories created"

# Configure nginx
cat > /etc/nginx/sites-available/xsc-repos << 'NGINX'
server {
    listen 80;
    server_name bx.ee;
    root /storage/icloud-backup/repos/xsc;
    
    location /repos/xsc/ {
        autoindex on;
    }
}
NGINX

ln -sf /etc/nginx/sites-available/xsc-repos /etc/nginx/sites-enabled/
nginx -t && systemctl reload nginx

echo "✓ Repository hosting configured at http://bx.ee/repos/xsc/"
REMOTE
