#!/bin/bash
set -e

# Detect base directories
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build"

# Auto-detect compilers and tools for maximum speed
LAUNCHER_FLAGS=""
LINKER_FLAGS=""
GENERATOR_FLAGS=""

if command -v clang++ &> /dev/null; then
    export CC=clang
    export CXX=clang++
fi

if command -v ccache &> /dev/null; then
    LAUNCHER_FLAGS="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi

if command -v mold &> /dev/null; then
    LINKER_FLAGS="-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=mold"
fi

if command -v ninja &> /dev/null; then
    GENERATOR_FLAGS="-G Ninja"
fi

# Configure if build directory doesn't exist or we want to force reconfigure
cmake $GENERATOR_FLAGS -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release $LAUNCHER_FLAGS $LINKER_FLAGS

# Build using Ninja if available, otherwise fallback to standard parallel CMake build
if command -v ninja &> /dev/null; then
    ninja -C "$BUILD_DIR"
else
    CORES=$(nproc 2>/dev/null || echo 4)
    cmake --build "$BUILD_DIR" -j "$CORES"
fi
