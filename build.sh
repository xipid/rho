#!/bin/bash
set -e

# Curated HSL-like sleek styling for modern premium look
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0;m' # No Color

echo -e "${CYAN}================================================================${NC}"
echo -e "${CYAN}               Rho High-Performance Builder                     ${NC}"
echo -e "${CYAN}================================================================${NC}"

# Detect base directories
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build"

# Check if CMake is installed
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: cmake is required but not found in PATH.${NC}"
    exit 1
fi

echo -e "${BLUE}[1/3] Configuring project via CMake...${NC}"
# Configure the build directory
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo -e "${BLUE}[2/3] Building all targets (rhod + tests) in parallel...${NC}"
# Compile using all available CPU cores for maximum efficiency
CORES=$(nproc 2>/dev/null || echo 4)
echo -e "${YELLOW}  -> Using $CORES CPU cores for parallel compilation${NC}"
cmake --build "$BUILD_DIR" -j "$CORES"

echo -e "${BLUE}[3/3] Locating compiled targets...${NC}"
echo -e "${GREEN}✅ Rho Daemon (rhod) binary built at: ${BUILD_DIR}/rhod${NC}"

if [ -d "$BUILD_DIR/tests" ]; then
    echo -e "${GREEN}✅ Rho Unit Tests built at:${NC}"
    for test_bin in "$BUILD_DIR"/tests/*; do
        if [ -x "$test_bin" ] && [ -f "$test_bin" ]; then
            echo -e "     - ${test_bin}"
        fi
    done
fi

echo -e "${CYAN}================================================================${NC}"
echo -e "${GREEN}✨ Rho built successfully! To run the tests, run:${NC}"
echo -e "   ./build/tests/branch"
echo -e "   ./build/tests/daemon_test"
echo -e "${CYAN}================================================================${NC}"
