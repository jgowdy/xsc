#!/bin/bash
# XSC Docker-Based Package Builder
# Uses Docker containers to avoid /tmp issues and provide isolation

set -eo pipefail

BUILD_DIR=${BUILD_DIR:-/storage/icloud-backup/build/xsc-debian-build}
TOOLCHAIN=${TOOLCHAIN:-/storage/icloud-backup/build/xsc-toolchain-x86_64-base}
RESULTS_DIR=$BUILD_DIR/results
LOGS_DIR=$BUILD_DIR/logs
SOURCES_DIR=$BUILD_DIR/sources
DOCKER_IMAGE="xsc-debian-builder"

# Create directory structure
mkdir -p $BUILD_DIR/{sources,results/{stage1,stage2,stage3,stage4},logs,failed,completed}

# Stage definitions
STAGE1_LIST=$BUILD_DIR/stage1-packages.txt
STAGE2_LIST=$BUILD_DIR/stage2-packages.txt
STAGE3_LIST=$BUILD_DIR/stage3-packages.txt
STAGE4_LIST=$BUILD_DIR/stage4-packages.txt

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a $LOGS_DIR/master-build.log
}

# Build Docker image if it doesn't exist
build_docker_image() {
    if ! docker image inspect $DOCKER_IMAGE >/dev/null 2>&1; then
        log "Building Docker image $DOCKER_IMAGE..."
        docker build -t $DOCKER_IMAGE -f Dockerfile.debian-builder .
    else
        log "Docker image $DOCKER_IMAGE already exists"
    fi
}

# Build a single package in Docker
build_package() {
    local package=$1
    local stage=$2

    log "Building $package (stage $stage)..."

    # Check if already built
    if compgen -G "$RESULTS_DIR/stage$stage/${package}_*.deb" > /dev/null; then
        log "SKIP: $package already built"
        return 0
    fi

    # Download source from Debian directly
    cd $SOURCES_DIR

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

    # Find .dsc file
    local dsc_file=$(find $SOURCES_DIR/ -maxdepth 1 -name "${package}_*.dsc" -o -name "${package/-/_}_*.dsc" 2>/dev/null | head -1)

    if [ -z "$dsc_file" ]; then
        log "ERROR: No .dsc file found for $package"
        echo "$package" >> $BUILD_DIR/failed/stage${stage}-no-dsc.txt
        return 1
    fi

    # Extract source
    local src_dir=$(dpkg-source -x "$dsc_file" 2>&1 | grep "extracting" | awk '{print $NF}')

    if [ -z "$src_dir" ] || [ ! -d "$src_dir" ]; then
        log "ERROR: Failed to extract source for $package"
        echo "$package" >> $BUILD_DIR/failed/stage${stage}-extract-failed.txt
        return 1
    fi

    # Build in Docker container with controlled resources
    if docker run --rm \
        --cpus="2" \
        -v "$SOURCES_DIR:/build/sources" \
        -v "$TOOLCHAIN:/toolchain:ro" \
        -v "$RESULTS_DIR/stage$stage:/build/results" \
        -e "PATH=/toolchain/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
        -e "CC=x86_64-xsc-linux-gnu-gcc" \
        -e "CXX=x86_64-xsc-linux-gnu-g++" \
        -e "DEB_BUILD_OPTIONS=parallel=2 nocheck" \
        -w "/build/sources/$src_dir" \
        $DOCKER_IMAGE \
        dpkg-buildpackage -d -us -uc -b >> $LOGS_DIR/${package}-build.log 2>&1; then

        log "SUCCESS: $package built successfully"
        echo "$package" >> $BUILD_DIR/completed/stage${stage}.txt

        # Move built packages to results (they're already in the mounted volume)
        cd $SOURCES_DIR
        mv ${package}_*.deb $RESULTS_DIR/stage$stage/ 2>/dev/null || true
        mv ${package}_*.changes $RESULTS_DIR/stage$stage/ 2>/dev/null || true

        # Cleanup source
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

# Build a stage with Docker containers
build_stage() {
    local stage=$1
    local package_list=$2
    local max_parallel=$3

    log "========================================="
    log "Starting Stage $stage (Docker-based)"
    log "Package list: $package_list"
    log "Packages to build: $(wc -l < $package_list)"
    log "Max parallel jobs: $max_parallel"
    log "========================================="

    if [ ! -f "$package_list" ]; then
        log "ERROR: Package list not found: $package_list"
        return 1
    fi

    export -f build_package log
    export BUILD_DIR TOOLCHAIN RESULTS_DIR LOGS_DIR SOURCES_DIR DOCKER_IMAGE

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
    log "XSC Docker Builder"
    log "========================================="
    log "Build directory: $BUILD_DIR"
    log "Toolchain:       $TOOLCHAIN"
    log "Docker image:    $DOCKER_IMAGE"
    log "========================================="

    # Build Docker image first
    build_docker_image

    # Generate package lists if they don't exist
    if [ ! -f "$STAGE1_LIST" ]; then
        log "Package lists not found. Run generate-stage-lists.sh first!"
        exit 1
    fi

    case "$command" in
        all)
            build_stage 1 "$STAGE1_LIST" 5
            build_stage 2 "$STAGE2_LIST" 10
            build_stage 3 "$STAGE3_LIST" 20
            build_stage 4 "$STAGE4_LIST" 40
            ;;
        stage1)
            build_stage 1 "$STAGE1_LIST" 5
            ;;
        stage2)
            build_stage 2 "$STAGE2_LIST" 10
            ;;
        stage3)
            build_stage 3 "$STAGE3_LIST" 20
            ;;
        stage4)
            build_stage 4 "$STAGE4_LIST" 40
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
