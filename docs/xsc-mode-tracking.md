# XSC Mode Tracking

## Overview

XSC provides an **opt-in** ring-based syscall interface. Processes that want to use XSC open `/dev/xsc` and transition to XSC mode. Once in XSC mode, direct syscalls are denied to ensure the process uses rings consistently.

## Core Principle

**XSC is optional. Processes explicitly opt-in by opening `/dev/xsc`.**

## Syscall Modes

```c
enum xsc_syscall_mode {
    XSC_MODE_NORMAL = 0,   // Default: regular syscalls work
    XSC_MODE_XSC,          // Opted-in: must use XSC rings
};
```

## State Machine

```
NORMAL:  Default state for all processes
    ↓ (one-way transition when opening /dev/xsc)
    → XSC (permanent)

XSC:     Using XSC rings
    ↓ (no transitions allowed)
    [stays XSC forever]
```

## Mode Lifecycle

```
Process starts
    ↓
XSC_MODE_NORMAL (default)
    ↓
    ↓ (Process opens /dev/xsc)
    ↓
XSC_MODE_XSC (permanent)
    ↓
All future direct syscalls: DENIED
```

## Enforcement Points

### 1. At Process Start

All processes start in `XSC_MODE_NORMAL`:

```c
void xsc_mode_init_task(struct task_struct *task)
{
    task->xsc_syscall_mode = XSC_MODE_NORMAL;
}
```

### 2. When Opening /dev/xsc (`drivers/xsc/xsc_core.c`)

```c
static int xsc_open(struct inode *inode, struct file *file)
{
    /* Enter XSC mode (one-way transition) */
    ret = xsc_enter_mode(current);
    if (ret)
        return ret;

    /* From this point: all direct syscalls are DENIED */
    // ... initialize XSC rings ...
}
```

### 3. At Syscall Entry (`arch/x86/entry/entry_64.S`)

```asm
ENTRY(entry_SYSCALL_64)
    PUSH_REGS

    /* Check if process is in XSC mode */
    movq    %gs:current_task, %rdi
    movl    TASK_xsc_syscall_mode(%rdi), %eax

    /* XSC_MODE_NORMAL (0): allow syscall */
    testl   %eax, %eax
    jz      .Lxsc_allowed

    /* XSC_MODE_XSC (1): deny syscall */
    jmp     .Lxsc_denied

.Lxsc_allowed:
    /* Continue with normal syscall */
```

**Performance: ~5 cycles** (single integer test + branch)

### 4. On Fork

Child inherits parent's mode:

```c
void xsc_mode_fork(struct task_struct *parent, struct task_struct *child)
{
    child->xsc_syscall_mode = parent->xsc_syscall_mode;
    /* Child does NOT inherit xsc_ctx - that's per-process */
}
```

### 5. On Exec

Mode is reset to NORMAL, except if already in XSC mode:

```c
void xsc_mode_exec(struct task_struct *task)
{
    /* If not using XSC, reset to NORMAL */
    if (task->xsc_syscall_mode != XSC_MODE_XSC) {
        task->xsc_syscall_mode = XSC_MODE_NORMAL;
    }
    /* XSC processes stay in XSC mode */
}
```

## Example Scenarios

### Scenario 1: Normal Application (No XSC)

```
1. Process starts: XSC_MODE_NORMAL
2. Process makes syscalls: all work normally
3. Process forks: child inherits NORMAL mode
4. Process execs: still NORMAL mode
5. Everything works exactly as it did before XSC existed
```

### Scenario 2: XSC Application

```
1. Process starts: XSC_MODE_NORMAL
2. Process opens /dev/xsc
3. xsc_open() transitions: NORMAL → XSC
4. Process now uses XSC rings for all syscalls
5. Any direct syscall attempt: DENIED (-ENOSYS, violation logged)
6. Process forks: child inherits XSC mode (but not XSC context)
7. Child must also open /dev/xsc to use XSC rings
```

### Scenario 3: XSC Process Tries to Bypass

```
Attacker gains code execution in XSC process:
1. Attacker tries: syscall(__NR_execve, "/bin/sh", ...)
2. Syscall entry detects XSC_MODE_XSC
3. → DENIED (-ENOSYS)
4. → xsc_mode_violation() logs attempt
5. Attacker is stuck - cannot execute shell
```

**Attacker cannot:**
- Use direct syscalls (mode flag enforced in kernel)
- Change mode flag (it's in kernel memory)
- Close `/dev/xsc` to escape (mode is permanent)
- Fork to escape (child inherits XSC mode)

## Security Properties

### 1. Opt-In Model

- XSC is completely optional
- Applications explicitly choose to use it
- No compatibility issues with existing software

### 2. One-Way Transition

- NORMAL → XSC is permanent
- Cannot downgrade back to NORMAL
- Prevents bypass attempts

### 3. Per-Process Enforcement

```c
struct task_struct {
    enum xsc_syscall_mode xsc_syscall_mode;  // Per-process mode
    struct xsc_ctx *xsc_ctx;                 // Per-process XSC context
};
```

### 4. Consistency Guarantee

Once a process enters XSC mode:
- **All** syscalls must go through XSC rings
- Direct syscalls are denied
- This ensures consistent behavior and prevents bugs

## Performance

### Syscall Entry Overhead

**Without XSC check:**
```
syscall → kernel → dispatch (~100 cycles)
```

**With XSC check:**
```
syscall → kernel → mode check (~5 cycles) → dispatch (~100 cycles)
Total overhead: ~5% additional
```

### Why This Is Acceptable

1. **Negligible overhead**: 5 cycles out of 100+ is noise
2. **Opt-in only**: Only processes using XSC see any change
3. **Enables optimization**: XSC processes get ring benefits (batching, async)

## Implementation Files

```
kernel-patches/
├── include/
│   └── xsc_mode.h                   # Mode enum, API declarations
├── include/linux/
│   └── sched-xsc.patch             # Add mode field to task_struct
├── drivers/xsc/
│   ├── xsc_mode.c                  # Mode tracking implementation
│   └── xsc_core.c                  # Mode transition when opening /dev/xsc
└── arch/x86/entry/
    └── entry_64-xsc.patch          # Fast mode check in syscall entry
```

## Kernel Messages

When process enters XSC mode:
```
[    2.345] xsc: Process 1234 (myapp) entered XSC mode (direct syscalls now DENIED)
```

When violation occurs:
```
[    3.456] xsc: Process 1234 (myapp) in XSC mode attempted direct syscall 59
```

## Comparison With Other Enforcement Models

### CET/PAC (Shadow Stack, Pointer Authentication)

- **Needs allowlist**: Old binaries crash without support
- **Binary-wide**: Either enforced or not
- **Gradual migration**: Allow old binaries until recompiled

### XSC (This Implementation)

- **No allowlist needed**: Completely opt-in
- **Process-driven**: Each process decides independently
- **No migration needed**: Works alongside existing syscalls

## Benefits

1. **Zero Breaking Changes**: Existing software unchanged
2. **Simple Model**: No complex allowlist management
3. **Clear Opt-In**: Applications explicitly choose XSC
4. **Consistency**: Once opted-in, cannot bypass
5. **Minimal Overhead**: ~5 cycles per syscall entry

## Use Cases

### Performance Optimization

Applications that benefit from XSC:
- High-frequency syscall workloads (databases, web servers)
- Batch operations (multiple syscalls per user request)
- Async I/O patterns (submit many, wait for completions)

### Security Hardening

XSC provides single controlled entry point:
- Easier to audit (one submission path vs 300+ syscalls)
- Reduces attack surface (ring validation vs syscall argument parsing)
- Enables advanced monitoring (all ops go through rings)

## Summary

XSC mode tracking provides:
- ✅ **Opt-in model** (no breaking changes)
- ✅ **Simple implementation** (two modes, one transition)
- ✅ **Fast enforcement** (~5 cycles overhead)
- ✅ **Strong consistency** (XSC mode is permanent)
- ✅ **No allowlist complexity** (unlike CET/PAC)

**XSC is a performance and security feature, not a mandatory system change.**
