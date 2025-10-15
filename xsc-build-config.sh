#!/bin/bash
# XSC Build Configuration
# Source this file to set build flags for base or cfi-compat variants

# Build variant: "base" or "cfi-compat"
XSC_VARIANT="${XSC_VARIANT:-base}"

# Target architecture
XSC_ARCH="${XSC_ARCH:-x86_64}"

# Base hardening flags (always applied)
XSC_BASE_CFLAGS="-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=3 -fstack-clash-protection"
XSC_BASE_LDFLAGS="-Wl,-z,relro,-z,now"

# Architecture-specific hardening
case "$XSC_ARCH" in
    x86_64)
        if [ "$XSC_VARIANT" = "cfi-compat" ]; then
            XSC_HARDENING_CFLAGS="-fcf-protection=full"
            XSC_VARIANT_NAME="cfi-compat"
            echo "XSC Build: x86-64 CFI-Compat (CET enabled, CFI with JIT allowlist support)"
        else
            XSC_HARDENING_CFLAGS=""
            XSC_VARIANT_NAME="base"
            echo "XSC Build: x86-64 Base"
        fi
        XSC_TRIPLET="x86_64-xsc-linux-gnu"
        XSC_DEBIAN_ARCH="xsc-amd64${XSC_VARIANT_NAME:+-$XSC_VARIANT_NAME}"
        XSC_RPM_ARCH="x86_64-xsc${XSC_VARIANT_NAME:+-$XSC_VARIANT_NAME}"
        ;;

    aarch64)
        if [ "$XSC_VARIANT" = "cfi-compat" ]; then
            XSC_HARDENING_CFLAGS="-mbranch-protection=pac-ret"
            XSC_VARIANT_NAME="cfi-compat"
            echo "XSC Build: ARM64 CFI-Compat (PAC enabled, CFI with JIT allowlist support)"
        else
            XSC_HARDENING_CFLAGS=""
            XSC_VARIANT_NAME="base"
            echo "XSC Build: ARM64 Base"
        fi
        XSC_TRIPLET="aarch64-xsc-linux-gnu"
        XSC_DEBIAN_ARCH="xsc-arm64${XSC_VARIANT_NAME:+-$XSC_VARIANT_NAME}"
        XSC_RPM_ARCH="aarch64-xsc${XSC_VARIANT_NAME:+-$XSC_VARIANT_NAME}"
        ;;

    *)
        echo "Error: Unsupported architecture: $XSC_ARCH"
        exit 1
        ;;
esac

# Combine flags
export XSC_CFLAGS="$XSC_BASE_CFLAGS $XSC_HARDENING_CFLAGS"
export XSC_CXXFLAGS="$XSC_CFLAGS"
export XSC_LDFLAGS="$XSC_BASE_LDFLAGS"

# Export for use in build scripts
export XSC_VARIANT
export XSC_VARIANT_NAME
export XSC_ARCH
export XSC_TRIPLET
export XSC_DEBIAN_ARCH
export XSC_RPM_ARCH

echo "Triplet: $XSC_TRIPLET"
echo "CFLAGS: $XSC_CFLAGS"
echo "LDFLAGS: $XSC_LDFLAGS"
