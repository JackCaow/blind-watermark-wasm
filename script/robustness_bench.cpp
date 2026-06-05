// Robustness benchmark: embed a watermark, subject it to real transforms
// (re-encode, JPEG quality sweep, scaling), and measure recovery. Turns the
// README's "robust to JPEG" claim into numbers — or disproves it.
//
// Build/run: bash script/run_bench.sh

#include "watermark_core.hpp"
#include "image_io.hpp"
#include <random>
#include <cstdio>
#include <vector>
#include <string>
#include <cmath>

using namespace bwm;

static Image makeImage(int w, int h) {
    Image img; img.allocate(w, h, 3);
    std::mt19937 rng(2024);
    std::uniform_int_distribution<int> jitter(-14, 14);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double base = 128 + 58 * std::sin(x * 0.07) + 42 * std::cos(y * 0.045) + 28 * std::sin((x + y) * 0.025);
            for (int c = 0; c < 3; ++c) {
                int v = (int)base + 12 * c + jitter(rng);
                img.set(x, y, c, (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v));
            }
        }
    return img;
}

// Bilinear resize of an RGB image.
static Image resizeRGB(const Image& s, int nw, int nh) {
    Image d; d.allocate(nw, nh, 3);
    for (int y = 0; y < nh; ++y) {
        double sy = (y + 0.5) * s.height / nh - 0.5; int y0 = (int)std::floor(sy); double fy = sy - y0;
        int y1 = std::min(std::max(y0 + 1, 0), s.height - 1); y0 = std::min(std::max(y0, 0), s.height - 1);
        for (int x = 0; x < nw; ++x) {
            double sx = (x + 0.5) * s.width / nw - 0.5; int x0 = (int)std::floor(sx); double fx = sx - x0;
            int x1 = std::min(std::max(x0 + 1, 0), s.width - 1); x0 = std::min(std::max(x0, 0), s.width - 1);
            for (int c = 0; c < 3; ++c) {
                double v = s.at(x0, y0, c) * (1 - fx) * (1 - fy) + s.at(x1, y0, c) * fx * (1 - fy)
                         + s.at(x0, y1, c) * (1 - fx) * fy + s.at(x1, y1, c) * fx * fy;
                d.set(x, y, c, (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v));
            }
        }
    }
    return d;
}

static std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// Extract from a (possibly transformed) image and report recovery vs the original payload.
static void report(const char* label, const Image& img, const std::vector<uint8_t>& payload) {
    BlindWatermarkCore ex;  // default config
    std::string status; int berNum = -1;
    try {
        ExtractResult r = ex.extractSelfDescribing(img);
        if (!r.found) status = "not found";
        else {
            // bit-error rate against the original payload (over the common length)
            size_t n = std::min(r.payload.size(), payload.size());
            int errs = 0, total = 0;
            for (size_t i = 0; i < n; ++i)
                for (int b = 0; b < 8; ++b) { total++; if (((r.payload[i] >> b) & 1) != ((payload[i] >> b) & 1)) errs++; }
            berNum = total ? (1000 * errs / total) : 0;  // per-mille
            status = r.valid ? "recovered (crc ok)" : (r.payload == payload ? "recovered" : "corrupt");
        }
    } catch (const std::exception& e) { status = std::string("error: ") + e.what(); }

    char ber[16]; if (berNum < 0) snprintf(ber, sizeof ber, "  -- "); else snprintf(ber, sizeof ber, "%4.1f%%", berNum / 10.0);
    printf("| %-26s | %-20s | %s |\n", label, status.c_str(), ber);
}

int main() {
    const int W = 512, H = 512;
    Image src = makeImage(W, H);
    auto payload = bytes("blind-watermark-wasm robustness probe \xE2\x9C\x93");  // ~40 bytes

    BlindWatermarkCore core;        // default config (redundancy 3, d1 36, d2 0)
    core.setImage(src);
    Image marked = core.embedSelfDescribing(payload, true);

    printf("\nPayload: %zu bytes into a %dx%d image (default config)\n\n", payload.size(), W, H);
    printf("| transform                  | result               |  BER  |\n");
    printf("|----------------------------|----------------------|-------|\n");

    // lossless baseline
    report("none (in-memory)", marked, payload);
    {
        std::vector<uint8_t> buf; encodeImage(marked, "png", buf, 0);
        Image x; loadImageFromMemory(buf.data(), buf.size(), x);
        report("PNG re-save (lossless)", x, payload);
    }
    // JPEG quality sweep
    for (int q : {95, 90, 85, 80, 70, 60, 50, 40}) {
        std::vector<uint8_t> buf; encodeImage(marked, "jpg", buf, q);
        Image x; loadImageFromMemory(buf.data(), buf.size(), x);
        char lbl[32]; snprintf(lbl, sizeof lbl, "JPEG q=%d", q);
        report(lbl, x, payload);
    }
    // scaling
    report("downscale 50% + upscale", resizeRGB(resizeRGB(marked, W/2, H/2), W, H), payload);
    report("resize to 90% (kept)", resizeRGB(marked, (int)(W*0.9), (int)(H*0.9)), payload);

    printf("\nBER = bit-error rate of the recovered payload. 'not found' = header magic\n");
    printf("failed (watermark unreadable). Scaling changes the block grid, so the mark\n");
    printf("does not survive resize/crop — embed before any resizing in your pipeline.\n");
    return 0;
}
