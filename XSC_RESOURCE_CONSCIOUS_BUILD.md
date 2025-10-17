# XSC Resource-Conscious Build Guide

**IMPORTANT**: Always use `nice` and `ionice` to avoid overloading the build server.

---

## Build Resource Limits

### Never Do This (Too Aggressive):
```bash
# ❌ DON'T: Full 80 cores, no priority control
make -j80

# ❌ DON'T: Multiple parallel builds without limits
for pkg in *; do make -j40 & done
```

### Always Do This (Respectful):
```bash
# ✅ DO: Low priority, limited parallelism
nice -n 19 ionice -c 3 make -j10

# ✅ DO: Monitor load and back off
if [ $(uptime | awk '{print $10}' | cut -d, -f1 | cut -d. -f1) -gt 40 ]; then
    echo "Load too high, waiting..."
    sleep 60
fi
```

---

## Safe Build Commands

### 1. Toolchain Build
```bash
# Low priority, 10 parallel jobs max
ssh bx.ee "cd /storage/icloud-backup/build/xsc-toolchain-x86_64-base/build/glibc && \
    nice -n 19 ionice -c 3 make -j10 && \
    nice -n 19 ionice -c 3 make install"
```

### 2. Package Build (Single)
```bash
# Build one package at a time with low priority
ssh bx.ee "cd /storage/icloud-backup/build/package-build && \
    nice -n 19 ionice -c 3 dpkg-buildpackage -j10"
```

### 3. Package Build (Batch)
```bash
# Sequential builds with pauses
for pkg in bash coreutils util-linux; do
    ssh bx.ee "nice -n 19 ionice -c 3 \
        dpkg-source -x /storage/icloud-backup/build/sources/${pkg}_*.dsc && \
        cd ${pkg}-* && \
        nice -n 19 ionice -c 3 dpkg-buildpackage -j10"

    # Pause between packages
    echo "Package $pkg done, waiting 30s..."
    sleep 30
done
```

### 4. Kernel Build
```bash
ssh bx.ee "cd /storage/icloud-backup/build/linux-6.1 && \
    nice -n 19 ionice -c 3 make -j10 bzImage modules"
```

---

## Priority Levels Explained

### nice (CPU priority)
- `-n 19`: Lowest CPU priority (most polite)
- `-n 10`: Medium-low priority
- `-n 0`: Default priority (no nice)
- `-n -20`: Highest priority (requires root, don't use)

**Use `-n 19` for all XSC builds**

### ionice (I/O priority)
- `-c 3`: Idle I/O class (only use idle disk time)
- `-c 2 -n 7`: Best-effort, lowest priority
- `-c 2 -n 0`: Best-effort, highest priority
- `-c 1`: Real-time (requires root, don't use)

**Use `-c 3` (idle) for all XSC builds**

---

## Load Monitoring

### Check Load Before Starting
```bash
ssh bx.ee "uptime"
# Output: 20:00:01 up 5 days, 3:21, 1 user, load average: 2.34, 1.98, 1.76
#                                                         ^^^^
#                                           1-minute load average

# If load average > 20, wait before starting builds
```

### Monitor During Build
```bash
# Check every 5 minutes
watch -n 300 'ssh bx.ee uptime'

# If load spikes > 60, pause builds
ssh bx.ee "killall -STOP make"  # Pause all make processes
sleep 300
ssh bx.ee "killall -CONT make"  # Resume when load drops
```

---

## Recommended Build Strategy

### Phase 1: Deploy glibc (Low Impact)
```bash
# Copy files (minimal I/O)
scp xsc-glibc-syscalls-v7.c bx.ee:/storage/icloud-backup/build/

# Single-threaded integration
ssh bx.ee "cd /storage/icloud-backup/build && \
    nice -n 19 ./integrate-xsc-glibc.sh"

# Rebuild glibc with 10 jobs
ssh bx.ee "cd /storage/icloud-backup/build/xsc-toolchain-x86_64-base/build/glibc && \
    nice -n 19 ionice -c 3 make -j10 && \
    nice -n 19 make install"
```

### Phase 2: Build Essential Packages (Sequential)
```bash
# Build packages one at a time, not in parallel
PACKAGES="bash coreutils ncurses readline util-linux"

for pkg in $PACKAGES; do
    echo "Building $pkg..."
    ssh bx.ee "cd /storage/icloud-backup/build && \
        nice -n 19 ionice -c 3 ./build-single-package.sh $pkg"

    # Wait between packages
    echo "Waiting 60s before next package..."
    sleep 60
done
```

### Phase 3: Build ISO (Single Threaded)
```bash
# ISO creation is mostly I/O, not CPU intensive
ssh bx.ee "cd /storage/icloud-backup/build && \
    nice -n 19 ionice -c 3 ./build-xsc-iso.sh"
```

---

## Emergency: Kill Runaway Builds

If builds are overloading the server:

```bash
# Stop all make processes
ssh bx.ee "killall -STOP make"

# Check what's running
ssh bx.ee "ps aux | grep make | grep -v grep"

# Kill specific builds
ssh bx.ee "pkill -9 -f 'xsc-cross-compile'"

# Resume paused processes (if safe)
ssh bx.ee "killall -CONT make"
```

---

## Updated Build Scripts

All build scripts should include:

```bash
#!/bin/bash
# Resource-conscious build script

# Set low priority for entire script
renice -n 19 -p $$ > /dev/null 2>&1
ionice -c 3 -p $$ > /dev/null 2>&1

# Limit parallelism
MAX_JOBS=10

# Check load before starting
check_load() {
    load=$(uptime | awk '{print $10}' | cut -d, -f1 | cut -d. -f1)
    if [ "$load" -gt 40 ]; then
        echo "Load too high ($load), waiting..."
        sleep 60
        check_load  # Recursive check
    fi
}

check_load

# Your build commands here
nice -n 19 ionice -c 3 make -j${MAX_JOBS}
```

---

## Monitoring While Building

**Safe monitoring** (minimal overhead):
```bash
# Check once, don't loop
ssh bx.ee "uptime; ps aux | grep make | wc -l"

# Or check every 5 minutes (not every 30 seconds!)
watch -n 300 'ssh bx.ee uptime'
```

**Don't do** (too much overhead):
```bash
# ❌ This overloads with SSH connections
while true; do ssh bx.ee "lots of commands"; sleep 30; done
```

---

## Apology & Commitment

I apologize for overloading your server. Going forward:

✅ All builds will use `nice -n 19` and `ionice -c 3`
✅ Maximum 10 parallel jobs (`-j10`), not 80
✅ Sequential package builds with pauses between
✅ Load monitoring before starting intensive operations
✅ Minimal monitoring overhead (check every 5 min, not 30 sec)

**No more aggressive parallel builds without resource limits.**

---

**When server recovers, I'll proceed carefully with the glibc integration using these safe practices.**
