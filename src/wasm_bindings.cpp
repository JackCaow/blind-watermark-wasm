#include <emscripten/bind.h>
#include <emscripten/val.h>
#include "watermark_core.hpp"
#include "image_io.hpp"
#include <vector>
#include <string>
#include <cstdint>

using namespace emscripten;
using namespace bwm;

// Global config, set via setConfig() immediately before each embed/extract call.
// Safe because WASM runs single-threaded and the JS wrapper always pairs
// setConfig() with its operation synchronously (no await between them), so calls
// never interleave. Kept global to avoid widening every exported function's
// signature; values are sanitised in the BlindWatermarkCore constructor.
static WatermarkConfig g_config;

// Copy bytes into a JS-owned Uint8Array.
//
// IMPORTANT: typed_memory_view returns a *view* over the WASM heap, not a copy.
// Returning such a view directly is a use-after-free: the backing std::vector is
// destroyed when the function returns, and with ALLOW_MEMORY_GROWTH the heap can
// be detached entirely, invalidating the view. We copy into an array that JS owns
// while the source vector is still alive.
static val toUint8Array(const std::vector<uint8_t>& bytes) {
    val view = val(typed_memory_view(bytes.size(), bytes.data()));
    val out = val::global("Uint8Array").new_(static_cast<unsigned>(bytes.size()));
    out.call<void>("set", view);
    return out;
}

// JPEG has no alpha channel; flatten RGBA -> RGB so a watermarked image with
// transparency still encodes (PNG/WebP keep the alpha as-is).
static Image stripAlphaForJpeg(Image img, const std::string& fmt) {
    if (fmt != "jpg" || img.channels != 4) {
        return img;
    }
    Image rgb;
    rgb.allocate(img.width, img.height, 3);
    size_t n = static_cast<size_t>(img.width) * img.height;
    for (size_t p = 0; p < n; ++p) {
        rgb.data[p * 3 + 0] = img.data[p * 4 + 0];
        rgb.data[p * 3 + 1] = img.data[p * 4 + 1];
        rgb.data[p * 3 + 2] = img.data[p * 4 + 2];
    }
    return rgb;
}

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

        // Decode image (preserve alpha so transparency survives the round-trip)
        Image img;
        if (!loadImageRGBAFromMemory(imageBytes.data(), imageBytes.size(), img)) {
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
        result = stripAlphaForJpeg(std::move(result), format);

        if (!encodeImage(result, format, outBytes, 95)) {
            val errorObj = val::object();
            errorObj.set("error", val(std::string("Failed to encode image")));
            return errorObj;
        }

        // Calculate watermark bit length for extraction
        size_t wmBitLength = watermarkText.size() * 8;

        // Return result object with Uint8Array
        val resultObj = val::object();
        resultObj.set("imageData", toUint8Array(outBytes));
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

        // Decode image (preserve alpha so transparency survives the round-trip)
        Image img;
        if (!loadImageRGBAFromMemory(imageBytes.data(), imageBytes.size(), img)) {
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
        result = stripAlphaForJpeg(std::move(result), format);

        if (!encodeImage(result, format, outBytes, 95)) {
            val errorObj = val::object();
            errorObj.set("error", val(std::string("Failed to encode image")));
            return errorObj;
        }

        // Return result
        val resultObj = val::object();
        resultObj.set("imageData", toUint8Array(outBytes));
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

// Embed a self-describing payload (header + payload) so extraction needs no length.
// payloadVal is a Uint8Array; isText tags it as UTF-8 for the caller's benefit.
val embedSelfDescribingBytes(val imageDataVal, val payloadVal, bool isText, const std::string& outputFormat) {
    try {
        std::vector<uint8_t> imageBytes = convertJSArrayToNumberVector<uint8_t>(imageDataVal);

        Image img;
        if (!loadImageRGBAFromMemory(imageBytes.data(), imageBytes.size(), img)) {
            val errorObj = val::object();
            errorObj.set("error", val(std::string("Failed to decode image")));
            return errorObj;
        }

        std::vector<uint8_t> payload = convertJSArrayToNumberVector<uint8_t>(payloadVal);

        BlindWatermarkCore core(g_config);
        core.setImage(img);
        Image result = core.embedSelfDescribing(payload, isText);

        std::vector<uint8_t> outBytes;
        std::string format = outputFormat;
        if (format == "jpeg") format = "jpg";
        result = stripAlphaForJpeg(std::move(result), format);

        if (!encodeImage(result, format, outBytes, 95)) {
            val errorObj = val::object();
            errorObj.set("error", val(std::string("Failed to encode image")));
            return errorObj;
        }

        val resultObj = val::object();
        resultObj.set("imageData", toUint8Array(outBytes));
        return resultObj;
    } catch (const std::exception& e) {
        val errorObj = val::object();
        errorObj.set("error", val(std::string(e.what())));
        return errorObj;
    }
}

// Extract a self-describing payload without knowing its length. Returns
// { found, valid, version, isText, payload(Uint8Array) } (or { found:false, error }).
val extractSelfDescribingBytes(val imageDataVal) {
    try {
        std::vector<uint8_t> imageBytes = convertJSArrayToNumberVector<uint8_t>(imageDataVal);

        Image img;
        if (!loadImageFromMemory(imageBytes.data(), imageBytes.size(), img)) {
            val o = val::object();
            o.set("found", val(false));
            o.set("error", val(std::string("Failed to decode image")));
            return o;
        }

        BlindWatermarkCore core(g_config);
        ExtractResult r = core.extractSelfDescribing(img);

        val o = val::object();
        o.set("found", val(r.found));
        o.set("valid", val(r.valid));
        o.set("version", val(r.version));
        o.set("isText", val(r.isText));
        o.set("payload", toUint8Array(r.payload));
        return o;
    } catch (const std::exception& e) {
        val o = val::object();
        o.set("found", val(false));
        o.set("error", val(std::string(e.what())));
        return o;
    }
}

EMSCRIPTEN_BINDINGS(blind_watermark) {
    function("setConfig", &setConfig);
    function("embedStringWatermark", &embedStringWatermarkBytes);
    function("extractStringWatermark", &extractStringWatermarkBytes);
    function("embedBinaryWatermark", &embedBinaryWatermarkBytes);
    function("extractBinaryWatermark", &extractBinaryWatermarkBytes);
    function("embedSelfDescribing", &embedSelfDescribingBytes);
    function("extractSelfDescribing", &extractSelfDescribingBytes);
}
