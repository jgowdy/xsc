# XSC QEMU Smoke Test

1. Build the kernel (with `seccomp-xsc.patch` applied) and copy the drivers/xsc
   sources into the tree.
2. From `~/flexsc` on `bx.ee`, run:
   ```
   ~/flexsc/test-xsc.sh
   ```
   This rebuilds a tiny initramfs with the demo binary and boots the kernel
   under QEMU with `/dev/xsc`.
3. Inside the QEMU shell (`/ #` prompt), the init script has already run the
   ring demo; inspect `/tmp` or re-run `xsc_ring_test` manually to exercise the
   interface.
4. To run additional tests, copy binaries into `initramfs/bin/` before invoking
   the script and rebuild the cpio archive.
