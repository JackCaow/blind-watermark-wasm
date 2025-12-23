#include <emscripten/bind.h>
#include <emscripten/val.h>
#include "watermark_core.hpp"
#include "image_io.hpp"
#include <vector>
#include <string>
#include <cstdint>

using namespace emscripten;
using namespace bwm;

// Global config
static WatermarkConfig g_config;

void setConfig(int passwordWm, int passwordImg, double d1, double d2, int blockSize, int dwtLevel, int redundancy) {
    g_config.passwordWm = passwordWm;
    g_config.passwordImg = passwordImg;
    g_config.d1 = d1;
    g_config.d2 = d2;
    g_config.blockSize = blockSize;
    g_config.dwtLevel = dwtLevel;
    g_config.redundancy = redundancy;
}

// Embed string watermark into image using Uint8Array input
val embedStringWatermarkBytes(val imageDataVal, const std::string& watermarkText, const std::string& outputFormat) {
    try {
        // Convert JavaScript Uint8Array to std::vector<uint8_t>
        std::vector<uint8_t> imageBytes = convertJSArrayToNumberVector<uint8_t>(imageDataVal);

        // Decode image
        Image img;
        if (!loadImageFromMemory(imageBytes.data(), imageBytes.size(), img)) {
            val errorObj = val::object();
            errorObj.set("error", val(std::string("Failed to decode image")));
            return errorObj;
        }

        // Create watermark core and embed
        BlindWatermarkCore core(g_config);
        core.setImage(img);
        core.setWatermarkText(watermarkText);

        Image result = core.embed();

        // Encode output
        std::vector<uint8_t> outBytes;
        std::string format = outputFormat;
        if (format == "jpeg") format = "jpg";

        if (!encodeImage(result, format, outBytes, 95)) {
            val errorObj = val::object();
            errorObj.set("error", val(std::string("Failed to encode image")));
            return errorObj;
        }

        // Calculate watermark bit length for extraction
        size_t wmBitLength = watermarkText.size() * 8;

        // Return result object with Uint8Array
        val resultObj = val::object();
        resultObj.set("imageData", val(typed_memory_view(outBytes.size(), outBytes.data())));
        resultObj.set("wmBitLength", val((int)wmBitLength));

        return resultObj;
    } catch (const std::exception& e) {
        val errorObj = val::object();
        errorObj.set("error", val(std::string(e.what())));
        return errorObj;
    }
}

// Extract string watermark from image using Uint8Array input
std::string extractStringWatermarkBytes(val imageDataVal, int wmBitLength) {
    try {
        // Convert JavaScript Uint8Array to std::vector<uint8_t>
        std::vector<uint8_t> imageBytes = convertJSArrayToNumberVector<uint8_t>(imageDataVal);

        // Decode image
        Image img;
        if (!loadImageFromMemory(imageBytes.data(), imageBytes.size(), img)) {
            return "";
        }

        // Create watermark core and extract
        BlindWatermarkCore core(g_config);
        std::string text = core.extractText(img, wmBitLength);

        return text;
    } catch (const std::exception& e) {
        return "";
    }
}

// Embed binary watermark using Uint8Array
val embedBinaryWatermarkBytes(val imageDataVal, const std::string& watermarkBits, const std::string& outputFormat) {
    try {
        // Convert JavaScript Uint8Array to std::vector<uint8_t>
        std::vector<uint8_t> imageBytes = convertJSArrayToNumberVector<uint8_t>(imageDataVal);

        // Decode image
        Image img;
        if (!loadImageFromMemory(imageBytes.data(), imageBytes.size(), img)) {
            val errorObj = val::object();
            errorObj.set("error", val(std::string("Failed to decode image")));
            return errorObj;
        }

        // Convert string bits to vector
        std::vector<uint8_t> bits;
        bits.reserve(watermarkBits.size());
        for (char c : watermarkBits) {
            bits.push_back(c == '1' ? 1 : 0);
        }

        // Create watermark core and embed
        BlindWatermarkCore core(g_config);
        core.setImage(img);
        core.setWatermarkBits(bits);

        Image result = core.embed();

        // Encode output
        std::vector<uint8_t> outBytes;
        std::string format = outputFormat;
        if (format == "jpeg") format = "jpg";

        if (!encodeImage(result, format, outBytes, 95)) {
            val errorObj = val::object();
            errorObj.set("error", val(std::string("Failed to encode image")));
            return errorObj;
        }

        // Return result
        val resultObj = val::object();
        resultObj.set("imageData", val(typed_memory_view(outBytes.size(), outBytes.data())));
        resultObj.set("wmBitLength", val((int)bits.size()));

        return resultObj;
    } catch (const std::exception& e) {
        val errorObj = val::object();
        errorObj.set("error", val(std::string(e.what())));
        return errorObj;
    }
}

// Extract binary watermark using Uint8Array
std::string extractBinaryWatermarkBytes(val imageDataVal, int wmBitLength) {
    try {
        // Convert JavaScript Uint8Array to std::vector<uint8_t>
        std::vector<uint8_t> imageBytes = convertJSArrayToNumberVector<uint8_t>(imageDataVal);

        // Decode image
        Image img;
        if (!loadImageFromMemory(imageBytes.data(), imageBytes.size(), img)) {
            return "";
        }

        // Create watermark core and extract
        BlindWatermarkCore core(g_config);
        std::vector<uint8_t> bits = core.extractBits(img, wmBitLength);

        // Convert to string
        std::string result;
        result.reserve(bits.size());
        for (uint8_t bit : bits) {
            result.push_back(bit ? '1' : '0');
        }

        return result;
    } catch (const std::exception& e) {
        return "";
    }
}

EMSCRIPTEN_BINDINGS(blind_watermark) {
    function("setConfig", &setConfig);
    function("embedStringWatermark", &embedStringWatermarkBytes);
    function("extractStringWatermark", &extractStringWatermarkBytes);
    function("embedBinaryWatermark", &embedBinaryWatermarkBytes);
    function("extractBinaryWatermark", &extractBinaryWatermarkBytes);
}
