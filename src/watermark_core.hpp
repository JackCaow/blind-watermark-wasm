#ifndef WATERMARK_CORE_HPP
#define WATERMARK_CORE_HPP

#include <Eigen/Dense>
#include <vector>
#include <cstdint>
#include <random>
#include "image_io.hpp"

namespace bwm {

/// Watermark configuration
struct WatermarkConfig {
    int passwordWm = 1;     // Password for watermark scrambling
    int passwordImg = 1;    // Password for image block scrambling
    double d1 = 36.0;       // Embedding strength for first singular value
    // Embedding strength for the SECOND singular value. Disabled by default (0):
    // s1 carries higher-frequency energy and is far more fragile under noise/JPEG
    // than s0, so enabling it (d2 > 0) measurably lowers robustness. Kept as an
    // opt-in knob; when > 0 its vote is combined softly (see extractBitsFromChannel).
    double d2 = 0.0;
    int blockSize = 4;      // DCT block size (4 for images <= 1024px)
    int dwtLevel = 1;       // DWT decomposition level (1 level like original)
    int redundancy = 3;     // Bit redundancy factor for JPG robustness (each bit embedded N times)
};

/// Result of a self-describing extraction (no payload length needed up front).
struct ExtractResult {
    bool found = false;               // a watermark with the matching password was detected (header magic ok)
    bool valid = false;               // payload checksum (CRC-16) matched
    int version = 0;                  // payload format version read from the header
    bool isText = false;              // payload was embedded as UTF-8 text
    std::vector<uint8_t> payload;     // raw payload bytes (UTF-8 if isText)
};

/// Blind watermark class using DWT-DCT-SVD algorithm
class BlindWatermarkCore {
public:
    BlindWatermarkCore(const WatermarkConfig& config = WatermarkConfig());
    ~BlindWatermarkCore() = default;

    /// Set the carrier image
    /// @param img RGB image
    void setImage(const Image& img);

    /// Set text watermark (converted to bits)
    /// @param text UTF-8 text
    void setWatermarkText(const std::string& text);

    /// Set image watermark (binary image)
    /// @param img Grayscale image (will be binarized)
    void setWatermarkImage(const Image& img);

    /// Set bit array watermark
    /// @param bits Watermark bits
    void setWatermarkBits(const std::vector<uint8_t>& bits);

    /// Get watermark bit length
    size_t getWatermarkSize() const { return watermarkBits_.size(); }

    /// Embed watermark into image
    /// @return Watermarked image
    Image embed();

    /// Extract text watermark
    /// @param img Watermarked image
    /// @param wmLength Expected watermark bit length
    /// @return Extracted text
    std::string extractText(const Image& img, size_t wmLength);

    /// Extract image watermark
    /// @param img Watermarked image
    /// @param wmHeight Watermark height
    /// @param wmWidth Watermark width
    /// @return Extracted watermark image
    Image extractImage(const Image& img, int wmHeight, int wmWidth);

    /// Extract bit array watermark
    /// @param img Watermarked image
    /// @param wmLength Expected watermark bit length
    /// @return Extracted bits
    std::vector<uint8_t> extractBits(const Image& img, size_t wmLength);

    /// Embed a self-describing payload (header + payload) so extraction needs no
    /// length up front. Requires setImage() first.
    /// @param payload Raw bytes to embed
    /// @param isText  Tag the payload as UTF-8 text (affects how callers decode it)
    /// @return Watermarked image
    Image embedSelfDescribing(const std::vector<uint8_t>& payload, bool isText);

    /// Extract a self-describing payload without knowing its length.
    /// @param img Watermarked image
    /// @return Result with found/valid flags and the payload (see ExtractResult)
    ExtractResult extractSelfDescribing(const Image& img);

private:
    WatermarkConfig config_;

    // Carrier image data
    int imgWidth_ = 0;
    int imgHeight_ = 0;
    Eigen::MatrixXd Y_;  // Y channel (luminance)
    Eigen::MatrixXd U_;  // U channel
    Eigen::MatrixXd V_;  // V channel

    bool hasAlpha_ = false;       // carrier had an alpha channel
    std::vector<uint8_t> alpha_;  // original alpha plane (W*H), preserved across embed

    // Watermark data
    std::vector<uint8_t> watermarkBits_;
    int wmImgHeight_ = 0;  // Original watermark image height
    int wmImgWidth_ = 0;   // Original watermark image width

    // Block indices for embedding
    std::vector<std::pair<int, int>> blockIndices_;

    /// Generate block indices based on password and actual LL dimensions
    void generateBlockIndices(int numBlocks, int password, int llRows, int llCols);

    /// Build the full deterministic (password-shuffled) block order for an LL plane.
    /// A prefix of this order is stable regardless of how many blocks are used,
    /// which is what lets a fixed-size header be read before the payload length.
    std::vector<std::pair<int, int>> buildBlockOrder(int password, int llRows, int llCols) const;

    /// Embed one decided bit into the QIM block at (blockRow, blockCol).
    void embedQimBlock(Eigen::MatrixXd& LL, int blockRow, int blockCol, uint8_t bit) const;

    /// Read the soft QIM confidence (in [0,1]) from the block at (blockRow, blockCol).
    double readQimBlockSoft(const Eigen::MatrixXd& LL, int blockRow, int blockCol) const;

    /// Embed `bits` as a redundant+scrambled chunk into `order` starting at blockOffset.
    void embedChunk(Eigen::MatrixXd& LL, const std::vector<std::pair<int, int>>& order,
                    size_t blockOffset, const std::vector<uint8_t>& bits) const;

    /// Read `numBits` logical bits from `order` starting at blockOffset.
    std::vector<uint8_t> readChunk(const Eigen::MatrixXd& LL, const std::vector<std::pair<int, int>>& order,
                                   size_t blockOffset, size_t numBits) const;

    /// Embed bits into DWT-DCT-SVD domain
    void embedBits(Eigen::MatrixXd& channel, const std::vector<uint8_t>& bits);

    /// Extract bits from DWT-DCT-SVD domain
    std::vector<uint8_t> extractBitsFromChannel(const Eigen::MatrixXd& channel, size_t numBits);

    /// Assemble the final Image from a watermarked Y channel (+ stored U/V/alpha).
    Image assembleResult(const Eigen::MatrixXd& Yembedded) const;

    /// Text to bits conversion
    static std::vector<uint8_t> textToBits(const std::string& text);

    /// Bits to text conversion
    static std::string bitsToText(const std::vector<uint8_t>& bits);

    /// Image to bits conversion (binarize)
    static std::vector<uint8_t> imageToBits(const Image& img);

    /// Bits to image conversion
    static Image bitsToImage(const std::vector<uint8_t>& bits, int height, int width);
};

} // namespace bwm

#endif // WATERMARK_CORE_HPP
