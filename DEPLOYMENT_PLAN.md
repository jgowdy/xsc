# XSC v7 Deployment Plan

## Understanding: Build Governor Runs SERVER-SIDE

The governor is a **server-side daemon** that:
- Runs on bx.ee (NOT locally)
- Monitors bx.ee's load every 10 seconds
- Pauses/resumes builds running on bx.ee
- Uses `doas` for elevated priority

## Deployment Steps (When Server is Stable)

### Step 1: Deploy Files to Server
```bash
./deploy-and-build-xsc-v7.sh
```

This script will:
1. Upload governor scripts to `/storage/icloud-backup/build/` on bx.ee
2. **Start governor daemon on bx.ee** with: `doas ./start-build-governor.sh`
3. Upload glibc shim and build scripts
4. Start the ISO build (protected by the running governor)

### Step 2: Verify Governor is Running (on server)
```bash
ssh bx.ee "ps aux | grep xsc-build-governor"
ssh bx.ee "cat /storage/icloud-backup/build/governor-stats.txt"
```

Should show:
- Governor process running with high priority (nice -5)
- Stats file showing current load and state

### Step 3: Monitor Build Progress
```bash
ssh bx.ee './watch-build-status.sh'
```

This shows:
- Current load
- Governor state (MONITORING/PAUSED/RESUMING)
- Active builds
- Recent output

## How Governor Protects Server

**Server-side monitoring loop:**
```
bx.ee: governor process (nice -5)
  ↓
  Checks: uptime | awk (load average)
  ↓
  If load > 70: kill -STOP (pause all builds)
  If load < 40: kill -CONT (resume builds)
  ↓
  Repeat every 10 seconds
```

**Result:** Builds can never freeze the server because governor automatically pauses them.

## Files That Will Run on bx.ee

After deployment, running on server:
- `xsc-build-governor.sh` - Main governor daemon (nice -5)
- `build-xsc-v7-iso.sh` - ISO build (nice 19) [controlled by governor]
- `watch-build-status.sh` - Dashboard (run via SSH when monitoring)

## Current Status

- ✅ All scripts created and tested
- ✅ Deployment automation ready
- 🟡 Waiting for bx.ee to stabilize
- ⏳ Ready to deploy when you give the word

## When You're Ready

Just say "deploy" or "server is up" and I'll:
1. Check server connectivity
2. Run `./deploy-and-build-xsc-v7.sh`
3. Verify governor started on server
4. Monitor the build safely

No more local monitoring loops hammering the server.
