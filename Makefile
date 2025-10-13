# XSC Build System Makefile
# Manages kernel, glibc, and distro builds with quilt patch management

SHELL := /bin/bash
export TMPDIR := /storage/icloud-backup/build/tmp
BUILD_ROOT := /storage/icloud-backup/build

KERNEL_VERSION := 6.1.0
GLIBC_VERSION := 2.36

# Kernel source
KERNEL_SRC := $(BUILD_ROOT)/linux-6.1
KERNEL_BZIMAGE := $(KERNEL_SRC)/arch/x86/boot/bzImage

# Glibc source
GLIBC_SRC := $(BUILD_ROOT)/glibc-$(GLIBC_VERSION)

# Output directories
ISO_DIR := $(BUILD_ROOT)/iso
DEBIAN_ISO := $(ISO_DIR)/debian/xsc-debian-amd64.iso
ALMA_ISO := $(ISO_DIR)/alma/xsc-alma-amd64.iso
ROCKY_ISO := $(ISO_DIR)/rocky/xsc-rocky-amd64.iso

# Patch directories
KERNEL_PATCHES := $(BUILD_ROOT)/patches/kernel
GLIBC_PATCHES := $(BUILD_ROOT)/patches/glibc

.PHONY: all clean help kernel glibc debian alma rocky isos

#========================================
# Top-level targets
# ========================================

all: kernel debian
	@echo "========================================="
	@echo "XSC Build Complete"
	@echo "========================================="
	@ls -lh $(ISO_DIR)/*/*.iso 2>/dev/null || true

help:
	@echo "XSC Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build kernel and Debian ISO (default)"
	@echo "  kernel    - Build XSC kernel"
	@echo "  glibc     - Build XSC glibc"
	@echo "  debian    - Build Debian ISO"
	@echo "  alma      - Build AlmaLinux ISO"
	@echo "  rocky     - Build Rocky Linux ISO"
	@echo "  isos      - Build all ISOs"
	@echo "  patches   - Generate quilt patch series"
	@echo "  clean     - Clean build artifacts"
	@echo ""
	@echo "Quick start:"
	@echo "  make kernel  # Build XSC kernel"
	@echo "  make debian  # Build Debian with XSC"

#========================================
# Kernel build
#========================================

kernel: $(KERNEL_BZIMAGE)

$(KERNEL_BZIMAGE):
	@echo "========================================="
	@echo "Building XSC Kernel"
	@echo "========================================="
	cd $(KERNEL_SRC) && \
	if ! grep -q "CONFIG_XSC=y" .config 2>/dev/null; then \
		make defconfig && \
		echo "CONFIG_XSC=y" >> .config; \
	fi && \
	make -j$$(nproc) bzImage modules 2>&1 | tee $(BUILD_ROOT)/logs/kernel-build-$$(date +%Y%m%d-%H%M%S).log
	@echo "Kernel built: $@"
	@ls -lh $@

#========================================
# Glibc build
#========================================

glibc:
	@echo "========================================="
	@echo "Building XSC Glibc"
	@echo "========================================="
	@echo "TODO: Implement glibc build with XSC patches"

#========================================
# Debian build
#========================================

debian: $(DEBIAN_ISO)

$(DEBIAN_ISO): kernel
	@echo "========================================="
	@echo "Building Debian ISO with XSC"
	@echo "========================================="
	@mkdir -p $(BUILD_ROOT)/debian-build/rootfs
	@mkdir -p $(ISO_DIR)/debian
	$(BUILD_ROOT)/xsc-cicd-build.sh debian
	@ls -lh $@

#========================================
# AlmaLinux build
#========================================

alma: $(ALMA_ISO)

$(ALMA_ISO): kernel
	@echo "========================================="
	@echo "Building AlmaLinux ISO with XSC"
	@echo "========================================="
	@echo "Requires mock or container tooling"
	@echo "Install: apt-get install mock"
	@mkdir -p $(ISO_DIR)/alma
	$(BUILD_ROOT)/xsc-cicd-build.sh alma

#========================================
# Rocky Linux build
#========================================

rocky: $(ROCKY_ISO)

$(ROCKY_ISO): kernel
	@echo "========================================="
	@echo "Building Rocky Linux ISO with XSC"
	@echo "========================================="
	@echo "Requires mock or container tooling"
	@mkdir -p $(ISO_DIR)/rocky
	$(BUILD_ROOT)/xsc-cicd-build.sh rocky

#========================================
# Build all ISOs
#========================================

isos: debian alma rocky
	@echo "========================================="
	@echo "All ISOs Built"
	@echo "========================================="
	@ls -lh $(ISO_DIR)/*/*.iso

#========================================
# Patch management
#========================================

patches: kernel-patches glibc-patches

kernel-patches:
	@echo "Generating kernel patch series..."
	@mkdir -p $(KERNEL_PATCHES)
	cd $(KERNEL_SRC) && \
	git diff drivers/xsc > $(KERNEL_PATCHES)/0001-xsc-driver.patch || \
	echo "No git repo - patches already applied"

glibc-patches:
	@echo "Generating glibc patch series..."
	@mkdir -p $(GLIBC_PATCHES)
	@echo "TODO: Generate glibc patches"

#========================================
# Clean
#========================================

clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_ROOT)/debian-build/rootfs
	rm -rf $(BUILD_ROOT)/alma-build/rootfs
	rm -rf $(BUILD_ROOT)/rocky-build/rootfs
	rm -f $(ISO_DIR)/*/*.iso
	cd $(KERNEL_SRC) && make clean || true

distclean: clean
	cd $(KERNEL_SRC) && make distclean || true

#========================================
# Status and info
#========================================

status:
	@echo "========================================="
	@echo "XSC Build Status"
	@echo "========================================="
	@echo "Build root: $(BUILD_ROOT)"
	@echo "TMPDIR: $(TMPDIR)"
	@echo ""
	@echo "Kernel source: $(KERNEL_SRC)"
	@test -f $(KERNEL_BZIMAGE) && echo "  Kernel: BUILT" || echo "  Kernel: NOT BUILT"
	@echo ""
	@echo "ISOs:"
	@test -f $(DEBIAN_ISO) && echo "  Debian: BUILT" || echo "  Debian: NOT BUILT"
	@test -f $(ALMA_ISO) && echo "  AlmaLinux: BUILT" || echo "  AlmaLinux: NOT BUILT"
	@test -f $(ROCKY_ISO) && echo "  Rocky: BUILT" || echo "  Rocky: NOT BUILT"
	@echo ""
	@echo "Disk usage:"
	@du -sh $(BUILD_ROOT) 2>/dev/null || true
