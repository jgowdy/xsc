# XSC v7 - Ready to Deploy

**EVERYTHING IS READY. DO NOT RUN ANYTHING UNTIL SERVER IS STABLE.**

## When Server is Stable - Run This ONE Command:

```bash
./deploy-and-build-xsc-v7.sh
```

That's it. One command does everything.

---

## What This Will Do (Automatically):

1. ✅ Deploy all files to server
2. ✅ **START BUILD GOVERNOR** (prevents overload)
3. ✅ Integrate glibc XSC shim
4. ✅ Rebuild toolchain
5. ✅ Start ISO build (2-6 hours, governor-protected)

## Critical: The Build Governor

The governor **MUST** run before any builds:
- Monitors load every 10 seconds
- Auto-pauses builds when load > 70
- Auto-resumes when load < 40
- Prevents server freezes

**The deployment script automatically starts it with `doas` for high priority.**

---

## Monitoring After Deployment

**Safe monitoring (won't overload):**
```bash
# Watch dashboard (updates every 5 sec)
ssh bx.ee './watch-build-status.sh'

# Check governor status once
ssh bx.ee 'cat /storage/icloud-backup/build/governor-stats.txt'

# Watch build log
ssh bx.ee 'tail -f /storage/icloud-backup/build/xsc-v7-iso-build.log'
```

---

## When Build Completes (2-6 hours)

```bash
# Copy ISO to Desktop
scp bx.ee:/storage/icloud-backup/build/xsc-v7-iso/xsc-debian-v7-base.iso ~/Desktop/
```

---

## Files Ready to Deploy

All committed in git (commit ee7a278):
- xsc-glibc-syscalls-v7.c
- build-xsc-v7-iso.sh
- xsc-build-governor.sh (THE CRITICAL ONE)
- start-build-governor.sh
- watch-build-status.sh
- test-xsc-hello.c
- test-xsc-fork.c
- integrate-xsc-glibc.sh

---

## IMPORTANT

**DO NOT:**
- ❌ Run monitoring loops
- ❌ Start builds without the governor
- ❌ Use -j80 (use -j30 max)
- ❌ Run anything else until you explicitly decide to

**The deployment script handles everything safely.**

---

**Status**: Waiting for server to stabilize
**Action Required**: When ready, run `./deploy-and-build-xsc-v7.sh`
