# XSC Observability Checklist

- `xsc_trace_sys_enter/exit()` mirror the `syscalls:*` fields so strace/bpftrace
  need no changes.
- Audit events are emitted via `xsc_audit_submit/result()`; they reuse the
  origin task’s UID/GID/pid/tgid and the original audit context captured in the
  attribution snapshot.
- `current->xsc_origin` is set while handlers execute, enabling BPF programs to
  key off the true initiator (e.g., via kprobe/kretprobe handlers).
- `/proc/<pid>/syscall` and `/proc/<pid>/stack` now reflect ring activity while
  a worker is executing on behalf of a task because the css/audit state is
  swapped before dispatch.
- To validate:
  1. Attach `strace -ff -p <pid>` to an XSC-enabled process and submit SQEs;
     observe semantic syscall entries.
  2. Add an audit rule (`auditctl -a exit,always -S read`) and confirm
     `AUDIT_XSC_SUBMIT/RESULT` events with matching metadata.
  3. Use `bpftrace -e 'tracepoint:syscalls:sys_enter_read { @[comm] = count(); }'`
     while exercising read SQEs.
