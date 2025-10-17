#!/bin/bash
# XSC Build Governor - Monitors and controls builds to prevent overload
#
# This script should be run with doas/sudo at higher priority than builds:
#   doas nice -n -5 ./xsc-build-governor.sh
#
# It will automatically pause builds when load is too high and resume when safe.

set -e

# Configuration
LOAD_THRESHOLD_PAUSE=70     # Pause builds if 1-min load > this
LOAD_THRESHOLD_RESUME=40    # Resume builds if 1-min load < this
CHECK_INTERVAL=10           # Check every N seconds
LOG_FILE="/storage/icloud-backup/build/governor.log"
STATS_FILE="/storage/icloud-backup/build/governor-stats.txt"

# Build process patterns to control
BUILD_PATTERNS=(
    "make.*-j"
    "dpkg-buildpackage"
    "gcc.*-o"
    "xsc-.*builder"
)

# State tracking
STATE="MONITORING"  # MONITORING, PAUSED, RESUMING
PAUSED_PIDS=()

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG_FILE"
}

get_load() {
    # Get 1-minute load average
    uptime | awk '{print $(NF-2)}' | tr -d ','
}

get_build_pids() {
    local pattern="$1"
    pgrep -f "$pattern" 2>/dev/null || true
}

pause_builds() {
    log "⏸️  PAUSING builds - load too high"
    STATE="PAUSED"

    # Find and pause all build processes
    for pattern in "${BUILD_PATTERNS[@]}"; do
        while IFS= read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                log "  Pausing PID $pid ($pattern)"
                kill -STOP "$pid" 2>/dev/null || true
                PAUSED_PIDS+=("$pid")
            fi
        done < <(get_build_pids "$pattern")
    done

    log "  Paused ${#PAUSED_PIDS[@]} build processes"
}

resume_builds() {
    log "▶️  RESUMING builds - load acceptable"
    STATE="RESUMING"

    # Resume all paused processes
    for pid in "${PAUSED_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            log "  Resuming PID $pid"
            kill -CONT "$pid" 2>/dev/null || true
        fi
    done

    PAUSED_PIDS=()
    STATE="MONITORING"
    log "  All builds resumed"
}

update_stats() {
    local load=$1
    local cpu_count=$(nproc)
    local load_pct=$(echo "scale=1; $load * 100 / $cpu_count" | bc)
    local active_builds=$(pgrep -fc "make.*-j" || echo 0)

    cat > "$STATS_FILE" <<EOF
Last Update: $(date '+%Y-%m-%d %H:%M:%S')
Load Average (1m): $load / $cpu_count cores ($load_pct%)
Active Build Processes: $active_builds
Governor State: $STATE
Paused Processes: ${#PAUSED_PIDS[@]}
Threshold Pause: $LOAD_THRESHOLD_PAUSE
Threshold Resume: $LOAD_THRESHOLD_RESUME
EOF
}

# Main monitoring loop
log "🎛️  XSC Build Governor starting..."
log "Configuration:"
log "  Pause threshold: $LOAD_THRESHOLD_PAUSE"
log "  Resume threshold: $LOAD_THRESHOLD_RESUME"
log "  Check interval: ${CHECK_INTERVAL}s"
log ""

# Set our own priority higher than builds (if we have permission)
renice -n -5 -p $$ 2>/dev/null || log "Warning: Cannot set higher priority (need doas/sudo)"

while true; do
    load=$(get_load)
    load_int=$(echo "$load" | cut -d. -f1)

    update_stats "$load"

    case "$STATE" in
        MONITORING)
            if [ "$load_int" -gt "$LOAD_THRESHOLD_PAUSE" ]; then
                log "⚠️  Load $load exceeds threshold $LOAD_THRESHOLD_PAUSE"
                pause_builds
            fi
            ;;

        PAUSED)
            if [ "$load_int" -lt "$LOAD_THRESHOLD_RESUME" ]; then
                log "✅ Load $load below resume threshold $LOAD_THRESHOLD_RESUME"
                resume_builds
            else
                # Check if paused processes are still alive
                local alive_count=0
                for pid in "${PAUSED_PIDS[@]}"; do
                    if kill -0 "$pid" 2>/dev/null; then
                        ((alive_count++))
                    fi
                done

                if [ "$alive_count" -eq 0 ] && [ "${#PAUSED_PIDS[@]}" -gt 0 ]; then
                    log "ℹ️  All paused processes have exited"
                    PAUSED_PIDS=()
                    STATE="MONITORING"
                fi
            fi
            ;;

        RESUMING)
            STATE="MONITORING"
            ;;
    esac

    sleep "$CHECK_INTERVAL"
done
