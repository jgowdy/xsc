#!/bin/bash
# Simple dashboard to watch build status and governor
#
# Usage:
#   ./watch-build-status.sh

STATS_FILE="/storage/icloud-backup/build/governor-stats.txt"
BUILD_LOG="/storage/icloud-backup/build/xsc-v7-iso/build.log"

while true; do
    clear
    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║           XSC Build Status Dashboard                        ║"
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo ""

    # System status
    echo "┌─ System Status ─────────────────────────────────────────────┐"
    uptime
    echo ""
    df -h /storage/icloud-backup/build | tail -1
    echo "└─────────────────────────────────────────────────────────────┘"
    echo ""

    # Governor status
    if [ -f "$STATS_FILE" ]; then
        echo "┌─ Build Governor Status ─────────────────────────────────────┐"
        cat "$STATS_FILE"
        echo "└─────────────────────────────────────────────────────────────┘"
    else
        echo "⚠️  Build Governor not running"
    fi
    echo ""

    # Active processes
    echo "┌─ Active Build Processes ────────────────────────────────────┐"
    ps aux | grep -E "make.*-j|dpkg-buildpackage|gcc.*-o" | grep -v grep | head -5 || echo "No builds running"
    echo "└─────────────────────────────────────────────────────────────┘"
    echo ""

    # Recent build output
    if [ -f "$BUILD_LOG" ]; then
        echo "┌─ Recent Build Output ───────────────────────────────────────┐"
        tail -3 "$BUILD_LOG" 2>/dev/null || echo "No recent output"
        echo "└─────────────────────────────────────────────────────────────┘"
    fi

    echo ""
    echo "Refreshing every 5 seconds... (Ctrl+C to stop)"

    sleep 5
done
