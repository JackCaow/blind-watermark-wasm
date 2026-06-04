#!/bin/bash
# Compile and run the native core-verification harness (no Emscripten required).
set -e
cd "$(dirname "$0")/.."

mkdir -p build
# image_io.cpp is compiled too (without BWM_HAVE_WEBP, so only the stb path) to
# syntax-check loadImageRGBAFromMemory; the WebP branch is verified by review only.
clang++ -std=c++17 -O2 -DNDEBUG -DEIGEN_NO_DEBUG -Wno-deprecated-declarations \
    -I src -I third_party -I third_party/Eigen \
    script/verify_core.cpp \
    src/watermark_core.cpp src/dct.cpp src/dwt.cpp src/color_convert.cpp src/image_io.cpp \
    -o build/verify_core

./build/verify_core
