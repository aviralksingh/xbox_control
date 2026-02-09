#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="${1:-build}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake ..
make -j"$(nproc 2>/dev/null || echo 2)"

echo
echo "Build complete. Run:"
echo "  $BUILD_DIR/xbox_udp_publisher [dest] [port]"
echo "  $BUILD_DIR/udp_receiver_test [port]"
