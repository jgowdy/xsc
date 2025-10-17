# XSC Full Debian ISO Build - Execution Checklist

This is your step-by-step checklist for building a complete Debian Bookworm DVD-1 ISO with all packages cross-compiled for the XSC architecture.

## Pre-Build Checklist

### Prerequisites Verification
- [ ] Server access confirmed (bx.ee with 80 cores, 128GB RAM)
- [ ] XSC toolchain exists at `/storage/icloud-backup/build/xsc-toolchain-x86_64-base`
- [ ] XSC kernel built at `/storage/icloud-backup/build/linux-6.1`
- [ ] Root/sudo access available on server
- [ ] At least 200 GB free disk space confirmed
- [ ] Network connectivity verified (for downloading Debian packages)

### File Transfer
- [ ] Upload all build scripts to server:
  ```bash
  scp *.sh bx.ee:/storage/icloud-backup/build/scripts/
  ```
- [ ] Verify uploads:
  ```bash
  ssh bx.ee ls -lh /storage/icloud-backup/build/scripts/
  ```

## Build Process Checklist

### Phase 1: Environment Setup (Day 1 - ~30 minutes)

- [ ] SSH to server:
  ```bash
  ssh bx.ee
  cd /storage/icloud-backup/build/scripts
  ```

- [ ] Make scripts executable:
  ```bash
  chmod +x *.sh
  ```

- [ ] Run environment setup (requires sudo):
  ```bash
  sudo ./setup-xsc-sbuild.sh
  ```

- [ ] Verify sbuild chroot created:
  ```bash
  ls -lh /var/lib/sbuild/bookworm-xsc-amd64.tar.gz
  schroot -l | grep xsc-amd64
  ```

- [ ] Test chroot:
  ```bash
  schroot -c source:bookworm-xsc-amd64 -u root -- /bin/bash -c 'echo OK'
  ```

**Expected time:** 15-20 minutes

**Output:** sbuild environment ready for builds

---

### Phase 2: Package List Generation (Day 1 - ~10 minutes)

- [ ] Generate package lists:
  ```bash
  ./generate-stage-lists.sh
  ```

- [ ] Verify package lists created:
  ```bash
  ls -lh /storage/icloud-backup/build/xsc-debian-full/stage*.txt
  ```

- [ ] Check package counts:
  ```bash
  wc -l /storage/icloud-backup/build/xsc-debian-full/stage*.txt
  ```

- [ ] Expected output:
  - Stage 1: ~30 packages (build-essential)
  - Stage 2: ~160 packages (essential+required)
  - Stage 3: ~800 packages (important+standard)
  - Stage 4: ~1,640 packages (optional)
  - Total: ~2,630 packages

**Expected time:** 5-10 minutes

**Output:** 4 stage package lists ready

---

### Phase 3: Test Build (Day 1 - ~1 hour)

Before starting the full 60+ hour build, test with a single package:

- [ ] Test build a simple package:
  ```bash
  cd /storage/icloud-backup/build/xsc-debian-full
  apt-get source --download-only hello
  sbuild --host=xsc-amd64 -c bookworm-xsc-amd64 hello_*.dsc
  ```

- [ ] Verify .deb package created:
  ```bash
  ls -lh hello_*.deb
  ```

- [ ] Check for syscall instructions (should be none):
  ```bash
  dpkg-deb -x hello_*.deb /tmp/hello-test
  objdump -d /tmp/hello-test/usr/bin/hello | grep syscall || echo "OK - No syscalls found"
  ```

- [ ] If test succeeds, proceed to full build

**Expected time:** 10-15 minutes

**Output:** Confirmed working build environment

---

### Phase 4: Full Build Launch (Day 1 - start)

- [ ] Start the master build process:
  ```bash
  cd /storage/icloud-backup/build/scripts
  nohup ./xsc-master-builder.sh > build.log 2>&1 &
  ```

- [ ] Save process ID:
  ```bash
  echo $! > build.pid
  cat build.pid
  ```

- [ ] Verify build started:
  ```bash
  tail -f build.log
  ```

- [ ] Press Ctrl+C to exit tail (build continues in background)

**Expected time:** 60-120 hours (build runs autonomously)

**Output:** Build process running in background

---

### Phase 5: Monitoring (Days 2-5)

- [ ] Monitor progress daily with dashboard:
  ```bash
  ssh bx.ee
  cd /storage/icloud-backup/build/scripts
  ./monitor-xsc-build.sh
  ```

- [ ] Check build logs:
  ```bash
  tail -100 /storage/icloud-backup/build/xsc-debian-full/logs/master-build.log
  ```

- [ ] Generate progress report:
  ```bash
  ./xsc-master-builder.sh report
  ```

- [ ] Monitor disk space daily:
  ```bash
  df -h /storage/icloud-backup
  ```

- [ ] Monitor memory usage:
  ```bash
  free -h
  ```

**Daily checks recommended**

---

### Phase 6: Handle Failures (As needed)

If packages fail to build:

- [ ] Check failed package list:
  ```bash
  cat /storage/icloud-backup/build/xsc-debian-full/failed/*.txt
  ```

- [ ] Review failure logs:
  ```bash
  less /storage/icloud-backup/build/xsc-debian-full/logs/<package>-build.log
  ```

- [ ] Retry failed builds:
  ```bash
  ./xsc-master-builder.sh retry
  ```

- [ ] For persistent failures, investigate:
  ```bash
  # Check dependencies
  apt-cache showsrc <package> | grep Build-Depends

  # Try manual build
  sbuild --host=xsc-amd64 <package>_*.dsc
  ```

**As needed throughout build process**

---

### Phase 7: Build Completion Verification (Day 5)

When build completes:

- [ ] Generate final report:
  ```bash
  ./xsc-master-builder.sh report
  ```

- [ ] Check success rate:
  - Target: >95% packages built successfully
  - Acceptable: >90% (some packages may be optional)
  - Review failures if <90%

- [ ] Verify package count:
  ```bash
  find /storage/icloud-backup/build/xsc-debian-full/results -name '*.deb' | wc -l
  ```

- [ ] Expected: ~2,500+ .deb packages

- [ ] Check total size of built packages:
  ```bash
  du -sh /storage/icloud-backup/build/xsc-debian-full/results
  ```

- [ ] Expected: 10-15 GB

**Expected time:** 5 minutes

**Output:** Build completion confirmation

---

### Phase 8: ISO Generation (Day 5 - ~30 minutes)

- [ ] Run ISO builder:
  ```bash
  ./build-xsc-iso.sh
  ```

- [ ] Verify ISO created:
  ```bash
  ls -lh /storage/icloud-backup/build/iso-output/xsc-debian-12.8-dvd1-amd64.iso
  ```

- [ ] Expected size: 3.5-4.7 GB

- [ ] Verify checksums generated:
  ```bash
  ls -lh /storage/icloud-backup/build/iso-output/*.{sha256,md5}
  ```

- [ ] Check ISO contents:
  ```bash
  isoinfo -l -i /storage/icloud-backup/build/iso-output/xsc-debian-12.8-dvd1-amd64.iso | head -50
  ```

**Expected time:** 20-30 minutes

**Output:** Bootable ISO image

---

### Phase 9: ISO Testing (Day 5-6 - ~2 hours)

- [ ] Download ISO to local machine (optional):
  ```bash
  scp bx.ee:/storage/icloud-backup/build/iso-output/xsc-debian-12.8-dvd1-amd64.iso .
  ```

- [ ] Test boot in QEMU:
  ```bash
  qemu-system-x86_64 \
    -m 4096 \
    -smp 4 \
    -cdrom xsc-debian-12.8-dvd1-amd64.iso \
    -boot d \
    -serial stdio
  ```

- [ ] Verify boot sequence:
  - [ ] Bootloader appears
  - [ ] Kernel loads
  - [ ] Initramfs loads
  - [ ] System boots to login/installer

- [ ] Test package repository:
  ```bash
  # Mount ISO
  mkdir -p /mnt/iso
  mount -o loop xsc-debian-12.8-dvd1-amd64.iso /mnt/iso

  # Create test chroot
  debootstrap --arch=xsc-amd64 bookworm /tmp/xsc-test file:///mnt/iso

  # Test package installation
  chroot /tmp/xsc-test apt-get update
  chroot /tmp/xsc-test apt-get install vim
  ```

- [ ] Verify XSC compliance:
  ```bash
  # Check random packages for syscall instructions
  ./verify-no-syscalls.sh
  ```

**Expected time:** 1-2 hours

**Output:** Verified working ISO

---

### Phase 10: Documentation & Cleanup (Day 6)

- [ ] Generate final build report:
  ```bash
  ./xsc-master-builder.sh report > FINAL_BUILD_REPORT.txt
  ```

- [ ] Document any package failures:
  ```bash
  cat /storage/icloud-backup/build/xsc-debian-full/failed/*.txt > FAILED_PACKAGES.txt
  ```

- [ ] Create package manifest:
  ```bash
  find /storage/icloud-backup/build/xsc-debian-full/results -name '*.deb' \
    -exec basename {} \; | sort > PACKAGE_MANIFEST.txt
  ```

- [ ] Optional: Clean up build artifacts to save space:
  ```bash
  # Clean source downloads (saves ~15 GB)
  rm -rf /storage/icloud-backup/build/xsc-debian-full/sources/*

  # Clean temporary build directories (saves ~80 GB)
  find /storage/icloud-backup/build/xsc-debian-full -name '*.build' -type d -delete

  # Keep: results/, logs/, ISO
  ```

**Expected time:** 30 minutes

**Output:** Final documentation

---

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] Stages 1-2 complete (~190 packages)
- [ ] ISO boots to shell
- [ ] Basic commands work (ls, cat, grep)
- [ ] No syscall instructions in binaries
- [ ] Can install packages from ISO

### Full Success (Target)
- [ ] All 4 stages complete (~2,630 packages)
- [ ] >95% package build success rate
- [ ] ISO boots with installer
- [ ] All packages installable from ISO
- [ ] XSC kernel loads successfully
- [ ] System fully operational

### Stretch Goals
- [ ] Desktop environment included
- [ ] >99% package build success rate
- [ ] All packages XSC-compliant
- [ ] Performance benchmarks completed
- [ ] Documentation complete

---

## Timeline Summary

| Day | Phase | Tasks | Time |
|-----|-------|-------|------|
| 1 | Setup | Environment + Package Lists + Test | 2 hours |
| 1 | Build Start | Launch master build | 5 min |
| 2-5 | Building | Monitor progress (autonomous) | 60-120 hours |
| 5 | Completion | Verify + Generate ISO | 2 hours |
| 5-6 | Testing | QEMU testing + Verification | 2 hours |
| 6 | Cleanup | Documentation + Cleanup | 1 hour |

**Total Active Time:** ~8 hours (spread over 6 days)
**Total Elapsed Time:** 5-6 days (mostly autonomous building)

---

## Troubleshooting Quick Reference

### Build won't start
```bash
# Check sbuild
schroot -l | grep xsc
sudo ./setup-xsc-sbuild.sh  # Re-run if needed
```

### Out of disk space
```bash
# Clean sources
rm -rf /storage/icloud-backup/build/xsc-debian-full/sources/*
# Clean build dirs
find . -name '*.build' -type d -delete
```

### Package fails to build
```bash
# Check log
less /storage/icloud-backup/build/xsc-debian-full/logs/<package>-build.log
# Retry
./xsc-master-builder.sh retry
```

### Build too slow
```bash
# Check resources
htop
# Reduce parallelism if memory pressure
export DEB_BUILD_OPTIONS="parallel=40 nocheck"
```

### ISO won't boot
```bash
# Check kernel
ls -lh /storage/icloud-backup/build/xsc-iso-build/boot/vmlinuz-xsc
# Rebuild ISO
./build-xsc-iso.sh
```

---

## Final Checklist

### Pre-Delivery
- [ ] All build scripts tested
- [ ] Documentation reviewed
- [ ] Success criteria defined
- [ ] Troubleshooting guide complete

### Delivery Package
- [ ] 3 documentation files (Strategy, Guide, Summary)
- [ ] 5 build scripts (Setup, Generate, Build, ISO, Monitor)
- [ ] All scripts executable
- [ ] README with quick start

### Post-Build
- [ ] ISO generated and tested
- [ ] Package count verified (>2,500)
- [ ] XSC compliance confirmed
- [ ] Build report generated
- [ ] Known issues documented

---

**Ready to build!** Start with Phase 1 and follow this checklist sequentially.

**Support:** Review XSC_FULL_ISO_BUILD_STRATEGY.md for detailed technical information.

**Last Updated:** 2025-10-14
