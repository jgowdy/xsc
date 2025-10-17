#!/bin/bash
# XSC Simple Package Builder
# Uses dpkg-buildpackage directly without sbuild (no root required)

set -eo pipefail

BUILD_DIR=${BUILD_DIR:-/storage/icloud-backup/build/xsc-debian-full}
TOOLCHAIN=${TOOLCHAIN:-/storage/icloud-backup/build/xsc-toolchain-x86_64-base}
RESULTS_DIR=$BUILD_DIR/results
LOGS_DIR=$BUILD_DIR/logs
SOURCES_DIR=$BUILD_DIR/sources

export PATH=$TOOLCHAIN/bin:$PATH
export CC=x86_64-xsc-linux-gnu-gcc
export CXX=x86_64-xsc-linux-gnu-g++
export DEB_BUILD_OPTIONS="parallel=80 nocheck"
export TMPDIR=$BUILD_DIR/tmp

# Create directory structure
mkdir -p $BUILD_DIR/{sources,results/{stage1,stage2,stage3,stage4},logs,failed,completed,tmp}

# Stage definitions
STAGE1_LIST=$BUILD_DIR/stage1-packages.txt
STAGE2_LIST=$BUILD_DIR/stage2-packages.txt
STAGE3_LIST=$BUILD_DIR/stage3-packages.txt
STAGE4_LIST=$BUILD_DIR/stage4-packages.txt

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a $LOGS_DIR/master-build.log
}

# Build a single package
build_package() {
    local package=$1
    local stage=$2

    log "Building $package (stage $stage)..."

    # Check if already built
    if compgen -G "$RESULTS_DIR/stage$stage/${package}_*.deb" > /dev/null; then
        log "SKIP: $package already built"
        return 0
    fi

    # Download source from Debian directly (can't use apt-get source without deb-src)
    cd $SOURCES_DIR

    # Use dget if available, otherwise wget from snapshot.debian.org
    if command -v dget >/dev/null 2>&1; then
        if ! dget -d -u "http://deb.debian.org/debian/pool/main/${package:0:1}/${package}/" >> $LOGS_DIR/${package}-download.log 2>&1; then
            log "ERROR: Failed to download source for $package with dget"
            echo "$package" >> $BUILD_DIR/failed/stage${stage}-download-failed.txt
            return 1
        fi
    else
        # Fallback: try to download .dsc from Debian
        local first_letter="${package:0:1}"
        if [ "${package:0:3}" = "lib" ]; then
            first_letter="lib${package:3:1}"
        fi

        local dsc_url="http://deb.debian.org/debian/pool/main/$first_letter/$package/"
        if ! wget -q -r -l1 -nd -A "*.dsc,*.tar.*,*.diff.gz" "$dsc_url" >> $LOGS_DIR/${package}-download.log 2>&1; then
            log "ERROR: Failed to download source for $package from $dsc_url"
            echo "$package" >> $BUILD_DIR/failed/stage${stage}-download-failed.txt
            return 1
        fi
    fi

    # Find .dsc file
    local dsc_file=$(find $SOURCES_DIR/ -maxdepth 1 -name "${package}_*.dsc" -o -name "${package/-/_}_*.dsc" 2>/dev/null | head -1)

    if [ -z "$dsc_file" ]; then
        log "ERROR: No .dsc file found for $package"
        echo "$package" >> $BUILD_DIR/failed/stage${stage}-no-dsc.txt
        return 1
    fi

    # Extract and build
    local src_dir=$(dpkg-source -x "$dsc_file" 2>&1 | grep "extracting" | awk '{print $NF}')

    if [ -z "$src_dir" ] || [ ! -d "$src_dir" ]; then
        log "ERROR: Failed to extract source for $package"
        echo "$package" >> $BUILD_DIR/failed/stage${stage}-extract-failed.txt
        return 1
    fi

    # Build with dpkg-buildpackage (-d skips dependency checks for bootstrapping)
    cd "$src_dir"
    if dpkg-buildpackage -d -us -uc -b \
        >> $LOGS_DIR/${package}-build.log 2>&1; then

        log "SUCCESS: $package built successfully"
        echo "$package" >> $BUILD_DIR/completed/stage${stage}.txt

        # Move built packages to results
        mv ../${package}_*.deb $RESULTS_DIR/stage$stage/ 2>/dev/null || true
        mv ../${package}_*.changes $RESULTS_DIR/stage$stage/ 2>/dev/null || true

        # Cleanup source
        cd $SOURCES_DIR
        rm -rf "$src_dir"

        return 0
    else
        log "ERROR: Build failed for $package"
        echo "$package" >> $BUILD_DIR/failed/stage${stage}-build-failed.txt

        # Cleanup
        cd $SOURCES_DIR
        rm -rf "$src_dir"

        return 1
    fi
}

# Build a stage with simple backgrounding (no parallel tool needed)
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

    export -f build_package log
    export BUILD_DIR TOOLCHAIN RESULTS_DIR LOGS_DIR SOURCES_DIR TMPDIR
    export DEB_BUILD_OPTIONS CC CXX PATH

    local count=0
    local pids=()

    while read -r package; do
        # Start build in background
        build_package "$package" "$stage" &
        pids+=($!)

        count=$((count + 1))

        # Wait when we hit max parallel jobs
        if [ $((count % max_parallel)) -eq 0 ]; then
            log "Waiting for batch of $max_parallel jobs to complete..."
            for pid in "${pids[@]}"; do
                wait $pid 2>/dev/null || true
            done
            pids=()
        fi
    done < "$package_list"

    # Wait for remaining jobs
    log "Waiting for final jobs to complete..."
    for pid in "${pids[@]}"; do
        wait $pid 2>/dev/null || true
    done

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

    return 0
}

# Main
main() {
    local command=${1:-all}

    log "========================================="
    log "XSC Simple Builder (no root required)"
    log "========================================="
    log "Build directory: $BUILD_DIR"
    log "Toolchain:       $TOOLCHAIN"
    log "Available cores: $(nproc)"
    log "========================================="

    # Generate package lists if they don't exist
    if [ ! -f "$STAGE1_LIST" ]; then
        log "Package lists not found. Run generate-stage-lists.sh first!"
        exit 1
    fi

    case "$command" in
        all)
            build_stage 1 "$STAGE1_LIST" 1
            build_stage 2 "$STAGE2_LIST" 5
            build_stage 3 "$STAGE3_LIST" 10
            build_stage 4 "$STAGE4_LIST" 20
            ;;
        stage1)
            build_stage 1 "$STAGE1_LIST" 1
            ;;
        stage2)
            build_stage 2 "$STAGE2_LIST" 5
            ;;
        stage3)
            build_stage 3 "$STAGE3_LIST" 10
            ;;
        stage4)
            build_stage 4 "$STAGE4_LIST" 20
            ;;
        *)
            log "ERROR: Unknown command: $command"
            log "Usage: $0 [all|stage1|stage2|stage3|stage4]"
            exit 1
            ;;
    esac

    log "========================================="
    log "Build process completed!"
    log "========================================="
}

main "$@"
