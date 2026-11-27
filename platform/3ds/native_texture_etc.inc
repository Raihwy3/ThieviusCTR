// Native texture preparation reuses this project's CMV2 ETC1 encoder.
// Generated texture caches contain user game data and stay on the user's SD.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>
namespace cane_texture_etc {


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


static uint64_t hashBytes(const uint8_t* data,size_t size,uint64_t hash=14695981039346656037ull) {
    while(size--) {hash^=*data++;hash*=1099511628211ull;}return hash;
}
static bool prepare(const std::vector<uint8_t>& rgb,unsigned width,unsigned height,std::vector<uint8_t>& result,bool allowEncode=true) {
    result.resize(size_t(width)*height/2);
    uint64_t key=hashBytes(rgb.data(),rgb.size());key^=uint64_t(width)<<32|height;
    char path[160],temp[168];
    std::snprintf(path,sizeof path,"sdmc:/3ds/ThieviusCTR/Cache/Textures/etc1-v1-%016llx.bin",(unsigned long long)key);
    bool loaded=false;
    if(FILE* file=std::fopen(path,"rb")) {
        uint64_t digest=0;
        loaded=std::fread(&digest,1,sizeof digest,file)==sizeof digest &&
            std::fread(result.data(),1,result.size(),file)==result.size() && std::fgetc(file)==EOF &&
            hashBytes(result.data(),result.size())==digest;
        std::fclose(file);
    }
    if(loaded)return true;
    if(!allowEncode) {result.clear();return false;}
    encodeFrame(rgb.data(),result.data(),int(width),int(height));
    mkdir("sdmc:/3ds/ThieviusCTR/Cache",0777);mkdir("sdmc:/3ds/ThieviusCTR/Cache/Textures",0777);
    std::snprintf(temp,sizeof temp,"%s.tmp",path);
    if(FILE* file=std::fopen(temp,"wb")) {
        const uint64_t digest=hashBytes(result.data(),result.size());
        bool ok=std::fwrite(&digest,1,sizeof digest,file)==sizeof digest &&
            std::fwrite(result.data(),1,result.size(),file)==result.size();
        ok=std::fclose(file)==0 && ok;
        if(ok) {std::remove(path);if(std::rename(temp,path)!=0)std::remove(temp);}
        else std::remove(temp);
    }
    return true;
}
struct PreparedTexture {unsigned width=0,height=0;std::vector<uint8_t> data;};
using PreparedImage=std::array<PreparedTexture,2>;
static bool loadBundle(const char* path,uint64_t fingerprint,std::vector<PreparedImage>& images) {
    FILE* file=std::fopen(path,"rb");if(!file)return false;
    // Bundle records are small; coalesce their headers and pixel payloads
    // instead of issuing a separate SD read for each cached texture.
    std::vector<char> readBuffer(128*1024);
    std::setvbuf(file,readBuffer.data(),_IOFBF,readBuffer.size());
    uint64_t header[4]{};
    bool ok=std::fread(header,1,sizeof header,file)==sizeof header && header[0]==0x3154455443545255ull &&
        header[1]==fingerprint && header[2]==images.size() && header[3]<=images.size()*2;
    uint64_t digest=14695981039346656037ull;
    for(uint64_t i=0;ok && i<header[3];++i) {
        uint32_t record[4]{};ok=std::fread(record,1,sizeof record,file)==sizeof record;
        const unsigned w=record[2]>>16,h=record[2]&65535;
        ok=ok && record[0]<images.size() && record[1]<2 && w>=8 && h>=8 && w<=128 && h<=128 &&
            !(w&(w-1)) && !(h&(h-1)) && record[3]==w*h/2;
        if(!ok)break;
        auto& texture=images[record[0]][record[1]];
        if(!texture.data.empty()) {ok=false;break;}
        texture.width=w;texture.height=h;texture.data.resize(record[3]);
        ok=std::fread(texture.data.data(),1,record[3],file)==record[3];
        digest=hashBytes(reinterpret_cast<const uint8_t*>(record),sizeof record,digest);
        digest=hashBytes(texture.data.data(),texture.data.size(),digest);
    }
    uint64_t expected=0;
    ok=ok && std::fread(&expected,1,sizeof expected,file)==sizeof expected && expected==digest && std::fgetc(file)==EOF;
    std::fclose(file);
    if(!ok)for(auto& image:images)image={};
    return ok;
}
static void saveBundle(const char* path,uint64_t fingerprint,const std::vector<PreparedImage>& images) {
    char temp[192];std::snprintf(temp,sizeof temp,"%s.tmp",path);
    FILE* file=std::fopen(temp,"wb");if(!file)return;
    uint64_t count=0;for(const auto& image:images)for(const auto& texture:image)if(!texture.data.empty())++count;
    const uint64_t header[4]={0x3154455443545255ull,fingerprint,uint64_t(images.size()),count};
    bool ok=std::fwrite(header,1,sizeof header,file)==sizeof header;
    uint64_t digest=14695981039346656037ull;
    for(size_t image=0;ok && image<images.size();++image)for(unsigned variant=0;variant<2;++variant) {
        const auto& texture=images[image][variant];if(texture.data.empty())continue;
        const uint32_t record[4]={uint32_t(image),variant,(texture.width<<16)|texture.height,uint32_t(texture.data.size())};
        ok=ok && std::fwrite(record,1,sizeof record,file)==sizeof record && std::fwrite(texture.data.data(),1,texture.data.size(),file)==texture.data.size();
        digest=hashBytes(reinterpret_cast<const uint8_t*>(record),sizeof record,digest);
        digest=hashBytes(texture.data.data(),texture.data.size(),digest);
    }
    ok=ok && std::fwrite(&digest,1,sizeof digest,file)==sizeof digest;
    ok=std::fclose(file)==0 && ok;
    if(ok) {std::remove(path);if(std::rename(temp,path)!=0)std::remove(temp);}
    else std::remove(temp);
}
} // namespace cane_texture_etc
