#!/bin/bash
set -euo pipefail

# XSC Master Build Controller
# Orchestrates building all 2,600 packages for Debian DVD-1
#
# Usage:
#   ./xsc-master-builder.sh           # Build all stages
#   ./xsc-master-builder.sh stage1    # Build only stage 1
#   ./xsc-master-builder.sh stage2    # Build only stage 2
#   ./xsc-master-builder.sh retry     # Retry failed builds

BUILD_DIR=${BUILD_DIR:-/storage/icloud-backup/build/xsc-debian-full}
TOOLCHAIN=${TOOLCHAIN:-/storage/icloud-backup/build/xsc-toolchain-x86_64-base}
RESULTS_DIR=$BUILD_DIR/results
LOGS_DIR=$BUILD_DIR/logs
CHROOT_TARBALL=/var/lib/sbuild/bookworm-xsc-amd64.tar.gz

export PATH=$TOOLCHAIN/bin:$PATH
export DEB_BUILD_OPTIONS="parallel=80 nocheck"
export DEB_BUILD_PROFILES="cross"

# Create directory structure
mkdir -p $BUILD_DIR/{sources,results/{stage1,stage2,stage3,stage4},logs,failed,completed}

# Stage definitions
STAGE1_LIST=$BUILD_DIR/stage1-packages.txt
STAGE2_LIST=$BUILD_DIR/stage2-packages.txt
STAGE3_LIST=$BUILD_DIR/stage3-packages.txt
STAGE4_LIST=$BUILD_DIR/stage4-packages.txt

# Logging
exec > >(tee -a $LOGS_DIR/master-build.log)
exec 2>&1

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"
}

# Build a single package
build_package() {
    local package=$1
    local stage=$2

    log "Building $package (stage $stage)..."

    # Check if already built
    if [ -f "$RESULTS_DIR/stage$stage/${package}_"*.deb ]; then
        log "SKIP: $package already built"
        return 0
    fi

    # Download source
    cd $BUILD_DIR/sources
    if ! apt-get source --download-only $package 2>&1 | tee -a $LOGS_DIR/${package}-download.log; then
        log "ERROR: Failed to download source for $package"
        echo "$package" >> $BUILD_DIR/failed/stage${stage}-download-failed.txt
        return 1
    fi

    # Find the .dsc file
    local dsc_file=$(find $BUILD_DIR/sources/ -name "${package}_*.dsc" -o -name "${package/-/_}_*.dsc" | head -1)

    if [ -z "$dsc_file" ]; then
        log "ERROR: No .dsc file found for $package"
        echo "$package" >> $BUILD_DIR/failed/stage${stage}-no-dsc.txt
        return 1
    fi

    # Build with sbuild
    if sbuild \
        --host=xsc-amd64 \
        --chroot-mode=unshare \
        --build-dir=$RESULTS_DIR/stage$stage \
        --log-file=$LOGS_DIR/${package}-stage${stage}.log \
        --extra-package=$RESULTS_DIR/stage1 \
        --extra-package=$RESULTS_DIR/stage2 \
        --extra-repository="deb [trusted=yes] file://$RESULTS_DIR ./" \
        --nolog \
        --no-run-lintian \
        --no-run-autopkgtest \
        "$dsc_file" 2>&1 | tee -a $LOGS_DIR/${package}-build.log; then

        log "SUCCESS: $package built successfully"
        echo "$package" >> $BUILD_DIR/completed/stage${stage}.txt

        # Move built packages to results directory
        mv ${package}_*.deb $RESULTS_DIR/stage$stage/ 2>/dev/null || true
        mv ${package}_*.changes $RESULTS_DIR/stage$stage/ 2>/dev/null || true

        return 0
    else
        log "ERROR: Build failed for $package (exit code: $?)"
        echo "$package" >> $BUILD_DIR/failed/stage${stage}-build-failed.txt

        # Check for missing dependencies
        if grep -q "Unmet build dependencies" $LOGS_DIR/${package}-build.log; then
            log "  Reason: Missing dependencies"
            grep "Unmet build dependencies" $LOGS_DIR/${package}-build.log | \
                tee -a $BUILD_DIR/failed/stage${stage}-missing-deps.txt
        fi

        return 1
    fi
}

# Build a stage with parallelization
build_stage() {
    local stage=$1
    local package_list=$2
    local max_parallel=$3

    log "========================================="
    log "Starting Stage $stage"
    log "Package list: $package_list"
    log "Packages to build: $(wc -l < $package_list)"
    log "Max parallel jobs: $max_parallel"
    log "========================================="

    if [ ! -f "$package_list" ]; then
        log "ERROR: Package list not found: $package_list"
        return 1
    fi

    # Use GNU parallel for job management
    export -f build_package log
    export BUILD_DIR TOOLCHAIN RESULTS_DIR LOGS_DIR CHROOT_TARBALL
    export DEB_BUILD_OPTIONS DEB_BUILD_PROFILES

    cat "$package_list" | \
        parallel --line-buffer --tagstring "{}" -j $max_parallel \
        --joblog $LOGS_DIR/stage${stage}-parallel.log \
        --halt-on-error 0 \
        build_package {} $stage

    # Report results
    local completed=$(wc -l < $BUILD_DIR/completed/stage${stage}.txt 2>/dev/null || echo 0)
    local total=$(wc -l < $package_list)
    local failed=$((total - completed))

    log "========================================="
    log "Stage $stage Results:"
    log "  Completed: $completed packages"
    log "  Failed:    $failed packages"
    log "  Success rate: $((completed * 100 / total))%"
    log "========================================="

    if [ $failed -gt 0 ]; then
        log "Failed packages saved to: $BUILD_DIR/failed/stage${stage}-*.txt"
    fi

    return 0
}

# Retry failed builds
retry_failed() {
    log "Retrying failed builds..."

    for stage in 1 2 3 4; do
        local failed_list=$BUILD_DIR/failed/stage${stage}-build-failed.txt

        if [ -f "$failed_list" ]; then
            log "Retrying stage $stage failures ($(wc -l < $failed_list) packages)..."

            # Backup original failed list
            cp "$failed_list" "${failed_list}.bak"

            # Clear failed list
            > "$failed_list"

            # Retry each package
            while read package; do
                build_package "$package" "$stage"
            done < "${failed_list}.bak"
        fi
    done
}

# Generate build report
generate_report() {
    local report_file=$BUILD_DIR/build-report.txt

    log "Generating build report..."

    cat > "$report_file" << EOF
XSC Debian Build Report
Generated: $(date)
================================================================================

Build Statistics:
--------------------------------------------------------------------------------
EOF

    local total_completed=0
    local total_failed=0
    local total_packages=0

    for stage in 1 2 3 4; do
        local stage_list=$BUILD_DIR/stage${stage}-packages.txt
        if [ ! -f "$stage_list" ]; then
            continue
        fi

        local total=$(wc -l < $stage_list)
        local completed=$(wc -l < $BUILD_DIR/completed/stage${stage}.txt 2>/dev/null || echo 0)
        local failed=$((total - completed))
        local percent=$((completed * 100 / total))

        total_packages=$((total_packages + total))
        total_completed=$((total_completed + completed))
        total_failed=$((total_failed + failed))

        cat >> "$report_file" << EOF
Stage $stage: $completed/$total packages built ($percent%)
  Total:     $total
  Completed: $completed
  Failed:    $failed
EOF
    done

    cat >> "$report_file" << EOF

Overall:
  Total packages:    $total_packages
  Successfully built: $total_completed
  Failed:            $total_failed
  Success rate:      $((total_completed * 100 / total_packages))%

================================================================================

Disk Usage:
--------------------------------------------------------------------------------
  Source packages:  $(du -sh $BUILD_DIR/sources 2>/dev/null | cut -f1 || echo "N/A")
  Built packages:   $(du -sh $RESULTS_DIR 2>/dev/null | cut -f1 || echo "N/A")
  Logs:            $(du -sh $LOGS_DIR 2>/dev/null | cut -f1 || echo "N/A")
  Total:           $(du -sh $BUILD_DIR 2>/dev/null | cut -f1 || echo "N/A")

================================================================================

Failed Packages by Category:
--------------------------------------------------------------------------------
EOF

    for failed_file in $BUILD_DIR/failed/*.txt; do
        if [ -f "$failed_file" ]; then
            local count=$(wc -l < "$failed_file")
            local category=$(basename "$failed_file" .txt)
            echo "  $category: $count packages" >> "$report_file"
        fi
    done

    cat >> "$report_file" << EOF

================================================================================
End of Report
================================================================================
EOF

    log "Report saved to: $report_file"
    cat "$report_file"
}

# Main build process
main() {
    local command=${1:-all}

    log "========================================="
    log "XSC Master Builder"
    log "========================================="
    log "Build directory: $BUILD_DIR"
    log "Toolchain:       $TOOLCHAIN"
    log "Available cores: $(nproc)"
    log "Memory:          $(free -h | grep Mem | awk '{print $2}')"
    log "Disk space:      $(df -h $BUILD_DIR | tail -1 | awk '{print $4}')"
    log "========================================="

    # Generate package lists if they don't exist
    if [ ! -f "$STAGE1_LIST" ]; then
        log "Package lists not found. Run generate-stage-lists.sh first!"
        exit 1
    fi

    case "$command" in
        all)
            # Build all stages sequentially
            build_stage 1 "$STAGE1_LIST" 1     # Bootstrap must be sequential
            build_stage 2 "$STAGE2_LIST" 20    # Moderate parallelism
            build_stage 3 "$STAGE3_LIST" 40    # Higher parallelism
            build_stage 4 "$STAGE4_LIST" 80    # Maximum parallelism
            ;;
        stage1)
            build_stage 1 "$STAGE1_LIST" 1
            ;;
        stage2)
            build_stage 2 "$STAGE2_LIST" 20
            ;;
        stage3)
            build_stage 3 "$STAGE3_LIST" 40
            ;;
        stage4)
            build_stage 4 "$STAGE4_LIST" 80
            ;;
        retry)
            retry_failed
            ;;
        report)
            generate_report
            exit 0
            ;;
        *)
            log "ERROR: Unknown command: $command"
            log "Usage: $0 [all|stage1|stage2|stage3|stage4|retry|report]"
            exit 1
            ;;
    esac

    log "========================================="
    log "Build process completed!"
    log "========================================="

    # Generate final report
    generate_report
}

# Run main with all arguments
main "$@"
