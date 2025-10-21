# XSC Test Plan

1. **Build/Load**
   - Build the patched kernel (apply `seccomp-xsc.patch`) and XSC module.
   - Boot into the kernel, insmod the module, ensure `/dev/xsc` appears.

2. **Functional I/O**
   - Build and run `samples/xsc_ring_demo.c` (use `tools/build-xsc-samples.sh`).
   - Run the supplied `test-xsc` binaries (hello/read/write/futex).
   - Confirm read/write/pread/pwrite and readv/writev succeed.
   - Socket test: send/recv across AF_INET pair.

3. **Seccomp Parity**
   - Apply a simple seccomp filter (allow read/write, deny open).
   - Make sure `open` SQEs are denied with `-EPERM` while read/write succeed.

4. **Attribution**
   - Place the submitting task in a non-default cgroup with throttled IO.
   - Submit SQEs and verify IO accounting increments the origin group.

5. **Observability**
   - Run `strace -ff -p <pid>` on an XSC process: syscall traces should appear.
   - Run `auditctl -a exit,always -S read` and verify XSC audit events.

6. **Stress**
   - Fire concurrent SQEs from multiple threads (with different cgroups).
   - Confirm no cross-task leakage and kernel doesn’t WARN/OOPS.
