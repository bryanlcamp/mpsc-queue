#!/bin/bash

BUILD_DIR="build"

print_usage() {
    echo "===================================================="
    echo "       MPSC-BUFFER Build Automation Suite"
    echo "===================================================="
    echo "Usage: ./build.sh [command]"
    echo ""
    echo "Commands:"
    echo "  init            Configure the build system using platform presets"
    echo "  all             Compile all libraries and applications"
    echo "  mpsc_benchmark  Compile and launch the multi-threaded queue benchmark"
    echo "  clean           Completely wipe the build cache folder"
    echo "===================================================="
}

cd "$(dirname "$0")"

ensure_init() {
    if [ ! -d "$BUILD_DIR" ]; then
        echo "--> Build folder missing. Initializing system configurations first..."
        cmake --preset default-gcc -DCMAKE_CXX_FLAGS="-Wno-interference-size"
    fi
}

case "$1" in
    init)
        echo "--> Detecting system architecture and initializing build engine..."
        rm -rf "$BUILD_DIR"
        cmake --preset default-gcc -DCMAKE_CXX_FLAGS="-Wno-interference-size"
        ;;
    all)
        ensure_init
        echo "--> Compiling all MPSC targets simultaneously..."
        cmake --build "$BUILD_DIR" --target all
        ;;
    mpsc_benchmark)
        ensure_init
        cmake --build "$BUILD_DIR" --target mpsc_benchmark
        if [ $? -eq 0 ]; then
            echo "--> Executing MPSC Queue Benchmark Application..."
            ./"$BUILD_DIR"/apps/mpsc_benchmark/mpsc_benchmark
        fi
        ;;
    clean)
        echo "--> Purging build cache..."
        rm -rf "$BUILD_DIR"
        echo "Clean operation completed successfully."
        ;;
    *)
        print_usage
        exit 1
        ;;
# --- AUTOMATED WORKSPACE STATE RESET LOOP ---
esac