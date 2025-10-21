# XSC Seccomp Parity

- Worker threads call `xsc_seccomp_check()` before dispatching an SQE.
- That helper builds a canonical `struct seccomp_data` and invokes the new
  kernel helper `xsc_seccomp_evaluate()` (added via `seccomp-xsc.patch`), which
  runs the submitting task’s BPF filter (`__seccomp_filter` path).
- Return codes mirror native seccomp actions (ALLOW/ERRNO/TRAP/TRACE/KILL/LOG).
- The helper never touches the worker’s seccomp state; everything is evaluated
  against the origin task captured in `xsc_task_cred_snapshot()`.
- When seccomp is disabled in the kernel build, the helper collapses to ALLOW
  (matching how the kernel behaves without CONFIG_SECCOMP).
