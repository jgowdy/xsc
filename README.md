# XSC OS

**eXtended SysCall Operating System**

A complete Linux distribution using ring-based syscalls with mandatory hardware control-flow integrity enforcement.

## What is XSC?

XSC replaces traditional CPU trap-based system calls (`syscall`, `sysenter`, `int 0x80`) with ring-buffer submission/completion queues, enabling:

- **Asynchronous syscall batching** - Submit multiple operations at once
- **Reduced context switches** - Kernel polls rings instead of trapping on every call
- **Hardware CFI enforcement** - CFI-Compat variant requires Intel CET or ARM PAC
- **Full Linux compatibility** - Source-compatible with RHEL 9 / Enterprise Linux

## Two Variants

### Base Variant
- Works on all x86-64 and ARM64 CPUs
- Full RHEL 9 API compatibility
- Standard hardening (stack protector, FORTIFY_SOURCE, RELRO)
- Ring-based syscalls for performance

### CFI-Compat Variant
- **Requires hardware CFI support**:
  - x86-64: Intel CET (Tiger Lake+, Zen 3+)
  - ARM64: PAC (ARMv8.3-A+, M1+, Graviton3+)
- Same API compatibility as base
- Mandatory control-flow integrity enforcement (hard CFI, no allowlist)
- All security features of base variant

## Architecture Support

| Architecture | Base | CFI-Compat | Triplet |
|-------------|------|------------|---------|
| x86-64 | ✅ | ✅ (CET) | `x86_64-xsc-linux-gnu` |
| ARM64 | ✅ | ✅ (PAC) | `aarch64-xsc-linux-gnu` |
| x32 | 📋 Planned | 📋 Planned | `x86_64-xsc-linux-gnux32` |

## Installation

Download ISOs from [Releases](https://github.com/jgowdy/xsc-os/releases):

**Debian-based:**
- `xsc-debian-bookworm-netinstall.iso` - Network installer
- `xsc-debian-bookworm-archive.iso` - Full offline installer

**AlmaLinux-based:**
- `xsc-almalinux-9-netinstall.iso` - Network installer  
- `xsc-almalinux-9-archive.iso` - Full offline installer

## Package Repositories

Add XSC repositories to your system:

### Debian/Ubuntu

```bash
# Base variant
echo "deb http://repos.xsc-os.org/debian bookworm main" | sudo tee /etc/apt/sources.list.d/xsc.list
curl -fsSL http://repos.xsc-os.org/debian/key.gpg | sudo apt-key add -

# CFI-Compat variant (CET-enabled CPUs only)
echo "deb http://repos.xsc-os.org/debian bookworm main" | sudo tee /etc/apt/sources.list.d/xsc-cfi-compat.list
```

### AlmaLinux/RHEL

```bash
# Base variant
cat > /etc/yum.repos.d/xsc.repo <<REPO
[xsc-base]
name=XSC Base
baseurl=http://repos.xsc-os.org/alma/9/BaseOS/x86_64-xsc/os/
enabled=1
gpgcheck=1
REPO

# CFI-Compat variant (CET-enabled CPUs only)
cat > /etc/yum.repos.d/xsc-cfi-compat.repo <<REPO
[xsc-cfi-compat]
name=XSC CFI-Compat
baseurl=http://repos.xsc-os.org/alma/9/BaseOS/x86_64-xsc-cfi-compat/os/
enabled=1
gpgcheck=1
REPO
```

## Building from Source

### Prerequisites

- 80+ core build server recommended
- 500GB free disk space
- Debian or AlmaLinux host

### Build Complete OS

```bash
git clone https://github.com/jgowdy/xsc-os.git
cd xsc-os
./build-everything.sh
```

This builds:
- All 4 toolchain variants (x86_64 + aarch64, base + cfi-compat)
- ~2,600 packages per architecture
- Debian repository with all packages
- Full bootable ISO

**Build time**: 48-72 hours on 80 cores

**Current build status**: See [CLAUDE.md](CLAUDE.md) for real-time progress

### Build Single Toolchain

```bash
# x86-64 base
export XSC_ARCH=x86_64 XSC_VARIANT=base
./build-xsc-toolchain.sh

# x86-64 cfi-compat (with CET, hard CFI enforcement)
export XSC_ARCH=x86_64 XSC_VARIANT=cfi-compat
./build-xsc-toolchain.sh
```

**Toolchain build time**: 3-4 hours on 80 cores

## Documentation

- [ABI Specification](XSC-ABI-SPEC.md) - Complete technical specification
- [Project Plan](PROJECT_PLAN.md) - Development roadmap
- [Status](XSC-STATUS.md) - Current build status

## Academic Foundation

XSC builds upon the FlexSC research from the University of Toronto:

> Livio Soares and Michael Stumm. 2010. **FlexSC: Flexible System Call Scheduling with Exception-Less System Calls.** In *Proceedings of the 9th USENIX Conference on Operating Systems Design and Implementation* (OSDI'10). USENIX Association, USA, 33–46.
>
> Paper: https://www.usenix.org/legacy/event/osdi10/tech/full_papers/Soares.pdf

The original FlexSC demonstrated that exception-less system calls using ring buffers could significantly improve performance by reducing CPU mode transitions and enabling better batching. XSC extends this concept with:

- Production implementation for modern Linux (6.1+)
- Hardware control-flow integrity enforcement
- Complete distribution infrastructure
- Support for modern CPU security features (CET, PAC)

## History & Motivation

XSC was created by Jay Gowdy to bring hardware-enforced control-flow integrity to production Linux systems. Early work on shadow stack concepts by Gowdy (as Jeremiah Gowdy) predates modern CPU implementations by over a decade. With Intel CET and ARM PAC now available in production hardware, XSC provides a complete platform to leverage these security features.

The ring-based syscall mechanism serves dual purposes:
1. Performance improvements through batching and reduced context switches
2. Natural enforcement point for hardware CFI validation

## License

- **Kernel components**: GPL v2 (inherits from Linux kernel)
- **Userland components**: MIT License
- **Build scripts**: MIT License

See [LICENSE](LICENSE) for details.

## Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Author

**Jay Gowdy**  
Senior Principal Architect, GoDaddy  
https://github.com/jgowdy

This is a personal research project and is not affiliated with or endorsed by GoDaddy.

## Contact

- GitHub Issues: https://github.com/jgowdy/xsc-os/issues
- Discussions: https://github.com/jgowdy/xsc-os/discussions

---

**XSC OS** - Ring-based syscalls with mandatory hardware CFI
