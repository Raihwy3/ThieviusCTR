// Fast multi-threaded RGB24 -> PICA200 GPU_ETC1 CMV2 cutscene encoder.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

constexpr int kTable[8][4] = {
    {  2,   8,  -2,   -8 },
    {  5,  17,  -5,  -17 },
    {  9,  29,  -9,  -29 },
    { 13,  42, -13,  -42 },
    { 18,  60, -18,  -60 },
    { 24,  80, -24,  -80 },
    { 33, 106, -33, -106 },
    { 47, 183, -47, -183 },
};

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

struct HalfResult {
    uint64_t err = UINT64_MAX;
    int t = 0;
    uint8_t sel[8]{};
};

HalfResult bestHalf(const int pxRGB[8][3], int baseR, int baseG, int baseB) {
    HalfResult best;
    for (int t = 0; t < 8; ++t) {
        uint64_t sumErr = 0;
        uint8_t sels[8]{};
        for (int i = 0; i < 8; ++i) {
            uint32_t bestPxErr = UINT32_MAX;
            uint8_t bestS = 0;
            const int pr = pxRGB[i][0], pg = pxRGB[i][1], pb = pxRGB[i][2];
            for (int s = 0; s < 4; ++s) {
                const int d = kTable[t][s];
                const int dr = clamp255(baseR + d) - pr;
                const int dg = clamp255(baseG + d) - pg;
                const int db = clamp255(baseB + d) - pb;
                const uint32_t e = uint32_t(dr * dr + dg * dg + db * db);
                if (e < bestPxErr) {
                    bestPxErr = e;
                    bestS = uint8_t(s);
                }
            }
            sumErr += bestPxErr;
            sels[i] = bestS;
            if (sumErr >= best.err) break;
        }
        if (sumErr < best.err) {
            best.err = sumErr;
            best.t = t;
            std::memcpy(best.sel, sels, 8);
        }
    }
    return best;
}

uint64_t encodeBlock4x4(const uint8_t* rgb, int strideBytes) {
    uint64_t bestTotal = UINT64_MAX;
    uint32_t bestLo = 0, bestHi = 0;

    for (int flip = 0; flip < 2; ++flip) {
        int sub[2][8][3];
        int bits[2][8];
        int sum[2][3]{};

        int cnt[2]{};
        for (int py = 0; py < 4; ++py) {
            const uint8_t* row = rgb + py * strideBytes;
            for (int px = 0; px < 4; ++px) {
                const int h = (flip == 0) ? (px >> 1) : (py >> 1);
                const int k = cnt[h]++;
                const int r = row[px * 3 + 0];
                const int g = row[px * 3 + 1];
                const int b = row[px * 3 + 2];
                sub[h][k][0] = r;
                sub[h][k][1] = g;
                sub[h][k][2] = b;
                bits[h][k] = px * 4 + py;
                sum[h][0] += r;
                sum[h][1] += g;
                sum[h][2] += b;
            }
        }

        // Individual mode (4-4-4 per half)
        {
            int c4[2][3]{};
            int exp[2][3]{};
            for (int h = 0; h < 2; ++h) {
                for (int ch = 0; ch < 3; ++ch) {
                    c4[h][ch] = std::clamp(((sum[h][ch] / 8) * 15 + 127) / 255, 0, 15);
                    exp[h][ch] = c4[h][ch] * 17;
                }
            }
            const HalfResult r0 = bestHalf(sub[0], exp[0][0], exp[0][1], exp[0][2]);
            const HalfResult r1 = bestHalf(sub[1], exp[1][0], exp[1][1], exp[1][2]);
            const uint64_t err = r0.err + r1.err;
            if (err < bestTotal) {
                bestTotal = err;
                uint32_t lsb = 0, msb = 0;
                for (int i = 0; i < 8; ++i) {
                    lsb |= uint32_t(r0.sel[i] & 1u) << bits[0][i];
                    msb |= uint32_t((r0.sel[i] >> 1) & 1u) << bits[0][i];
                    lsb |= uint32_t(r1.sel[i] & 1u) << bits[1][i];
                    msb |= uint32_t((r1.sel[i] >> 1) & 1u) << bits[1][i];
                }
                bestLo = (msb << 16) | lsb;
                bestHi = (uint32_t(c4[0][0]) << 28) | (uint32_t(c4[1][0]) << 24) |
                         (uint32_t(c4[0][1]) << 20) | (uint32_t(c4[1][1]) << 16) |
                         (uint32_t(c4[0][2]) << 12) | (uint32_t(c4[1][2]) << 8) |
                         (uint32_t(r0.t) << 5) | (uint32_t(r1.t) << 2) |
                         uint32_t(flip);
            }
        }

        // Differential mode (5-5-5 + 3-3-3 delta)
        {
            int c5_0[3]{}, dc[3]{};
            bool fits = true;
            for (int ch = 0; ch < 3; ++ch) {
                c5_0[ch] = std::clamp(((sum[0][ch] / 8) * 31 + 127) / 255, 0, 31);
                const int c5_1 = std::clamp(((sum[1][ch] / 8) * 31 + 127) / 255, 0, 31);
                dc[ch] = std::clamp(c5_1 - c5_0[ch], -4, 3);
                if (std::abs((c5_1 - c5_0[ch]) - dc[ch]) > 1) fits = false;
            }
            if (fits) {
                int exp[2][3]{};
                for (int ch = 0; ch < 3; ++ch) {
                    const int v0 = c5_0[ch];
                    const int v1 = std::clamp(v0 + dc[ch], 0, 31);
                    exp[0][ch] = (v0 << 3) | (v0 >> 2);
                    exp[1][ch] = (v1 << 3) | (v1 >> 2);
                }
                const HalfResult r0 = bestHalf(sub[0], exp[0][0], exp[0][1], exp[0][2]);
                const HalfResult r1 = bestHalf(sub[1], exp[1][0], exp[1][1], exp[1][2]);
                const uint64_t err = r0.err + r1.err;
                if (err < bestTotal) {
                    bestTotal = err;
                    uint32_t lsb = 0, msb = 0;
                    for (int i = 0; i < 8; ++i) {
                        lsb |= uint32_t(r0.sel[i] & 1u) << bits[0][i];
                        msb |= uint32_t((r0.sel[i] >> 1) & 1u) << bits[0][i];
                        lsb |= uint32_t(r1.sel[i] & 1u) << bits[1][i];
                        msb |= uint32_t((r1.sel[i] >> 1) & 1u) << bits[1][i];
                    }
                    bestLo = (msb << 16) | lsb;
                    bestHi = (uint32_t(c5_0[0]) << 27) | (uint32_t(dc[0] & 7) << 24) |
                             (uint32_t(c5_0[1]) << 19) | (uint32_t(dc[1] & 7) << 16) |
                             (uint32_t(c5_0[2]) << 11) | (uint32_t(dc[2] & 7) << 8) |
                             (uint32_t(r0.t) << 5) | (uint32_t(r1.t) << 2) |
                             2u | uint32_t(flip);
                }
            }
        }
    }

    return uint64_t(bestLo) | (uint64_t(bestHi) << 32);
}

void encodeFrame(const uint8_t* rgb, uint8_t* out, int width, int height) {
    const int tilesX = width / 8;
    const int tilesY = height / 8;
    const int stride = width * 3;
    uint8_t* dst = out;
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            const int x0 = tx * 8, y0 = ty * 8;
            static constexpr int kBx[4] = { 0, 4, 0, 4 };
            static constexpr int kBy[4] = { 0, 0, 4, 4 };
            for (int b = 0; b < 4; ++b) {
                const uint64_t blk = encodeBlock4x4(
                    rgb + (y0 + kBy[b]) * stride + (x0 + kBx[b]) * 3, stride);
                std::memcpy(dst, &blk, 8);
                dst += 8;
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <output.cmov> [width=320] [height=240] [fps=15]\n", argv[0]);
        return 1;
    }
    const char* outPath = argv[1];
    const int width = argc > 2 ? std::atoi(argv[2]) : 320;
    const int height = argc > 3 ? std::atoi(argv[3]) : 240;
    const int fps = argc > 4 ? std::atoi(argv[4]) : 15;
    if (width <= 0 || height <= 0 || (width % 8) != 0 || (height % 8) != 0 || fps <= 0) {
        std::fprintf(stderr, "Invalid dimensions/fps\n");
        return 1;
    }

    FILE* out = std::fopen(outPath, "wb");
    if (!out) {
        std::perror("fopen");
        return 1;
    }
    uint32_t header[6] = { 0x32564d43u, uint32_t(width), uint32_t(height), uint32_t(fps), 0u, uint32_t((width / 8) * (height / 8) * 32) };
    if (std::fwrite(header, 4, 6, out) != 6) return 1;

    const size_t inFrameBytes = size_t(width) * height * 3;
    const size_t outFrameBytes = header[5];
    const size_t batchCap = 64;
    std::vector<uint8_t> inBuf(batchCap * inFrameBytes);
    std::vector<uint8_t> outBuf(batchCap * outFrameBytes);

    const unsigned threads = std::max(1u, std::min(12u, std::thread::hardware_concurrency()));
    uint32_t totalFrames = 0;

    for (;;) {
        size_t batchCount = 0;
        while (batchCount < batchCap) {
            const size_t got = std::fread(inBuf.data() + batchCount * inFrameBytes, 1, inFrameBytes, stdin);
            if (got == 0) break;
            if (got != inFrameBytes) {
                std::fprintf(stderr, "Truncated RGB24 frame on stdin (%zu bytes)\n", got);
                std::fclose(out);
                return 1;
            }
            ++batchCount;
        }
        if (batchCount == 0) break;

        if (threads <= 1 || batchCount == 1) {
            for (size_t f = 0; f < batchCount; ++f) {
                encodeFrame(inBuf.data() + f * inFrameBytes, outBuf.data() + f * outFrameBytes, width, height);
            }
        } else {
            std::vector<std::thread> workers;
            const unsigned n = std::min<unsigned>(threads, unsigned(batchCount));
            workers.reserve(n);
            for (unsigned t = 0; t < n; ++t) {
                workers.emplace_back([&, t, n]() {
                    for (size_t f = t; f < batchCount; f += n) {
                        encodeFrame(inBuf.data() + f * inFrameBytes, outBuf.data() + f * outFrameBytes, width, height);
                    }
                });
            }
            for (auto& w : workers) w.join();
        }

        if (std::fwrite(outBuf.data(), outFrameBytes, batchCount, out) != batchCount) {
            std::perror("fwrite");
            std::fclose(out);
            return 1;
        }
        totalFrames += uint32_t(batchCount);
    }

    header[4] = totalFrames;
    std::fseek(out, 0, SEEK_SET);
    if (std::fwrite(header, 4, 6, out) != 6 || std::fclose(out) != 0) return 1;
    return 0;
}
