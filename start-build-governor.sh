#!/bin/bash
# Start the XSC Build Governor with elevated priority
#
# Usage on bx.ee:
#   doas ./start-build-governor.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GOVERNOR_SCRIPT="$SCRIPT_DIR/xsc-build-governor.sh"
PID_FILE="/storage/icloud-backup/build/governor.pid"
LOG_FILE="/storage/icloud-backup/build/governor.log"

# Check if already running
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo "Governor already running with PID $OLD_PID"
        exit 0
    else
        echo "Removing stale PID file"
        rm -f "$PID_FILE"
    fi
fi

# Start governor in background with high priority
echo "Starting XSC Build Governor..."
nohup nice -n -5 "$GOVERNOR_SCRIPT" >> "$LOG_FILE" 2>&1 &
GOVERNOR_PID=$!

echo "$GOVERNOR_PID" > "$PID_FILE"
echo "Governor started with PID $GOVERNOR_PID"
echo "Log: $LOG_FILE"
echo "Stats: /storage/icloud-backup/build/governor-stats.txt"
echo ""
echo "To monitor:"
echo "  tail -f $LOG_FILE"
echo ""
echo "To stop:"
echo "  kill $GOVERNOR_PID && rm $PID_FILE"
