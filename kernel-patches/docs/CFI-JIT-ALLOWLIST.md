# CFI JIT Allowlist System

## Overview

The CFI (Control-Flow Integrity) JIT Allowlist allows specific JIT engines to run with hardware CFI protections disabled. This is necessary because JIT engines generate code at runtime and cannot easily comply with hardware CFI requirements.

## The Problem: JITs vs Hardware CFI

### Intel CET (Control-flow Enforcement Technology)
- **Indirect Branch Tracking (IBT)**: Requires `ENDBR64` landing pads at indirect branch targets
- **Shadow Stack**: Hardware-enforced return address integrity
- **JIT Problem**: Runtime-generated code needs `ENDBR64` instructions at all indirect branch targets
  - Hard to retrofit into existing JIT compilers (V8, HotSpot, LuaJIT)
  - Performance overhead for JIT-compiled code
  - Some JITs use self-modifying code incompatible with shadow stack

### ARM PAC (Pointer Authentication Codes)
- **Return Address Signing**: Cryptographically sign return addresses
- **Pointer Authentication**: Sign/verify pointers before use
- **JIT Problem**: Runtime code generation conflicts with pointer signing
  - JITs dynamically create function pointers
  - Hard to integrate PAC signing into JIT code generators
  - Performance overhead

## Why Not Just Recompile JITs for CFI?

Unlike XSC (which is just a different syscall mechanism), **CFI is fundamentally incompatible with how JITs work**:

1. **XSC**: JIT can call `xsc_submit()` instead of `syscall()` → **Easy fix**
2. **CFI**: JIT needs to add ENDBR64/PAC to all runtime-generated code → **Hard/impossible**

This is why we need a permanent allowlist for CFI, while XSC allowlist would be temporary.

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                      Boot Process                             │
├──────────────────────────────────────────────────────────────┤
│ 1. Kernel loads initramfs                                    │
│ 2. cfi_allowlist_init() runs (subsys_initcall)              │
│ 3. Opens /etc/cfi/allowlist from initramfs                  │
│ 4. Loads JIT paths into kernel memory                        │
│ 5. Sets cfi_allowlist_active flag                           │
│ 6. __ro_after_init makes allowlist immutable                │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                    Process Execution                          │
├──────────────────────────────────────────────────────────────┤
│ 1. Process calls exec("/usr/bin/java")                      │
│ 2. cfi_allowlist_exec() checks binary against allowlist     │
│ 3. Sets task->cfi_mode:                                     │
│    - Allowlisted → CFI_MODE_DISABLED (CFI off for this JIT) │
│    - Not listed  → CFI_MODE_ENFORCED (CFI on, secure)       │
│ 4. Process setup code reads task->cfi_mode                  │
│ 5. Enables/disables CET/PAC based on mode                   │
│ 6. Process runs with appropriate CFI state                   │
└──────────────────────────────────────────────────────────────┘
```

## File Locations

### Kernel Code
- `/security/cfi_allowlist.c` - Loads allowlist and manages CFI modes
- `/include/cfi_allowlist.h` - API definitions
- `/include/linux/sched.h` - `task_struct.cfi_mode` field (if CONFIG_CFI_JIT_ALLOWLIST=y)

### Allowlist File
- `/etc/cfi/allowlist` - User-editable list of JIT binaries exempt from CFI

### Debian Integration
- `/usr/share/initramfs-tools/hooks/cfi-allowlist` - Copies allowlist to initramfs
- `/usr/sbin/cfi-allowlist-add` - Add JIT to allowlist
- `/usr/sbin/cfi-allowlist-remove` - Remove JIT from allowlist
- `/var/lib/dpkg/info/cfi-jit.triggers` - Trigger on allowlist changes
- `/var/lib/dpkg/info/cfi-jit.postinst` - Regenerate initramfs on trigger

## Allowlist File Format

File: `/etc/cfi/allowlist`

```
# CFI JIT Allowlist
#
# Binaries listed here run with hardware CFI (CET/PAC) DISABLED.
# Only add JIT engines that cannot comply with CFI requirements.
#
# One absolute path per line
# Lines starting with # are comments
# Empty lines are ignored

/usr/lib/jvm/java-17-openjdk/bin/java
/usr/bin/node
/usr/bin/luajit
```

## Configuration Options

### CONFIG_CFI_JIT_ALLOWLIST=y (JIT Allowlist Enabled)

**Use Case**: Systems that need to run JIT engines (Java, Node.js, LuaJIT)

**Behavior**:
- Binary allowlist checked once at `exec()` time
- CFI mode stored in `task_struct->cfi_mode`
- Allowlisted binaries run with CET/PAC disabled
- All other processes have FULL CFI enforcement

**Security**:
- `CFI_MODE_ENFORCED` = 0 (fail-secure: zeroing keeps you safe)
- `CFI_MODE_DISABLED` = 1 (allowlisted JITs only)
- Small attack surface: only explicitly allowlisted binaries
- Global `cfi_allowlist_active` flag prevents privilege escalation

**Performance**:
- Zero runtime overhead (mode checked once at exec)
- One `d_path()` call at exec for allowlist checking

### CONFIG_CFI_JIT_ALLOWLIST=n (Hard CFI Enforcement - RECOMMENDED)

**Use Case**: Maximum security systems where all software complies with CFI

**Behavior**:
- **ALL processes have CFI enforced, no exceptions**
- No mode field in task_struct (saves memory)
- No allowlist checking at exec time

**Security**:
- Maximum security - zero attack surface
- No allowlist to bypass
- No mode field to exploit

**Performance**:
- Zero overhead - no mode checks
- Saves sizeof(enum) bytes per task_struct

## Security Properties

### Immutable After Boot
- Allowlist loaded once during `subsys_initcall()`
- Memory protected by `__ro_after_init`
- Cannot be modified after kernel init
- Updates require initramfs regeneration + reboot

### Defense-in-Depth
1. **Global flag** (`cfi_allowlist_active`): Protected by `__ro_after_init`
2. **Per-task mode** (`task->cfi_mode`): Set at exec based on allowlist
3. **Process setup code**: Reads mode and configures CET/PAC accordingly

### Fail-Secure Design
- Empty allowlist = full CFI enforcement
- Missing allowlist file = same as empty
- Parse errors = ignore invalid entries
- Default mode (0) = CFI_MODE_ENFORCED (secure)

## Package Manager Integration

### For JIT Package Maintainers

#### postinst (package installation)
```bash
#!/bin/bash
set -e

case "$1" in
    configure)
        # Add JIT to CFI allowlist
        if [ -x /usr/sbin/cfi-allowlist-add ]; then
            cfi-allowlist-add /usr/lib/jvm/java-17-openjdk/bin/java || true
        fi
        ;;
esac
```

#### prerm (package removal)
```bash
#!/bin/bash
set -e

case "$1" in
    remove|upgrade|deconfigure)
        # Remove JIT from CFI allowlist
        if [ -x /usr/sbin/cfi-allowlist-remove ]; then
            cfi-allowlist-remove /usr/lib/jvm/java-17-openjdk/bin/java || true
        fi
        ;;
esac
```

## Usage

### Manual Allowlist Management

```bash
# Add JIT to allowlist
sudo cfi-allowlist-add /usr/bin/node

# Remove JIT from allowlist
sudo cfi-allowlist-remove /usr/bin/node

# Regenerate initramfs
sudo update-initramfs -u

# Reboot to apply changes
sudo reboot
```

### Check Boot Messages

```bash
# Check if allowlist loaded successfully
dmesg | grep cfi_allowlist

# Expected output (with JITs):
# [    0.123456] cfi_allowlist: Loading JIT allowlist from /etc/cfi/allowlist
# [    0.123457] cfi_allowlist: [0] /usr/lib/jvm/java-17-openjdk/bin/java (CFI disabled for this JIT)
# [    0.123458] cfi_allowlist: [1] /usr/bin/node (CFI disabled for this JIT)
# [    0.123459] cfi_allowlist: JIT allowlist ACTIVE (2 JIT engines with CFI disabled)
# [    0.123460] cfi_allowlist: All other processes have FULL CFI enforcement

# Expected output (empty):
# [    0.123456] cfi_allowlist: No allowlist file at /etc/cfi/allowlist (CFI enforced for all processes)
# [    0.123457] cfi_allowlist: JIT allowlist EMPTY - FULL CFI enforcement for all processes
```

### Verify Process CFI State

```bash
# Check if process has CFI enabled (implementation-specific)
# On Intel CET systems:
cat /proc/$PID/status | grep -i cet

# Expected for normal process:
# CET: shadow_stack indirect_branch_tracking

# Expected for allowlisted JIT:
# CET: disabled
```

## Comparison: CFI Allowlist vs XSC Allowlist

| Feature | CFI Allowlist | XSC Allowlist |
|---------|---------------|---------------|
| **Purpose** | Control hardware CFI (CET/PAC) | Control syscall mechanism |
| **Compatibility Issue** | **Real & Permanent** (JITs can't easily add ENDBR64/PAC) | Transitional (JITs can use xsc_submit()) |
| **Enforcement Point** | Process setup (exec time) | Syscall entry |
| **Long-term Need** | **Permanent** (some JITs may never support CFI) | Temporary (eventually all use XSC) |
| **Security Impact** | Disables CFI for specific processes | Allows direct syscalls for specific processes |
| **Performance Impact** | Zero runtime cost | ~7 cycles per syscall |

**Key Difference**: CFI allowlist addresses a **fundamental incompatibility**, while XSC allowlist would only be needed during a transition period.

## Recommendations

### For Distributions
1. **Default**: Empty allowlist (full CFI enforcement)
2. **Provide tools**: cfi-allowlist-add/remove for users
3. **Package integration**: Add hooks to JIT packages automatically
4. **Documentation**: Explain CFI security implications clearly

### For System Administrators
1. **Minimize allowlist**: Only add JITs that genuinely need it
2. **Audit regularly**: Review `/etc/cfi/allowlist` contents
3. **Test first**: Verify JITs actually fail with CFI before adding
4. **Monitor updates**: Some JITs may gain CFI support over time

### For Package Maintainers
1. **Test with CFI first**: Only add to allowlist if JIT actually breaks
2. **Use provided scripts**: cfi-allowlist-add/remove (don't modify file directly)
3. **Make it conditional**: Check if scripts exist before calling
4. **Document clearly**: Explain why your JIT needs CFI exemption
5. **Track upstream**: Monitor for CFI support in future JIT versions

## Known JITs Requiring CFI Exemption

### Confirmed to need allowlist:
- **OpenJDK HotSpot JIT** (Java): Generates code at runtime without ENDBR64
- **V8 JavaScript Engine** (Node.js, Chrome): JIT incompatible with IBT
- **LuaJIT**: Dynamic code generation conflicts with CET/PAC

### May work with CFI (test first):
- **WebAssembly runtimes** (wasmtime, wasmer): Some support CFI
- **PyPy** (Python JIT): Newer versions may support CFI
- **.NET CoreCLR** (C# JIT): Microsoft working on CET support

### Do NOT need allowlist:
- **CPython** (standard Python): No JIT, runs with CFI fine
- **Ruby (non-YJIT)**: Interpreted, no CFI issues
- **PHP (non-JIT)**: Standard interpreter works with CFI

## Future Enhancements

### Possible Improvements
- Signature verification for allowlist file
- Per-user allowlist granularity
- Runtime reloading with CAP_SYS_ADMIN
- Integration with audit logging
- SELinux/AppArmor policy integration

### Upstream JIT Fixes (Preferred Over Allowlist)
- Work with JIT projects to add CET/PAC support
- Provide tooling to help JITs emit ENDBR64
- Collaborate on performance-optimized CFI for JITs
- Gradually reduce allowlist as JITs gain CFI support

## References

- Intel CET Documentation: https://software.intel.com/content/www/us/en/develop/articles/technical-look-control-flow-enforcement-technology.html
- ARM PAC Documentation: https://developer.arm.com/documentation/100067/latest/
- V8 JIT + CET Discussion: https://bugs.chromium.org/p/v8/issues/detail?id=12797
