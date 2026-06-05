#!/bin/bash
# Compile and run the robustness benchmark (native, no Emscripten).
set -e
cd "$(dirname "$0")/.."

mkdir -p build
clang++ -std=c++17 -O2 -DNDEBUG -DEIGEN_NO_DEBUG -Wno-deprecated-declarations \
    -I src -I third_party -I third_party/Eigen \
    script/robustness_bench.cpp \
    src/watermark_core.cpp src/dct.cpp src/dwt.cpp src/color_convert.cpp src/image_io.cpp \
    -o build/robustness_bench

./build/robustness_bench
