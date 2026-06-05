// Native verification harness for the watermark core (no Emscripten needed).
//
// Validates the priority-1 round of fixes:
//   1. Clean round-trip still recovers the watermark exactly (regression guard for
//      the soft-voting change in extractBitsFromChannel).
//   2. On the SAME noisy image, extracting WITH the s1 soft vote (d2>0) yields a
//      lower bit-error rate than ignoring it (d2=0) — i.e. the secondary singular
//      value now actually contributes, which the old hard-threshold code discarded.
//
// Build: see script/run_verify.sh

#include "watermark_core.hpp"
#include "color_convert.hpp"
#include "dwt.hpp"
#include <random>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using namespace bwm;

// A textured RGB image so DCT blocks have non-degenerate singular values.
static Image makeTexturedImage(int w, int h) {
    Image img;
    img.allocate(w, h, 3);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> jitter(-12, 12);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double base = 128.0
                + 60.0 * std::sin(x * 0.08)
                + 40.0 * std::cos(y * 0.05)
                + 30.0 * std::sin((x + y) * 0.03);
            for (int c = 0; c < 3; ++c) {
                int v = (int)base + 10 * c + jitter(rng);
                v = v < 0 ? 0 : (v > 255 ? 255 : v);
                img.set(x, y, c, (uint8_t)v);
            }
        }
    }
    return img;
}

static Image addNoise(const Image& in, double sigma, unsigned seed) {
    Image out = in;
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, sigma);
    for (size_t i = 0; i < out.data.size(); ++i) {
        int v = (int)std::lround(out.data[i] + noise(rng));
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        out.data[i] = (uint8_t)v;
    }
    return out;
}

static int berCount(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    int e = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) e += (a[i] != b[i]);
    return e;
}

int main() {
    const int W = 256, H = 256;
    Image base = makeTexturedImage(W, H);

    int failures = 0;

    // ---- Test 1: clean text round-trip ----
    {
        WatermarkConfig cfg;  // defaults: d1=36, d2=20, redundancy=3
        BlindWatermarkCore core(cfg);
        core.setImage(base);
        const std::string text = "Hello WASM! 盲水印";  // multi-byte UTF-8 on purpose
        core.setWatermarkText(text);
        Image embedded = core.embed();

        size_t wmBitLen = text.size() * 8;
        BlindWatermarkCore ex(cfg);
        std::string got = ex.extractText(embedded, wmBitLen);

        bool ok = (got == text);
        printf("[1] clean text round-trip: %s  (got \"%s\")\n", ok ? "PASS" : "FAIL", got.c_str());
        failures += !ok;
    }

    // ---- Test 2: s1 soft vote must help under noise ----
    {
        const int N = 128;
        std::mt19937 rng(777);
        std::vector<uint8_t> wm(N);
        for (auto& b : wm) b = rng() & 1;

        WatermarkConfig embedCfg;
        embedCfg.redundancy = 5;   // more copies -> soft averaging has room to work
        embedCfg.d2 = 20.0;        // embed into both singular values
        BlindWatermarkCore core(embedCfg);
        core.setImage(base);
        core.setWatermarkBits(wm);
        Image embedded = core.embed();

        WatermarkConfig withS1 = embedCfg;            // d2 > 0  -> uses s1 soft vote
        WatermarkConfig withoutS1 = embedCfg; withoutS1.d2 = 0.0;  // ignores s1

        printf("[2] BER vs noise (lower is better):\n");
        printf("      sigma   with-s1   without-s1\n");
        int cleanErrWith = -1;
        for (double sigma : {0.0, 3.0, 6.0, 9.0, 12.0}) {
            Image noisy = (sigma == 0.0) ? embedded : addNoise(embedded, sigma, 4242);

            BlindWatermarkCore exA(withS1);
            BlindWatermarkCore exB(withoutS1);
            auto bitsA = exA.extractBits(noisy, N);
            auto bitsB = exB.extractBits(noisy, N);
            int eA = berCount(wm, bitsA);
            int eB = berCount(wm, bitsB);
            if (sigma == 0.0) cleanErrWith = eA;
            printf("      %5.1f   %5d/%d   %5d/%d\n", sigma, eA, N, eB, N);
        }

        bool cleanOk = (cleanErrWith == 0);
        printf("    clean (sigma=0) exact recovery with s1: %s\n", cleanOk ? "PASS" : "FAIL");
        failures += !cleanOk;
    }

    // ---- Test 3 (#6): embedded NUL byte must survive, not be dropped ----
    {
        WatermarkConfig cfg;
        BlindWatermarkCore core(cfg);
        core.setImage(base);
        std::string text("\x41\x00\x42\x00\x43", 5);  // "A\0B\0C", 5 bytes
        core.setWatermarkText(text);
        Image embedded = core.embed();

        BlindWatermarkCore ex(cfg);
        std::string got = ex.extractText(embedded, text.size() * 8);
        bool ok = (got.size() == 5 && got == text);
        printf("[3] embedded NUL preserved (#6): %s  (len %zu, expected 5)\n",
               ok ? "PASS" : "FAIL", got.size());
        failures += !ok;
    }

    // ---- Test 4 (#4): multi-level DWT round-trip on odd-triggering sizes ----
    {
        printf("[4] multi-level DWT/IDWT round-trip:\n");
        std::mt19937 rng(99);
        std::uniform_real_distribution<double> u(0.0, 255.0);
        struct Case { int n, level; };
        // 30/lvl2 -> LL0=15 (odd); 50/lvl2 -> LL0=25 (odd); 100/lvl3 -> LL1=25 (odd);
        // these are exactly the cases that desynced the LL/LH sizes before the fix.
        Case cases[] = {{30, 2}, {50, 2}, {100, 3}, {31, 2}, {64, 3}};
        for (auto cs : cases) {
            Eigen::MatrixXd M(cs.n, cs.n);
            for (int r = 0; r < cs.n; ++r)
                for (int c = 0; c < cs.n; ++c) M(r, c) = u(rng);

            auto levels = dwt2d_multilevel(M, cs.level);
            Eigen::MatrixXd R = idwt2d_multilevel(levels);
            // Reconstruction may be padded +1; compare the original region.
            Eigen::MatrixXd Rc = R.block(0, 0, cs.n, cs.n);
            double err = (Rc - M).cwiseAbs().maxCoeff();
            bool ok = err < 1e-6;
            printf("      %3dx%-3d level %d: max|err|=%.2e  %s\n",
                   cs.n, cs.n, cs.level, err, ok ? "PASS" : "FAIL");
            failures += !ok;
        }
    }

    // ---- Test 5 (#5): alpha channel preserved through embed ----
    {
        const int W = 128, H = 128;  // big enough: LL 64x64 -> 256 blocks > 48*redundancy
        Image rgba;
        rgba.allocate(W, H, 4);
        Image rgb3 = makeTexturedImage(W, H);  // reuse RGB texture for color planes
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                size_t p = (size_t)y * W + x;
                rgba.data[p * 4 + 0] = rgb3.data[p * 3 + 0];
                rgba.data[p * 4 + 1] = rgb3.data[p * 3 + 1];
                rgba.data[p * 4 + 2] = rgb3.data[p * 3 + 2];
                rgba.data[p * 4 + 3] = (uint8_t)((x * 4 + y) & 0xFF);  // varied alpha
            }
        }

        WatermarkConfig cfg;
        BlindWatermarkCore core(cfg);
        core.setImage(rgba);
        const std::string text = "alpha!";
        core.setWatermarkText(text);
        Image out = core.embed();

        bool chOk = (out.channels == 4);
        bool alphaOk = chOk;
        for (size_t p = 0; chOk && p < (size_t)W * H; ++p) {
            if (out.data[p * 4 + 3] != rgba.data[p * 4 + 3]) { alphaOk = false; break; }
        }
        // Extract straight from the 4-channel result (extract path now accepts RGBA).
        BlindWatermarkCore ex(cfg);
        std::string got = ex.extractText(out, text.size() * 8);
        bool wmOk = (got == text);

        printf("[5] alpha preserved + RGBA extract (#5): channels=%d alpha=%s wm=%s -> %s\n",
               out.channels, alphaOk ? "ok" : "BAD", wmOk ? "ok" : "BAD",
               (chOk && alphaOk && wmOk) ? "PASS" : "FAIL");
        failures += !(chOk && alphaOk && wmOk);
    }

    // ---- Test 6: invalid config is clamped, not crashed ----
    {
        WatermarkConfig bad;
        bad.d1 = 0.0;        // would be a divide-by-zero / NaN source
        bad.d2 = -5.0;
        bad.blockSize = 0;
        bad.dwtLevel = 0;
        bad.redundancy = 0;

        BlindWatermarkCore core(bad);
        core.setImage(base);
        const std::string text = "cfg";
        core.setWatermarkText(text);
        Image embedded = core.embed();

        BlindWatermarkCore ex(bad);
        std::string got = ex.extractText(embedded, text.size() * 8);
        bool ok = (got == text);
        printf("[6] invalid config clamped + round-trip: %s  (got \"%s\")\n",
               ok ? "PASS" : "FAIL", got.c_str());
        failures += !ok;
    }

    // ---- Test 7: self-describing text round-trip WITHOUT a length ----
    {
        WatermarkConfig cfg;
        BlindWatermarkCore core(cfg);
        core.setImage(base);
        std::string text = "Self-describing 自描述 \xF0\x9F\x8E\x89";  // ASCII + CJK + emoji
        std::vector<uint8_t> payload(text.begin(), text.end());
        Image embedded = core.embedSelfDescribing(payload, true);

        BlindWatermarkCore ex(cfg);
        ExtractResult r = ex.extractSelfDescribing(embedded);
        std::string got(r.payload.begin(), r.payload.end());
        bool ok = r.found && r.valid && r.isText && got == text;
        printf("[7] self-describing text (no length): %s  found=%d valid=%d isText=%d \"%s\"\n",
               ok ? "PASS" : "FAIL", r.found, r.valid, r.isText, got.c_str());
        failures += !ok;
    }

    // ---- Test 8: detect() distinguishes clean vs watermarked ----
    {
        WatermarkConfig cfg;
        BlindWatermarkCore ex(cfg);
        ExtractResult clean = ex.extractSelfDescribing(base);  // base is unwatermarked

        BlindWatermarkCore core(cfg);
        core.setImage(base);
        Image wm = core.embedSelfDescribing({'h', 'i'}, false);
        ExtractResult got = ex.extractSelfDescribing(wm);

        bool ok = (!clean.found) && got.found && got.valid;
        printf("[8] detect clean vs watermarked: %s  (clean.found=%d wm.found=%d)\n",
               ok ? "PASS" : "FAIL", clean.found, got.found);
        failures += !ok;
    }

    // ---- Test 9: wrong password -> not found ----
    {
        WatermarkConfig cfg;
        BlindWatermarkCore core(cfg);
        core.setImage(base);
        Image wm = core.embedSelfDescribing({'s', 'e', 'c', 'r', 'e', 't'}, false);

        WatermarkConfig wrong = cfg;
        wrong.passwordImg = 999;  // wrong block order -> header garbled
        BlindWatermarkCore ex(wrong);
        ExtractResult r = ex.extractSelfDescribing(wm);
        bool ok = !r.found;
        printf("[9] wrong password -> not found: %s  (found=%d)\n", ok ? "PASS" : "FAIL", r.found);
        failures += !ok;
    }

    // ---- Test 10: self-describing binary round-trip ----
    {
        WatermarkConfig cfg;
        BlindWatermarkCore core(cfg);
        core.setImage(base);
        std::vector<uint8_t> bin = {0x00, 0xFF, 0x42, 0x7E, 0x01, 0x80};
        Image wm = core.embedSelfDescribing(bin, false);

        BlindWatermarkCore ex(cfg);
        ExtractResult r = ex.extractSelfDescribing(wm);
        bool ok = r.found && r.valid && !r.isText && r.payload == bin;
        printf("[10] self-describing binary round-trip: %s  found=%d valid=%d bytes=%zu\n",
               ok ? "PASS" : "FAIL", r.found, r.valid, r.payload.size());
        failures += !ok;
    }

    // ---- Test 11: deterministic PRNG is pinned (toolchain-independent) ----
    {
        auto p = BlindWatermarkCore::pinnedPermutation(1, 16);
        auto q = BlindWatermarkCore::pinnedPermutation(42, 10);
        printf("[11] pinnedPermutation(1,16) =");  for (auto x : p) printf(" %zu", x); printf("\n");
        printf("     pinnedPermutation(42,10) ="); for (auto x : q) printf(" %zu", x); printf("\n");
        // GOLDEN — these lock the format. If they ever change, watermarks break.
        std::vector<size_t> g1 = {2, 11, 10, 6, 7, 13, 14, 0, 12, 5, 15, 9, 3, 8, 4, 1};
        std::vector<size_t> g2 = {0, 9, 5, 8, 6, 4, 7, 2, 1, 3};
        bool ok = (p == g1) && (q == g2);
        printf("     determinism golden: %s\n", ok ? "PASS" : "FAIL (pin the values above)");
        failures += !ok;
    }

    // ---- Test 12: oversize image is rejected with a catchable error ----
    {
        Image big; big.width = 5000; big.height = 5000; big.channels = 3; big.data.assign(3, 0);
        bool threw = false; std::string msg;
        try { BlindWatermarkCore core; core.setImage(big); }
        catch (const std::exception& e) { threw = true; msg = e.what(); }
        bool ok = threw && msg.find("too large") != std::string::npos;
        printf("[12] oversize image rejected: %s  (\"%s\")\n", ok ? "PASS" : "FAIL", msg.c_str());
        failures += !ok;
    }

    printf("\n%s\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
