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

    /// Embed bits into DWT-DCT-SVD domain
    void embedBits(Eigen::MatrixXd& channel, const std::vector<uint8_t>& bits);

    /// Extract bits from DWT-DCT-SVD domain
    std::vector<uint8_t> extractBitsFromChannel(const Eigen::MatrixXd& channel, size_t numBits);

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
