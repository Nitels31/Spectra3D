#!/bin/sh
# Thin wrapper around CMake - see CMakeLists.txt for the real build definition.
set -e
BUILD_DIR="$(dirname "$0")/build"
cmake -S "$(dirname "$0")" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$BUILD_DIR" --config Release "$@"
echo
echo "Build OK: $BUILD_DIR/spectra3d"