#!/bin/bash

# Real-time monitoring of XSC build progress
# Shows live stats, active builds, and resource usage

BUILD_DIR=${BUILD_DIR:-/storage/icloud-backup/build/xsc-debian-full}

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

clear

while true; do
    clear
    echo -e "${BLUE}=========================================${NC}"
    echo -e "${BLUE}   XSC Debian Build Progress Monitor${NC}"
    echo -e "${BLUE}=========================================${NC}"
    echo
    echo -e "${YELLOW}Build Status:${NC}"
    echo "$(date '+%Y-%m-%d %H:%M:%S')"
    echo

    # Stage statistics
    for stage in 1 2 3 4; do
        if [ -f "$BUILD_DIR/stage${stage}-packages.txt" ]; then
            total=$(wc -l < "$BUILD_DIR/stage${stage}-packages.txt" 2>/dev/null || echo 0)
            completed=$(wc -l < "$BUILD_DIR/completed/stage${stage}.txt" 2>/dev/null || echo 0)
            failed=$(cat "$BUILD_DIR/failed/stage${stage}"-*.txt 2>/dev/null | wc -l || echo 0)

            if [ $total -gt 0 ]; then
                percent=$((completed * 100 / total))
                remaining=$((total - completed - failed))

                # Progress bar
                bar_length=30
                filled=$((percent * bar_length / 100))
                empty=$((bar_length - filled))

                bar=""
                for ((i=0; i<filled; i++)); do bar+="█"; done
                for ((i=0; i<empty; i++)); do bar+="░"; done

                # Color based on status
                if [ $percent -eq 100 ]; then
                    color=$GREEN
                elif [ $failed -gt $((total / 10)) ]; then
                    color=$RED
                else
                    color=$YELLOW
                fi

                echo -e "${color}Stage $stage: [$bar] $percent%${NC}"
                printf "  Total: %4d | Completed: %4d | Failed: %3d | Remaining: %4d\n" \
                    $total $completed $failed $remaining
            fi
        fi
    done

    echo
    echo -e "${YELLOW}Resource Usage:${NC}"

    # CPU usage
    cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | cut -d'%' -f1)
    echo "  CPU:    ${cpu_usage}% ($(nproc) cores available)"

    # Memory usage
    mem_info=$(free -h | grep Mem)
    mem_used=$(echo $mem_info | awk '{print $3}')
    mem_total=$(echo $mem_info | awk '{print $2}')
    echo "  Memory: $mem_used / $mem_total"

    # Disk usage
    if [ -d "$BUILD_DIR" ]; then
        disk_used=$(du -sh "$BUILD_DIR" 2>/dev/null | cut -f1 || echo "N/A")
        disk_avail=$(df -h "$BUILD_DIR" | tail -1 | awk '{print $4}')
        echo "  Disk:   $disk_used used, $disk_avail available"
    fi

    # Load average
    load_avg=$(uptime | awk -F'load average:' '{print $2}')
    echo "  Load:  $load_avg"

    echo
    echo -e "${YELLOW}Active Processes:${NC}"

    # Count sbuild processes
    sbuild_count=$(pgrep -c sbuild 2>/dev/null || echo 0)
    echo "  sbuild:   $sbuild_count active builds"

    # Count parallel processes
    parallel_count=$(pgrep -c parallel 2>/dev/null || echo 0)
    echo "  parallel: $parallel_count job controllers"

    echo
    echo -e "${YELLOW}Recent Activity:${NC}"

    # Show last 5 completed packages
    if [ -f "$BUILD_DIR/logs/master-build.log" ]; then
        echo "  Last completed packages:"
        tail -100 "$BUILD_DIR/logs/master-build.log" | \
            grep "SUCCESS:" | \
            tail -5 | \
            sed 's/.*SUCCESS: /    - /' || echo "    (none yet)"

        echo
        echo "  Recent failures:"
        tail -100 "$BUILD_DIR/logs/master-build.log" | \
            grep "ERROR:" | \
            tail -5 | \
            sed 's/.*ERROR: /    - /' || echo "    (none)"
    fi

    echo
    echo -e "${YELLOW}Time Estimate:${NC}"

    # Calculate estimated completion time
    total_packages=0
    completed_packages=0
    for stage in 1 2 3 4; do
        if [ -f "$BUILD_DIR/stage${stage}-packages.txt" ]; then
            stage_total=$(wc -l < "$BUILD_DIR/stage${stage}-packages.txt" 2>/dev/null || echo 0)
            stage_complete=$(wc -l < "$BUILD_DIR/completed/stage${stage}.txt" 2>/dev/null || echo 0)
            total_packages=$((total_packages + stage_total))
            completed_packages=$((completed_packages + stage_complete))
        fi
    done

    if [ $completed_packages -gt 0 ] && [ $total_packages -gt 0 ]; then
        remaining=$((total_packages - completed_packages))
        percent_complete=$((completed_packages * 100 / total_packages))

        # Read start time from log
        if [ -f "$BUILD_DIR/logs/master-build.log" ]; then
            start_time=$(head -1 "$BUILD_DIR/logs/master-build.log" | grep -oP '\[\K[^]]+' || date '+%Y-%m-%d %H:%M:%S')
            start_epoch=$(date -d "$start_time" +%s 2>/dev/null || date +%s)
            current_epoch=$(date +%s)
            elapsed=$((current_epoch - start_epoch))

            if [ $elapsed -gt 0 ] && [ $completed_packages -gt 0 ]; then
                avg_time_per_pkg=$((elapsed / completed_packages))
                est_remaining=$((remaining * avg_time_per_pkg))

                # Convert to human readable
                hours=$((est_remaining / 3600))
                minutes=$(((est_remaining % 3600) / 60))

                echo "  Progress:       $completed_packages / $total_packages packages ($percent_complete%)"
                echo "  Elapsed:        $((elapsed / 3600))h $((elapsed % 3600 / 60))m"
                echo "  Est. Remaining: ${hours}h ${minutes}m"
                echo "  Avg:            ${avg_time_per_pkg}s per package"
            fi
        fi
    fi

    echo
    echo -e "${BLUE}=========================================${NC}"
    echo "Press Ctrl+C to exit | Refreshing in 5 seconds..."

    sleep 5
done
