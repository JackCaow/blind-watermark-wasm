cmake_minimum_required(VERSION 3.18)
project(blind_watermark_wasm VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# libwebp path
set(WEBP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libwebp-1.3.2")

# libwebp source files
file(GLOB WEBP_DEC_SOURCES "${WEBP_ROOT}/src/dec/*.c")
file(GLOB WEBP_DSP_SOURCES "${WEBP_ROOT}/src/dsp/*.c")
file(GLOB WEBP_UTILS_SOURCES "${WEBP_ROOT}/src/utils/*.c")
file(GLOB WEBP_ENC_SOURCES "${WEBP_ROOT}/src/enc/*.c")
file(GLOB SHARPYUV_SOURCES "${WEBP_ROOT}/sharpyuv/*.c")

# Source files
set(SOURCES
    src/image_io.cpp
    src/color_convert.cpp
    src/dct.cpp
    src/dwt.cpp
    src/watermark_core.cpp
    src/wasm_bindings.cpp
    ${WEBP_DEC_SOURCES}
    ${WEBP_DSP_SOURCES}
    ${WEBP_UTILS_SOURCES}
    ${WEBP_ENC_SOURCES}
    ${SHARPYUV_SOURCES}
)

# Create executable (Emscripten creates .js and .wasm)
add_executable(blind_watermark ${SOURCES})

# Include directories
target_include_directories(blind_watermark PRIVATE
    src
    third_party
    third_party/Eigen
    ${WEBP_ROOT}
    ${WEBP_ROOT}/src
)

# Compiler definitions
target_compile_definitions(blind_watermark PRIVATE
    BWM_HAVE_WEBP=1
    EIGEN_NO_DEBUG=1
    NDEBUG=1
)

# Compiler flags
target_compile_options(blind_watermark PRIVATE
    -O3
    -ffast-math
)

# Emscripten specific settings
if(EMSCRIPTEN)
    set_target_properties(blind_watermark PROPERTIES
        OUTPUT_NAME "blind_watermark"
        SUFFIX ".js"
    )

    set(EMSCRIPTEN_LINK_FLAGS
        "--bind"
        "-sWASM=1"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=BlindWatermarkModule"
        "-sENVIRONMENT=web,node"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sMAXIMUM_MEMORY=1GB"
        "-sEXPORTED_RUNTIME_METHODS=[ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8]"
        "-sNO_EXIT_RUNTIME=1"
        "-sFILESYSTEM=0"
        "-sSINGLE_FILE=1"
        "-O3"
    )
    target_link_options(blind_watermark PRIVATE ${EMSCRIPTEN_LINK_FLAGS})
endif()
