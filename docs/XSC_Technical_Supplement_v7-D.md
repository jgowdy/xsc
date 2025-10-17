# XSC Technical & Detailed Supplement v7-D  
**Engineering Implementation, Transparency Enhancements, and Rationale**

---

## 1. Purpose and Scope
This unified supplement combines and extends the prior Technical (-T) and Detailed (-D) documents.  
It captures all technical changes since v6, including the complete Linux API implementation, the strict default-off policy for the null doorbell syscall, and detailed transparency behavior for audit, seccomp, ptrace, and BPF.

---

## 2. Kernel Implementation Overview

### 2.1 Core Components
```
drivers/xsc/
 ├── xsc_core.c        # /dev/xsc device, SR|CR|CTRL mmap, worker threads
 ├── xsc_consume_*.c   # category-specific handlers: fs, net, timer, sync
 ├── xsc_exec.c        # XPROC_SPAWN/EXEC handling
 ├── xsc_trace.c       # tracepoints for audit/strace/BPF
 ├── xsc_seccomp.c     # consume-time seccomp filters
 ├── xsc_audit.c       # audit event emission (AUDIT_XSC_*)
arch/x86/vdso/xsc_tramp.S
arch/arm64/vdso/xsc_tramp.S
```

### 2.2 Worker lifecycle
1. Worker dequeues SQE → **seccomp filters run** → emits `trace_xsc_sys_enter` & `AUDIT_XSC_SUBMIT`.  
2. Executes kernel helper (`vfs_read`, `sendmsg`, etc.).  
3. Emits `trace_xsc_sys_exit` & `AUDIT_XSC_RESULT`.  
4. Writes CQE → wakes submitter.

Observability tools see the same sequence they would on a traditional kernel.

---

## 3. glibc Integration and Syscall Shim

- The `syscall()` symbol is implemented as a ring enqueue/wait operation instead of an actual trap.  
- Metadata-driven translation table routes each syscall number to the correct XSC consumer class.  
- Certain calls are optimized (e.g., `futex` → `XSYNC_WAIT/WAKE`), while others pass through unmodified.  
- vDSO handles `clock_gettime`, `gettimeofday`, `getcpu`.  
- Inline assembly issuing `syscall`/`svc` instructions is forbidden by toolchain plugins and verified post-link.

---

## 4. Toolchain Enforcement

- GCC/Clang plugins scan inline asm for forbidden mnemonics (`syscall`, `svc`).  
- CI performs post-link opcode scans (x86: `0F05`, arm64: `D4 00 00 01`).  
- Offenders fail build.  
- Optional pragma `#pragma XSC_ALLOW_TRAP` available for test-only binaries.

Purpose: guarantee all binaries respect the XSC ABI layering—no user-mode traps.

---

## 5. Observability and Transparency

### 5.1 Kernel events
- **Tracepoints:** `xsc_sys_enter/exit` mirror `sys_enter/exit`.  
- **Audit:** `AUDIT_XSC_SUBMIT/RESULT` use existing netlink interface.  
- **Ptrace:** `PTRACE_EVENT_XSC_SYSCALL` provides debugger stops.  
- **Perf/BPF:** native `xsc_sys_*` tracepoints with compatibility aliases for `syscalls:sys_enter_*` / `sys_exit_*`.

### 5.2 eBPF compatibility
Existing BPF tools attach seamlessly via alias events.  
Example mapping:  
| Legacy Attach | XSC Equivalent |  
|----------------|----------------|  
| `tracepoint:sys_enter_openat` | `tracepoint:xsc:xsc_sys_enter` (filtered by `nr`) |  
| `kprobe:__x64_sys_write` | `fentry:xsc_dispatch_fs()` |

### 5.3 Seccomp, Audit, Ptrace
- Seccomp filters executed at dequeue; all `ALLOW/KILL/ERRNO/TRAP/USER_NOTIF` actions preserved.  
- Audit records identical to `AUDIT_SYSCALL`.  
- ptrace syscall-stops preserved through synthetic event delivery.

---

## 6. Hardening and Mitigation Policy

### 6.1 Always-on (XSC default)
| Feature | Purpose | Overhead | Compatibility |
|----------|----------|-----------|----------------|
| RANDKSTACK | Adds stack entropy; mitigates leaks | <1 % | Transparent |
| HARDENED_USERCOPY | Bounds-checks copy_to/from_user | <1 % | Transparent |
| Seccomp on consume | Policy parity | negligible | Transparent |
| Audit/trace parity | Observability parity | negligible | Transparent |

### 6.2 Optional (XSC-CET build)
| Feature | Purpose | Overhead | Compatibility |
|----------|----------|-----------|----------------|
| CET (IBT+SHSTK)/BTI+PAC-ret | Hardware CFI | 2–5 % | Requires CFI-aware JITs |
| MDWE/W^X | Strict RW→RX policy | negligible | May block legacy JITs |
| RANDSTRUCT, STACKLEAK, kCFI | Kernel memory safety | small | Kernel-only |

---

## 7. Hypervisor Integration and Doorbell Design


### 7.1 Unified Adaptive Polling Subsystem (Fundamental and Always-On)

Adaptive polling is a **core, permanent subsystem** within XSC. It operates under all notification modes—hardware, virtual, or null doorbell—and dynamically adjusts timing based on observed reliability and workload activity.  
This unified approach replaces the former split between adaptive polling and watchdog; polling is never disabled.

**Behavior Model**
1. **Startup (fast mode):** Workers begin with standard sub-ms intervals (**≈100 µs active window, 100 µs idle tick**) for immediate responsiveness while doorbells prove reliability.  
2. **Trust convergence:** As doorbells fire consistently without misses, the polling tick **backs off** gradually (→ 1 ms → 5–10 ms).  
3. **Steady-state (watchdog):** When reliability is established, polling transitions to long deferrable timers (**5–10 ms**, or **100–500 ms** on tickless/idle hosts).  
4. **Anomaly detection:** Any missed doorbell or queue growth immediately triggers a **snap back** to fast mode (100 µs) for rapid recovery.  

| Phase | Active Window | Idle Tick | Timer Type | Behavior |
|-------|----------------|------------|-------------|-----------|
| Startup (no trust yet) | ~100 µs | ~100 µs | **hrtimer** | Immediate responsiveness before doorbell stabilization |
| Stabilization | ~100 µs | ~1 ms | **hrtimer** | Doorbell proving reliable |
| Steady-state watchdog | ~100 µs | 5–10 ms | **deferrable timer** | Integrity/liveness check; tickless safe |
| Idle/tickless extension | ~100 µs | 100–500 ms | **deferrable timer** | Extended interval for power efficiency |
| Doorbell anomaly | ~100 µs | resets to ~100 µs | **hrtimer** | Rapid recovery to fast polling |

**Tickless / Deferrable Timer Mode.** On tickless or highly idle hosts, workers arm **deferrable timers** so the watchdog does not wake sleeping CPUs. These timers fire only when the CPU wakes for another reason (interrupt, IPI, or workload event), preserving NO_HZ power savings.  
During quiescence, the subsystem extends its interval automatically (default **100–500 ms**, tunable). When activity resumes, polling frequency shortens to **5–10 ms**, or to sub-ms in fast mode.  

**Timer Selection**
- **Fast / precise (<1 ms):** `hrtimer` (high-resolution).  
- **Watchdog / tickless (>5 ms):** deferrable `timer_list` or deferrable delayed work.  
- **Elastic adjustment:** automatic based on `doorbell_success_count / doorbell_miss_count` ratio.

**Tunables & Telemetry**
- `/sys/kernel/xsc/idle_tick_ns` — current idle tick interval (ns).  
- `/sys/kernel/xsc/deferrable_mode` — boolean toggle for deferrable timers.  
- `/sys/kernel/xsc/doorbell_miss` — per-shard miss counter.  
- `/sys/kernel/xsc/adaptive_tick_ns` — current adaptive interval.  
- `/proc/<pid>/xsc/metrics` — per-process counters for doorbell success/miss.

Adaptive polling is fundamental to XSC’s design: it provides liveness, trust convergence, and self-healing in all environments without compromising tickless operation or power efficiency.

## 8. Full Linux API Implementation

### 8.1 Policy
XSC maintains **100 % Linux API equivalence**—identical syscall semantics, errno codes, blocking, signals, namespace and cgroup behavior.  
All system calls present in mainline Linux are implemented or pass-through translated.

### 8.2 Implementation classes
| Class | Examples | Approach |
|--------|-----------|-----------|
| Process & namespaces | `clone3`, `setns`, `unshare`, pidfds | Direct mapping; new tasks inherit ns/cred like Linux |
| Mount & FS | `open_tree`, `move_mount`, `fsopen`, `fsmount`, `pivot_root` | Routed to existing VFS helpers |
| Signals | `rt_sigaction`, `tgkill`, `sigtimedwait` | Identical signal delivery |
| Event/timer | `epoll_*`, `poll`, `ppoll`, `timerfd_*`, `signalfd_*` | Implemented via CR waits |
| Futex | `futex`, `futex_waitv`, PI variants | Implemented as `XSYNC_*` ops preserving semantics |
| I/O & sockets | `read`, `write`, `sendmmsg`, `accept4`, `ioctl` | Generic file/driver dispatch |
| BPF, perf | `bpf(BPF_*)`, `perf_event_open` | Direct; verifier and perf subsystems untouched |
| Memory mgmt | `mmap`, `mremap`, `madvise`, `mlock2` | Normal MM subsystem; MDWE enforced only in XSC-CET |
| Runtimes | `rseq`, `membarrier`, `userfaultfd` | Same kernel services used by glibc/JVM/.NET |
| io_uring | `io_uring_setup/enter/register` | Served by libxsc-uring (ABI-compatible shim) |

### 8.3 Conformance testing
LTP, glibc, io_uring, CRIU, container runtimes, and JVM/.NET tests run on XSC and stock Linux; return codes and errno validated bit-for-bit.

---

## 9. Justifications (Why Each Change Was Made)

| Change | Reason | Result |
|---------|--------|--------|
| Seccomp-on-consume | Preserve existing container seccomp behavior | Identical filter semantics |
| Audit/trace parity | Preserve audit and observability tools | Zero regression |
| RANDKSTACK/HARDENED_USERCOPY | Free hardening with no downsides | Leak/copy CVE mitigation |
| Null doorbell strict policy | Avoid purity criticism; maintain isolation | Optional, disabled by default |
| Linux API parity | Guarantee adoption and migration ease | Drop-in replacement for Linux user space |
| Split XSC vs. XSC‑CET | Let users choose performance vs. hardening | Clear separation |

---

## 10. References
1. L. Soares & M. Stumm, “FlexSC: Flexible System Call Scheduling with Exception‑Less System Calls,” *USENIX OSDI*, 2010.  
2. Intel, *CET Specification*, 2021.  
3. Arm Ltd., *BTI & PAC Reference Manual*, 2022.  
4. Linux kernel docs, *SHSTK/BTI/PAC*, 2023.  
5. PaX Team, *RANDKSTACK/STACKLEAK Mitigations*, 2018.  
6. Red Hat, “MDS/TAA Vulnerability Overview,” 2019.  
7. NVD/Red Hat CVE Data, 2015–2025.  

---

*End of Document*


## 12. Lockdown (Optional, CET Build Default)

### 12.1 Purpose
XSC-CET enables the Linux **Lockdown LSM** in **Integrity mode** by default to prevent kernel modification vectors even from privileged users.  
Lockdown complements XSC’s domain isolation, IOMMU, and hardware mitigations, providing an additional software barrier against privilege escalation.

### 12.2 Modes
| Mode | Description | Default |
|------|--------------|----------|
| **Integrity** | Blocks kernel-modifying actions while preserving observability (perf, BPF, audit). | Default (CET builds) |
| **Confidentiality** | Adds introspection restrictions (perf/kcore/kallsyms) for compliance. | Optional (Paranoid profile) |

### 12.3 What Integrity Mode Blocks
| Vector | Status | Comment |
|---------|---------|----------|
| `/dev/mem`, `/dev/kmem`, `/dev/port` | **Blocked** | Rarely legitimate in modern systems. |
| `iopl`, `ioperm` | **Blocked** | Legacy user-space direct I/O disabled. |
| Unsigned `kexec_file_load` | **Blocked** | Requires signed kernels. |
| ACPI table override | **Blocked** | Prevents firmware injection. |
| Raw PCI config / MSR writes | **Blocked** | Only driver APIs allowed. |
| Text poking / patching | **Blocked** | Prevents runtime code modification. |
| Perf / trace (read-only) | **Allowed** | XSC tracepoints & audit remain intact. |
| eBPF (with JIT hardening) | **Allowed** | Subject to `CAP_BPF` / `CAP_PERFMON`. |

### 12.4 Complementary Configurations
**Kernel configuration:**  
```
CONFIG_SECURITY_LOCKDOWN_LSM=y
CONFIG_LOCK_DOWN_KERNEL_FORCE_INTEGRITY=y
CONFIG_MODULE_SIG=y
CONFIG_MODULE_SIG_FORCE=y
CONFIG_MODULE_SIG_ALL=y
CONFIG_KEXEC_FILE=y
CONFIG_KEXEC_SIG=y
CONFIG_KEXEC_SIG_FORCE=y
CONFIG_BPF_JIT=y
CONFIG_BPF_JIT_HARDEN=y
CONFIG_RANDOMIZE_BASE=y
```

**Sysctl / runtime controls:**  
```
lockdown=integrity
/proc/sys/kernel/kptr_restrict=2
/proc/sys/kernel/dmesg_restrict=1
```

### 12.5 Profiles
| Profile | Mode | Notes |
|----------|------|-------|
| **Balanced (default CET)** | Integrity | Prevents kernel modification, preserves observability. |
| **Paranoid (compliance)** | Confidentiality | Hides kernel symbols & restricts introspection. |
| **Developer / Compat** | Off | Legacy debug flexibility, lower assurance. |

### 12.6 Integration with XSC Controls
- Complements domain and tenant isolation by securing the kernel against local privilege escalation.  
- Works alongside BPF JIT hardening and module signature enforcement.  
- No interference with XSC observability (tracepoints, auditd, seccomp).  
- Combined with Secure/Measured Boot and IOMMU enforcement, forms a complete integrity chain.

### 12.7 Summary
Lockdown Integrity mode is **enabled by default** in XSC-CET builds, silently protecting against kernel modification from user space without visible compatibility loss.  
Confidentiality mode may be activated in Paranoid profiles where total introspection control is required.  
This feature closes the software-layer gap, ensuring that even root-level processes cannot subvert XSC’s isolation guarantees.

