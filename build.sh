#!/bin/bash

# Build script for blind-watermark-wasm

set -e

echo "Building blind-watermark-wasm..."

# Create build directory
mkdir -p build
cd build

# Configure with Emscripten
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
emmake make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Copy output to lib directory
mkdir -p ../lib
cp blind_watermark.js ../lib/
cp blind_watermark.wasm ../lib/

echo "Build complete! Output in lib/"
