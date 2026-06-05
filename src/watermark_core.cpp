#include "watermark_core.hpp"
#include "color_convert.hpp"
#include "dwt.hpp"
#include "dct.hpp"
#include <Eigen/SVD>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace bwm {

// Deterministic, toolchain-independent PRNG + shuffle.
//
// We deliberately do NOT use std::shuffle / std::uniform_int_distribution: the
// standard does not fix their algorithms, so a future libc++/emscripten change
// could silently alter block placement and make previously embedded watermarks
// unreadable. SplitMix64 + Fisher-Yates below is pure integer arithmetic and
// produces identical output on every platform and toolchain, forever. (Pinned by
// the golden test in script/verify_core.cpp.)
struct SplitMix64 {
    uint64_t state;
    explicit SplitMix64(uint64_t seed) : state(seed) {}
    uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

// In-place Fisher-Yates with a fixed seed. Modulo bias is irrelevant here (this
// scrambles bit/block positions, not crypto), and keeping it makes the result
// trivially reproducible.
template <typename T>
static void portableShuffle(std::vector<T>& v, int password) {
    SplitMix64 rng(static_cast<uint64_t>(static_cast<uint32_t>(password)));
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = static_cast<size_t>(rng.next() % i);  // j in [0, i)
        std::swap(v[i - 1], v[j]);
    }
}

// Apply (or undo) the password-seeded watermark permutation.
// Templated so it can carry soft (double) confidence values on the way out, not
// just hard bits on the way in. The permutation must be generated identically to
// generateBlockIndices (same seed + portableShuffle) so embed and extract agree.
template <typename T>
static std::vector<T> permuteBits(const std::vector<T>& vals, int password, bool inverse) {
    size_t n = vals.size();
    if (n == 0) return vals;

    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    portableShuffle(indices, password);

    std::vector<T> result(n);
    if (inverse) {
        for (size_t i = 0; i < n; ++i) result[indices[i]] = vals[i];
    } else {
        for (size_t i = 0; i < n; ++i) result[i] = vals[indices[i]];
    }
    return result;
}

// ---- Self-describing payload format ------------------------------------------
// Header (8 bytes) embedded before the payload so extraction needs no length:
//   [magic:16][version:8][flags:8][length:16][crc16:16]
// magic + crc gate "is there really a watermark for this password" and "is the
// payload intact". The header is its own redundant+scrambled chunk in the leading
// blocks, so it can be decoded before the payload length is known.
static const uint16_t BWM_MAGIC = 0xBA5E;
static const uint8_t  BWM_FMT_VERSION = 2;  // 2 = deterministic (SplitMix64) block order
static const size_t   BWM_HEADER_BYTES = 8;

// Guards the Eigen double-precision Y/U/V buffers (24 bytes/px) plus DWT/DCT
// temporaries against the 1 GB wasm heap. Beyond this we raise a catchable error
// instead of letting the wasm instance abort on a bad allocation.
static const int64_t BWM_MAX_PIXELS = 10000000;  // ~10 megapixels

static void checkPixelLimit(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid image dimensions");
    }
    if (static_cast<int64_t>(width) * static_cast<int64_t>(height) > BWM_MAX_PIXELS) {
        throw std::runtime_error("Image too large: maximum is ~10 megapixels");
    }
}

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over the payload bytes.
static uint16_t crc16_ccitt(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : data) {
        crc ^= static_cast<uint16_t>(b) << 8;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

static std::vector<uint8_t> bytesToBits(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> bits;
    bits.reserve(bytes.size() * 8);
    for (uint8_t by : bytes) {
        for (int i = 7; i >= 0; --i) bits.push_back((by >> i) & 1);
    }
    return bits;
}

static std::vector<uint8_t> bitsToBytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes;
    bytes.reserve(bits.size() / 8);
    for (size_t i = 0; i + 7 < bits.size(); i += 8) {
        uint8_t c = 0;
        for (int j = 0; j < 8; ++j) c = (c << 1) | (bits[i + j] & 1);
        bytes.push_back(c);
    }
    return bytes;
}

BlindWatermarkCore::BlindWatermarkCore(const WatermarkConfig& config)
    : config_(config) {
    // Clamp nonsensical values that would otherwise divide by zero, produce NaNs,
    // or silently embed nothing. Applies to every entry point (WASM + native FFI).
    if (!(config_.d1 > 0.0))     config_.d1 = 36.0;  // used as fmod/floor divisor
    if (config_.d2 < 0.0)        config_.d2 = 0.0;
    if (config_.blockSize < 1)   config_.blockSize = 4;
    if (config_.dwtLevel < 1)    config_.dwtLevel = 1;
    if (config_.redundancy < 1)  config_.redundancy = 1;
}

void BlindWatermarkCore::setImage(const Image& img) {
    if (img.empty() || (img.channels != 3 && img.channels != 4)) {
        throw std::runtime_error("Invalid image: must be RGB (3) or RGBA (4) channels");
    }
    checkPixelLimit(img.width, img.height);

    imgWidth_ = img.width;
    imgHeight_ = img.height;

    // Preserve the alpha plane so transparency survives the round-trip; the
    // watermark itself only touches luminance (Y).
    hasAlpha_ = (img.channels == 4);
    if (hasAlpha_) {
        size_t n = static_cast<size_t>(img.width) * img.height;
        alpha_.resize(n);
        for (size_t p = 0; p < n; ++p) {
            alpha_[p] = img.data[p * 4 + 3];
        }
    } else {
        alpha_.clear();
    }

    // Convert RGB(A) to YUV (alpha ignored here, re-attached in embed())
    rgbToYuv(img.data, img.width, img.height, Y_, U_, V_, img.channels);
}

void BlindWatermarkCore::setWatermarkText(const std::string& text) {
    watermarkBits_ = textToBits(text);
    wmImgHeight_ = 0;
    wmImgWidth_ = 0;
}

void BlindWatermarkCore::setWatermarkImage(const Image& img) {
    watermarkBits_ = imageToBits(img);
    wmImgHeight_ = img.height;
    wmImgWidth_ = img.width;
}

void BlindWatermarkCore::setWatermarkBits(const std::vector<uint8_t>& bits) {
    watermarkBits_ = bits;
    wmImgHeight_ = 0;
    wmImgWidth_ = 0;
}

std::vector<uint8_t> BlindWatermarkCore::textToBits(const std::string& text) {
    std::vector<uint8_t> bits;
    bits.reserve(text.size() * 8);

    for (char c : text) {
        for (int i = 7; i >= 0; --i) {
            bits.push_back((static_cast<unsigned char>(c) >> i) & 1);
        }
    }
    return bits;
}

std::string BlindWatermarkCore::bitsToText(const std::vector<uint8_t>& bits) {
    std::string text;
    text.reserve(bits.size() / 8);

    for (size_t i = 0; i + 7 < bits.size(); i += 8) {
        unsigned char c = 0;
        for (int j = 0; j < 8; ++j) {
            c = (c << 1) | (bits[i + j] & 1);
        }
        // Keep every decoded byte. Dropping '\0' would shorten/misalign the whole
        // string on a single bit error, hiding the corruption instead of showing it.
        text.push_back(static_cast<char>(c));
    }
    return text;
}

std::vector<uint8_t> BlindWatermarkCore::imageToBits(const Image& img) {
    std::vector<uint8_t> bits;

    if (img.empty()) {
        return bits;
    }

    bits.reserve(static_cast<size_t>(img.width) * img.height);

    // Convert to grayscale if needed and binarize
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            double gray;
            if (img.channels == 1) {
                gray = img.at(x, y, 0);
            } else {
                // RGB to grayscale
                double r = img.at(x, y, 0);
                double g = img.at(x, y, 1);
                double b = img.at(x, y, 2);
                gray = 0.299 * r + 0.587 * g + 0.114 * b;
            }
            // Binarize with threshold 128
            bits.push_back(gray > 128 ? 1 : 0);
        }
    }

    return bits;
}

Image BlindWatermarkCore::bitsToImage(const std::vector<uint8_t>& bits, int height, int width) {
    Image img;
    img.allocate(width, height, 1);

    size_t idx = 0;
    for (int y = 0; y < height && idx < bits.size(); ++y) {
        for (int x = 0; x < width && idx < bits.size(); ++x, ++idx) {
            img.set(x, y, 0, bits[idx] ? 255 : 0);
        }
    }

    return img;
}

std::vector<std::pair<int, int>> BlindWatermarkCore::buildBlockOrder(int password, int llRows, int llCols) const {
    int blocksPerRow = llCols / config_.blockSize;
    int blocksPerCol = llRows / config_.blockSize;

    std::vector<std::pair<int, int>> all;
    all.reserve(static_cast<size_t>(blocksPerRow) * blocksPerCol);
    for (int r = 0; r < blocksPerCol; ++r) {
        for (int c = 0; c < blocksPerRow; ++c) {
            all.emplace_back(r, c);
        }
    }

    // Deterministic password-seeded shuffle. A prefix of this order is stable
    // regardless of how many blocks are taken, so a fixed-size header can be read
    // before the payload length is known.
    portableShuffle(all, password);
    return all;
}

void BlindWatermarkCore::generateBlockIndices(int numBlocks, int password, int llRows, int llCols) {
    std::vector<std::pair<int, int>> order = buildBlockOrder(password, llRows, llCols);
    if (static_cast<int>(order.size()) < numBlocks) {
        throw std::runtime_error("Image too small for watermark");
    }
    blockIndices_.assign(order.begin(), order.begin() + numBlocks);
}

std::vector<size_t> BlindWatermarkCore::pinnedPermutation(int password, size_t count) {
    std::vector<size_t> idx(count);
    std::iota(idx.begin(), idx.end(), 0);
    portableShuffle(idx, password);
    return idx;
}

void BlindWatermarkCore::embedQimBlock(Eigen::MatrixXd& LL, int blockRow, int blockCol, uint8_t bit) const {
    Eigen::MatrixXd block = LL.block(blockRow, blockCol, config_.blockSize, config_.blockSize);
    Eigen::MatrixXd dctBlock = dct2d(block);

    Eigen::JacobiSVD<Eigen::MatrixXd> svd(dctBlock, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::VectorXd S = svd.singularValues();

    // QIM: s = (floor(s / d) + 0.25 + 0.5*bit) * d  -> centred with margin d/2.
    double b = bit ? 1.0 : 0.0;
    S(0) = (std::floor(S(0) / config_.d1) + 0.25 + 0.5 * b) * config_.d1;
    if (config_.d2 > 0 && S.size() > 1) {
        S(1) = (std::floor(S(1) / config_.d2) + 0.25 + 0.5 * b) * config_.d2;
    }

    Eigen::MatrixXd newBlock = idct2d(svd.matrixU() * S.asDiagonal() * svd.matrixV().transpose());
    LL.block(blockRow, blockCol, config_.blockSize, config_.blockSize) = newBlock;
}

double BlindWatermarkCore::readQimBlockSoft(const Eigen::MatrixXd& LL, int blockRow, int blockCol) const {
    Eigen::MatrixXd block = LL.block(blockRow, blockCol, config_.blockSize, config_.blockSize);
    Eigen::MatrixXd dctBlock = dct2d(block);

    Eigen::JacobiSVD<Eigen::MatrixXd> svd(dctBlock, Eigen::ComputeThinU | Eigen::ComputeThinV);
    Eigen::VectorXd S = svd.singularValues();

    double wm = (std::fmod(S(0), config_.d1) > config_.d1 / 2.0) ? 1.0 : 0.0;
    if (config_.d2 > 0 && S.size() > 1) {
        double tmp = (std::fmod(S(1), config_.d2) > config_.d2 / 2.0) ? 1.0 : 0.0;
        wm = (wm * 3.0 + tmp) / 4.0;  // 3:1 soft vote, combined after redundancy averaging
    }
    return wm;
}

void BlindWatermarkCore::embedChunk(Eigen::MatrixXd& LL, const std::vector<std::pair<int, int>>& order,
                                    size_t blockOffset, const std::vector<uint8_t>& bits) const {
    int R = config_.redundancy;
    std::vector<uint8_t> redundant;
    redundant.reserve(bits.size() * R);
    for (uint8_t b : bits) {
        for (int r = 0; r < R; ++r) redundant.push_back(b);
    }

    std::vector<uint8_t> scrambled = permuteBits(redundant, config_.passwordWm, false);
    for (size_t i = 0; i < scrambled.size(); ++i) {
        const std::pair<int, int>& blk = order[blockOffset + i];
        embedQimBlock(LL, blk.first * config_.blockSize, blk.second * config_.blockSize, scrambled[i]);
    }
}

std::vector<uint8_t> BlindWatermarkCore::readChunk(const Eigen::MatrixXd& LL,
                                                   const std::vector<std::pair<int, int>>& order,
                                                   size_t blockOffset, size_t numBits) const {
    int R = config_.redundancy;
    size_t total = numBits * R;

    std::vector<double> soft;
    soft.reserve(total);
    for (size_t i = 0; i < total; ++i) {
        const std::pair<int, int>& blk = order[blockOffset + i];
        soft.push_back(readQimBlockSoft(LL, blk.first * config_.blockSize, blk.second * config_.blockSize));
    }

    std::vector<double> unscrambled = permuteBits(soft, config_.passwordWm, true);

    std::vector<uint8_t> out;
    out.reserve(numBits);
    for (size_t i = 0; i < numBits; ++i) {
        double sum = 0.0;
        for (int r = 0; r < R; ++r) sum += unscrambled[i * R + r];
        out.push_back(sum / R >= 0.5 ? 1 : 0);
    }
    return out;
}

void BlindWatermarkCore::embedBits(Eigen::MatrixXd& channel, const std::vector<uint8_t>& bits) {
    if (bits.empty()) {
        return;
    }

    // Store original dimensions for cropping after IDWT
    int origRows = static_cast<int>(channel.rows());
    int origCols = static_cast<int>(channel.cols());

    // Apply multi-level DWT
    std::vector<DwtResult> dwtLevels = dwt2d_multilevel(channel, config_.dwtLevel);

    if (dwtLevels.empty()) {
        throw std::runtime_error("DWT failed");
    }

    // Get the LL subband at deepest level
    Eigen::MatrixXd& LL = dwtLevels.back().LL;
    int llRows = static_cast<int>(LL.rows());
    int llCols = static_cast<int>(LL.cols());

    // Embed the (redundant, scrambled) bits into the leading blocks of the
    // deterministic order. Same layout as before, now via the shared helper.
    std::vector<std::pair<int, int>> order = buildBlockOrder(config_.passwordImg, llRows, llCols);
    if (order.size() < bits.size() * config_.redundancy) {
        throw std::runtime_error("Image too small for watermark");
    }
    embedChunk(LL, order, 0, bits);

    // Update dwtLevels with modified LL
    dwtLevels.back().LL = LL;

    // Reconstruct image with inverse DWT
    Eigen::MatrixXd reconstructed = idwt2d_multilevel(dwtLevels);

    // Crop back to original dimensions (DWT may have padded)
    channel = reconstructed.block(0, 0, origRows, origCols);
}

std::vector<uint8_t> BlindWatermarkCore::extractBitsFromChannel(const Eigen::MatrixXd& channel, size_t numBits) {
    // Apply multi-level DWT
    std::vector<DwtResult> dwtLevels = dwt2d_multilevel(channel, config_.dwtLevel);

    if (dwtLevels.empty()) {
        throw std::runtime_error("DWT failed");
    }

    // Get the LL subband at deepest level
    const Eigen::MatrixXd& LL = dwtLevels.back().LL;
    int llRows = static_cast<int>(LL.rows());
    int llCols = static_cast<int>(LL.cols());

    std::vector<std::pair<int, int>> order = buildBlockOrder(config_.passwordImg, llRows, llCols);
    if (order.size() < numBits * config_.redundancy) {
        throw std::runtime_error("Image too small for watermark");
    }
    return readChunk(LL, order, 0, numBits);
}

Image BlindWatermarkCore::assembleResult(const Eigen::MatrixXd& Yembedded) const {
    std::vector<uint8_t> rgb;
    yuvToRgb(Yembedded, U_, V_, rgb);

    Image result;
    result.width = imgWidth_;
    result.height = imgHeight_;

    if (hasAlpha_) {
        // Interleave the watermarked RGB with the original alpha -> RGBA output.
        result.channels = 4;
        size_t n = static_cast<size_t>(imgWidth_) * imgHeight_;
        result.data.resize(n * 4);
        for (size_t p = 0; p < n; ++p) {
            result.data[p * 4 + 0] = rgb[p * 3 + 0];
            result.data[p * 4 + 1] = rgb[p * 3 + 1];
            result.data[p * 4 + 2] = rgb[p * 3 + 2];
            result.data[p * 4 + 3] = alpha_[p];
        }
    } else {
        result.channels = 3;
        result.data = std::move(rgb);
    }

    return result;
}

Image BlindWatermarkCore::embed() {
    if (Y_.size() == 0) {
        throw std::runtime_error("No image set");
    }
    if (watermarkBits_.empty()) {
        throw std::runtime_error("No watermark set");
    }

    Eigen::MatrixXd Y_embedded = Y_;
    embedBits(Y_embedded, watermarkBits_);
    return assembleResult(Y_embedded);
}

Image BlindWatermarkCore::embedSelfDescribing(const std::vector<uint8_t>& payload, bool isText) {
    if (Y_.size() == 0) {
        throw std::runtime_error("No image set");
    }
    if (payload.size() > 0xFFFF) {
        throw std::runtime_error("Payload too large (max 65535 bytes)");
    }

    Eigen::MatrixXd Y_embedded = Y_;
    int origRows = static_cast<int>(Y_embedded.rows());
    int origCols = static_cast<int>(Y_embedded.cols());

    std::vector<DwtResult> dwtLevels = dwt2d_multilevel(Y_embedded, config_.dwtLevel);
    if (dwtLevels.empty()) {
        throw std::runtime_error("DWT failed");
    }
    Eigen::MatrixXd& LL = dwtLevels.back().LL;
    std::vector<std::pair<int, int>> order =
        buildBlockOrder(config_.passwordImg, static_cast<int>(LL.rows()), static_cast<int>(LL.cols()));

    // Build the 8-byte header.
    uint16_t crc = crc16_ccitt(payload);
    uint16_t length = static_cast<uint16_t>(payload.size());
    std::vector<uint8_t> header = {
        static_cast<uint8_t>(BWM_MAGIC >> 8), static_cast<uint8_t>(BWM_MAGIC & 0xFF),
        BWM_FMT_VERSION,
        static_cast<uint8_t>(isText ? 1 : 0),
        static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length & 0xFF),
        static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc & 0xFF),
    };

    std::vector<uint8_t> headerBits = bytesToBits(header);
    std::vector<uint8_t> payloadBits = bytesToBits(payload);
    size_t headerBlocks = headerBits.size() * config_.redundancy;
    size_t needed = headerBlocks + payloadBits.size() * config_.redundancy;
    if (order.size() < needed) {
        throw std::runtime_error("Image too small for watermark (header + payload)");
    }

    // Header and payload are independently scrambled chunks, so the fixed-size
    // header can be decoded before the payload length is known.
    embedChunk(LL, order, 0, headerBits);
    embedChunk(LL, order, headerBlocks, payloadBits);

    dwtLevels.back().LL = LL;
    Eigen::MatrixXd reconstructed = idwt2d_multilevel(dwtLevels);
    Y_embedded = reconstructed.block(0, 0, origRows, origCols);

    return assembleResult(Y_embedded);
}

ExtractResult BlindWatermarkCore::extractSelfDescribing(const Image& img) {
    if (img.empty() || (img.channels != 3 && img.channels != 4)) {
        throw std::runtime_error("Invalid image: must be RGB (3) or RGBA (4) channels");
    }
    checkPixelLimit(img.width, img.height);

    ExtractResult res;

    Eigen::MatrixXd Y, U, V;
    rgbToYuv(img.data, img.width, img.height, Y, U, V, img.channels);

    std::vector<DwtResult> dwtLevels = dwt2d_multilevel(Y, config_.dwtLevel);
    if (dwtLevels.empty()) {
        throw std::runtime_error("DWT failed");
    }
    const Eigen::MatrixXd& LL = dwtLevels.back().LL;
    std::vector<std::pair<int, int>> order =
        buildBlockOrder(config_.passwordImg, static_cast<int>(LL.rows()), static_cast<int>(LL.cols()));

    size_t headerBits = BWM_HEADER_BYTES * 8;
    size_t headerBlocks = headerBits * config_.redundancy;
    if (order.size() < headerBlocks) {
        return res;  // image too small to even hold a header -> not found
    }

    std::vector<uint8_t> hbytes = bitsToBytes(readChunk(LL, order, 0, headerBits));
    uint16_t magic = (static_cast<uint16_t>(hbytes[0]) << 8) | hbytes[1];
    if (magic != BWM_MAGIC) {
        return res;  // no watermark, or wrong password -> not found
    }

    res.found = true;
    res.version = hbytes[2];
    res.isText = (hbytes[3] & 1) != 0;
    uint16_t length = (static_cast<uint16_t>(hbytes[4]) << 8) | hbytes[5];
    uint16_t crc = (static_cast<uint16_t>(hbytes[6]) << 8) | hbytes[7];

    size_t payloadBits = static_cast<size_t>(length) * 8;
    size_t needed = headerBlocks + payloadBits * config_.redundancy;
    if (order.size() < needed) {
        // Header decoded but claims more payload than the image can hold -> corrupt.
        return res;  // found = true, valid = false
    }

    res.payload = bitsToBytes(readChunk(LL, order, headerBlocks, payloadBits));
    res.valid = (crc16_ccitt(res.payload) == crc);
    return res;
}

std::string BlindWatermarkCore::extractText(const Image& img, size_t wmLength) {
    std::vector<uint8_t> bits = extractBits(img, wmLength);
    return bitsToText(bits);
}

Image BlindWatermarkCore::extractImage(const Image& img, int wmHeight, int wmWidth) {
    size_t numBits = static_cast<size_t>(wmHeight) * wmWidth;
    std::vector<uint8_t> bits = extractBits(img, numBits);
    return bitsToImage(bits, wmHeight, wmWidth);
}

std::vector<uint8_t> BlindWatermarkCore::extractBits(const Image& img, size_t wmLength) {
    if (img.empty() || (img.channels != 3 && img.channels != 4)) {
        throw std::runtime_error("Invalid image: must be RGB (3) or RGBA (4) channels");
    }
    checkPixelLimit(img.width, img.height);

    // Convert RGB(A) to YUV (alpha ignored for extraction)
    Eigen::MatrixXd Y, U, V;
    rgbToYuv(img.data, img.width, img.height, Y, U, V, img.channels);

    // Update dimensions for block index generation
    imgWidth_ = img.width;
    imgHeight_ = img.height;

    // Extract bits from Y channel
    return extractBitsFromChannel(Y, wmLength);
}

} // namespace bwm
