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

# Copy output to lib directory. With SINGLE_FILE=1 the wasm is base64-inlined into
# the .js, so there is no separate .wasm to copy (copy it only if it exists).
mkdir -p ../lib
cp blind_watermark.js ../lib/
[ -f blind_watermark.wasm ] && cp blind_watermark.wasm ../lib/ || true

echo "Build complete! Output in lib/"
