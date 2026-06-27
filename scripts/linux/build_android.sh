#!/bin/bash

# Build script for Android ARM64
# Requirements:
# - ANDROID_NDK_HOME set to your NDK path
# - rexglue SDK installed/available

set -e

if [ -z "$ANDROID_NDK_HOME" ]; then
    echo "Error: ANDROID_NDK_HOME is not set."
    exit 1
fi

TARGET=${1:-both}
PRESET=${2:-android-arm64-release}

echo "Building for Android ARM64 (Target: $TARGET, Preset: $PRESET)..."

build_project() {
    local proj=$1
    echo "Processing $proj..."
    
    # Run codegen first if needed
    # rexglue codegen $proj/${proj}_manifest.toml

    cmake --preset $PRESET -S $proj
    cmake --build --preset $PRESET
}

case $TARGET in
    sp|default)
        build_project default
        ;;
    mp|default_mp)
        build_project default_mp
        ;;
    both)
        build_project default
        build_project default_mp
        ;;
    *)
        echo "Unknown target: $TARGET"
        exit 1
        ;;
esac

echo "Build complete."
echo "Native libraries are in:"
echo "  default/out/build/$PRESET/libdefault.so"
echo "  default_mp/out/build/$PRESET/libdefault_mp.so"
