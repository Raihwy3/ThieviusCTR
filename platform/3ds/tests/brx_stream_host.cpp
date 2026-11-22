#include "BrxWalker.h"
#include "OptionWalker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef TRACK_ALLOC
namespace {
std::atomic<bool> gTracking{false};
std::size_t gLiveBytes = 0;
std::size_t gPeakBytes = 0;
std::size_t gMaxAlloc = 0;

struct AllocHeader {
    std::size_t size;
    std::size_t magic;
};
constexpr std::size_t kAllocMagic = 0x54484945u;

void* trackedAlloc(std::size_t sz) {
    const std::size_t total = sizeof(AllocHeader) + (sz ? sz : 1u);
    void* raw = std::malloc(total);
    if (!raw) throw std::bad_alloc();
    auto* hdr = static_cast<AllocHeader*>(raw);
    hdr->size = gTracking.load(std::memory_order_relaxed) ? sz : 0u;
    hdr->magic = gTracking.load(std::memory_order_relaxed) ? kAllocMagic : 0u;
    if (hdr->magic == kAllocMagic) {
        gLiveBytes += sz;
        if (gLiveBytes > gPeakBytes) gPeakBytes = gLiveBytes;
        if (sz > gMaxAlloc) gMaxAlloc = sz;
    }
    return hdr + 1;
}

void trackedFree(void* ptr) noexcept {
    if (!ptr) return;
    auto* hdr = static_cast<AllocHeader*>(ptr) - 1;
    if (hdr->magic == kAllocMagic) {
        gLiveBytes -= hdr->size;
        hdr->magic = 0;
    }
    std::free(hdr);
}

void startTracking() {
    gLiveBytes = 0;
    gPeakBytes = 0;
    gMaxAlloc = 0;
    gTracking.store(true, std::memory_order_relaxed);
}

void stopTracking() {
    gTracking.store(false, std::memory_order_relaxed);
}
} // namespace

void* operator new(std::size_t sz) { return trackedAlloc(sz); }
void* operator new[](std::size_t sz) { return trackedAlloc(sz); }
void operator delete(void* ptr) noexcept { trackedFree(ptr); }
void operator delete[](void* ptr) noexcept { trackedFree(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { trackedFree(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { trackedFree(ptr); }
#endif

namespace {

void pushU8(std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back(v); }
void pushS8(std::vector<std::uint8_t>& b, std::int8_t v) { b.push_back(static_cast<std::uint8_t>(v)); }
void pushU16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xffu));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void pushS16(std::vector<std::uint8_t>& b, std::int16_t v) { pushU16(b, static_cast<std::uint16_t>(v)); }
void pushU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int s = 0; s < 32; s += 8)
        b.push_back(static_cast<std::uint8_t>((v >> s) & 0xffu));
}
void pushS32(std::vector<std::uint8_t>& b, std::int32_t v) { pushU32(b, static_cast<std::uint32_t>(v)); }
void pushF32(std::vector<std::uint8_t>& b, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    pushU32(b, bits);
}
void pushVec2(std::vector<std::uint8_t>& b, float x, float y) { pushF32(b, x); pushF32(b, y); }
void pushVec3(std::vector<std::uint8_t>& b, float x, float y, float z) { pushF32(b, x); pushF32(b, y); pushF32(b, z); }
void pushMat3(std::vector<std::uint8_t>& b) {
    for (int i = 0; i < 9; ++i) pushF32(b, (i % 4 == 0) ? 1.0f : 0.0f);
}
void pushNarrowStr(std::vector<std::uint8_t>& b, const std::string& s) {
    pushU16(b, static_cast<std::uint16_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
#ifndef TRACK_ALLOC
void pushUtf16Str(std::vector<std::uint8_t>& b, const std::string& ascii) {
    pushU16(b, static_cast<std::uint16_t>(ascii.size()));
    for (unsigned char c : ascii) {
        pushU16(b, c);
    }
}
#endif
void pushEncDesc(std::vector<std::uint8_t>& b, std::uint32_t sec, std::uint32_t sz, std::uint32_t ident) {
    std::uint32_t w[8]{};
    w[5] = 0x13572468u;
    w[7] = 0x24681357u;
    w[6] = 0xabcdef01u;
    w[0] = sec ^ w[5];
    w[1] = sz ^ w[7];
    w[2] = ident ^ w[6];
    for (std::uint32_t word : w) pushU32(b, word);
}

void pushAloBase(std::vector<std::uint8_t>& b) {
    pushMat3(b);
    pushVec3(b, 0.0f, 0.0f, 0.0f);
    pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushU32(b, 0);
    pushF32(b, 1.0f); pushF32(b, 1.0f); pushF32(b, 1.0f); pushF32(b, 1.0f);
}

#ifndef TRACK_ALLOC
void pushEmptyGlobsetAndAloxTail(std::vector<std::uint8_t>& b) {
    // LoadGlobset empty: vertexKind, normalKind, boneCount=0, poseCount=0, globCount=0
    pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushU16(b, 0);
    // LoadAlox: flags=0
    pushU32(b, 0);
    // poseCopies=0
    pushU8(b, 0);
}
#endif

void pushTestSubglob(std::vector<std::uint8_t>& b, std::uint8_t vc, std::uint8_t ic, std::uint8_t poseCount) {
    pushVec3(b, 0.0f, 0.0f, 0.0f);
    pushF32(b, 10.0f);
    const std::uint8_t nc = vc;
    const std::uint8_t cc = 4;
    const std::uint8_t tc = 4;
    pushU8(b, vc);
    pushU8(b, nc);
    pushU8(b, cc);
    pushU8(b, tc);
    pushU8(b, ic);
    while ((b.size() & 3u) != 0u) pushU8(b, 0xaa);

    for (std::uint8_t i = 0; i < vc; ++i)
        pushVec3(b, float(i), float(i) * 0.5f, 1.0f);
    for (std::uint8_t i = 0; i < nc; ++i)
        pushVec3(b, 0.0f, 0.0f, 1.0f);
    for (std::uint8_t i = 0; i < cc; ++i) {
        pushU8(b, 200); pushU8(b, 150); pushU8(b, 100); pushU8(b, 255);
    }
    for (std::uint8_t i = 0; i < tc; ++i)
        pushVec2(b, 0.25f * float(i), 0.5f);

    for (std::uint8_t i = 0; i < ic; ++i) {
        pushU8(b, static_cast<std::uint8_t>(i % vc));
        pushU8(b, static_cast<std::uint8_t>(i % nc));
        pushU8(b, static_cast<std::uint8_t>(i % tc));
        const std::uint8_t colorIdx = static_cast<std::uint8_t>(i % cc);
        const std::uint8_t skipBit = (i < 2u) ? 0x80u : 0x00u;
        pushU8(b, static_cast<std::uint8_t>(colorIdx | skipBit));
    }

    pushU16(b, 0); // shaderId = 0
    pushU8(b, 128); // selfIllumByte
    const std::uint8_t bi = 2;
    pushU8(b, bi);
    pushU8(b, 1); pushU8(b, 2);
    for (std::size_t i = 0; i < std::size_t(vc) * bi; ++i)
        pushF32(b, 0.5f);

    if (poseCount) {
        const std::uint16_t pp = 4;
        const std::uint16_t pn = 4;
        pushU16(b, pp);
        for (std::uint16_t i = 0; i < pp; ++i)
            pushVec3(b, 1.0f + float(i), 2.0f, 3.0f);
        pushU16(b, pn);
        for (std::uint16_t i = 0; i < pn; ++i)
            pushVec3(b, 0.0f, 1.0f, 0.0f);
        for (std::uint8_t p = 0; p < poseCount; ++p) {
            for (std::uint8_t v = 0; v < ic; ++v)
                pushU16(b, static_cast<std::uint16_t>(v % pp));
            for (std::uint8_t v = 0; v < ic; ++v)
                pushU16(b, static_cast<std::uint16_t>(v % pn));
        }
    }
}

#ifndef TRACK_ALLOC
void pushTestSubcel(std::vector<std::uint8_t>& b, std::uint8_t poseCount) {
    const std::uint8_t pc = 4;
    const std::uint8_t ec = 2;
    const std::uint8_t bi = 2;
    pushU8(b, pc);
    for (std::uint8_t i = 0; i < pc; ++i)
        pushVec3(b, float(i), 0.0f, 1.0f);
    pushU8(b, ec);
    for (std::uint8_t i = 0; i < ec; ++i) {
        pushU8(b, 0); pushU8(b, 1); pushU8(b, 2); pushU8(b, 3);
    }
    pushU8(b, bi);
    pushU8(b, 0); pushU8(b, 1);
    for (std::size_t i = 0; i < std::size_t(bi) * pc; ++i)
        pushF32(b, 0.5f);
    if (poseCount) {
        const std::uint16_t pp = 2;
        pushU16(b, pp);
        pushVec3(b, 2.0f, 0.0f, 0.0f);
        pushVec3(b, 3.0f, 0.0f, 0.0f);
        for (std::uint8_t p = 0; p < poseCount; ++p)
            for (std::uint8_t i = 0; i < pc; ++i)
                pushU16(b, static_cast<std::uint16_t>(i % pp));
    }
}

std::vector<std::uint8_t> buildFeatureFixture(bool pal) {
    std::vector<std::uint8_t> b;
    // Header (8 bytes)
    pushS32(b, 0);
    pushS32(b, 1);
    // debugInfo = 0
    pushU8(b, 0);
    // sound bank index 0 = Common.bnk
    pushS16(b, 0);
    // name table count = 0
    pushU32(b, 0);
    if (pal) {
        // PAL localization table: 2 UTF-16LE strings
        pushU16(b, 2);
        pushUtf16Str(b, "Subtitle One");
        pushUtf16Str(b, "Subtitle Two");
    }
    // LoadSwSpliceFromBrx: spliceCount=1, symbolCount=0, extraOptionCount=0
    pushU32(b, 1); pushU32(b, 0); pushU32(b, 0);
    pushU8(b, 0x14); // tag 20 S32
    pushS32(b, 12345);
    // LoadOptionsFromBrx: option 95 (packed color), 526 (lighting), -1
    pushS16(b, 95); pushU32(b, 0x80402010u);
    pushS16(b, 526); pushF32(b, 127.5f);
    pushS16(b, -1);
    // AudioDescriptor, world name string, MusicDescriptor
    pushEncDesc(b, 0x100u, 0x200u, 0x0022u);
    if (pal) pushUtf16Str(b, "Paris");
    else pushNarrowStr(b, "Paris");
    pushEncDesc(b, 0x300u, 0x400u, 0x55u);
    if (pal) {
        for (int i = 0; i < 14 * 32; ++i) pushU8(b, 0);
    }

    // Build texture payload data to place at the end after alignment
    std::vector<std::uint8_t> texPayload;
    // CLUT 0: 16 colors (opaque alpha 0x80)
    const std::uint32_t clut0Off = static_cast<std::uint32_t>(texPayload.size());
    for (int i = 0; i < 16; ++i) {
        texPayload.push_back(static_cast<std::uint8_t>(i * 16));
        texPayload.push_back(static_cast<std::uint8_t>(255 - i * 8));
        texPayload.push_back(static_cast<std::uint8_t>(i * 10));
        texPayload.push_back(0x80u);
    }
    // CLUT 1: 16 colors (transparent alpha 0x40 on entry 1)
    const std::uint32_t clut1Off = static_cast<std::uint32_t>(texPayload.size());
    for (int i = 0; i < 16; ++i) {
        texPayload.push_back(static_cast<std::uint8_t>(255 - i * 12));
        texPayload.push_back(static_cast<std::uint8_t>(i * 15));
        texPayload.push_back(128u);
        texPayload.push_back(i == 1 ? 0x40u : 0x80u);
    }
    // CLUT 2: 256 colors (CSM layout)
    const std::uint32_t clut2Off = static_cast<std::uint32_t>(texPayload.size());
    for (int i = 0; i < 256; ++i) {
        texPayload.push_back(static_cast<std::uint8_t>(i));
        texPayload.push_back(static_cast<std::uint8_t>(i ^ 0x55));
        texPayload.push_back(static_cast<std::uint8_t>(255 - i));
        texPayload.push_back(0x80u);
    }
    // Bitmap 0: 8x8 4-bit (32 bytes)
    const std::uint32_t bm0Off = static_cast<std::uint32_t>(texPayload.size());
    for (int i = 0; i < 32; ++i)
        texPayload.push_back(static_cast<std::uint8_t>(((i * 3) & 0xf) | (((i * 5 + 1) & 0xf) << 4)));
    // Bitmap 1: 8x8 4-bit distinct
    const std::uint32_t bm1Off = static_cast<std::uint32_t>(texPayload.size());
    for (int i = 0; i < 32; ++i)
        texPayload.push_back(static_cast<std::uint8_t>((i & 0xf) | (((i + 2) & 0xf) << 4)));
    // Bitmap 2: 8x8 8-bit (64 bytes)
    const std::uint32_t bm2Off = static_cast<std::uint32_t>(texPayload.size());
    for (int i = 0; i < 64; ++i)
        texPayload.push_back(static_cast<std::uint8_t>((i * 7 + 13) & 0xffu));
    // Bitmap 3: identical bytes to Bitmap 0 -> tests cross-(bi,ci) content dedup!
    const std::uint32_t bm3Off = static_cast<std::uint32_t>(texPayload.size());
    texPayload.insert(texPayload.end(), texPayload.begin() + bm0Off, texPayload.begin() + bm0Off + 32);

    // LoadShadersFromBrx:
    pushU16(b, 3); // nc = 3
    pushU32(b, 1); pushU16(b, 16); pushU16(b, 1); pushU32(b, clut0Off);
    pushU32(b, 1); pushU16(b, 16); pushU16(b, 1); pushU32(b, clut1Off);
    pushU32(b, 1); pushU16(b, 256); pushU16(b, 1); pushU32(b, clut2Off);

    pushU16(b, 4); // nb = 4
    pushU16(b, 8); pushU16(b, 8); pushU32(b, 1); pushS8(b, 0); pushS8(b, 0); pushU16(b, 0); pushU32(b, 32); pushU32(b, bm0Off);
    pushU16(b, 8); pushU16(b, 8); pushU32(b, 1); pushS8(b, 0); pushS8(b, 0); pushU16(b, 0); pushU32(b, 32); pushU32(b, bm1Off);
    pushU16(b, 8); pushU16(b, 8); pushU32(b, 2); pushS8(b, 0); pushS8(b, 0); pushU16(b, 0); pushU32(b, 64); pushU32(b, bm2Off);
    pushU16(b, 8); pushU16(b, 8); pushU32(b, 1); pushS8(b, 0); pushS8(b, 0); pushU16(b, 0); pushU32(b, 32); pushU32(b, bm3Off);

    // ns = 3 shaders
    pushU16(b, 3); pushU16(b, 0);
    // Shader 0: ThreeWay (kind=0), 2 pending textures
    pushU8(b, 0); pushU8(b, 0); pushS16(b, 1);
    for (int c = 0; c < 8; ++c) pushU8(b, static_cast<std::uint8_t>(64 + c * 16));
    pushU32(b, 0); pushU16(b, 0); pushU8(b, 0); pushU8(b, 2); // nt = 2
    pushU16(b, 0); // ReadSaa none
    // Texture 0.0: 1 bitmap, 3 cluts (clut 0 BC1, clut 1 BC3, clut 0xFFFF invalid!)
    pushS16(b, 10); pushS16(b, 3); pushU8(b, 1); pushU8(b, 3);
    pushU16(b, 0); pushU16(b, 0); pushU16(b, 1); pushU16(b, 0xffff);
    // Texture 0.1: 1 bitmap, 3 cluts (repeat + new bitmap 1)
    pushS16(b, 11); pushS16(b, 0); pushU8(b, 1); pushU8(b, 3);
    pushU16(b, 1); pushU16(b, 0); pushU16(b, 1); pushU16(b, 0);

    // Shader 1: Prelit (kind=1), 1 pending texture referencing bm3 + clut0 (dedups with Shader 0.0 image 0!)
    pushU8(b, 1); pushU8(b, 0); pushS16(b, 2);
    for (int c = 0; c < 8; ++c) pushU8(b, 255);
    pushU32(b, 0); pushU16(b, 0); pushU8(b, 0); pushU8(b, 1);
    pushU16(b, 0);
    pushS16(b, 12); pushS16(b, 3); pushU8(b, 1); pushU8(b, 1);
    pushU16(b, 3); pushU16(b, 0);

    // Shader 2: Kind 2 (lossless RGBA8), 1 pending texture with 8-bit bitmap 2
    pushU8(b, 2); pushU8(b, 0); pushS16(b, 3);
    for (int c = 0; c < 8; ++c) pushU8(b, 200);
    pushU32(b, 0); pushU16(b, 0); pushU8(b, 0); pushU8(b, 1);
    pushU16(b, 0);
    pushS16(b, 13); pushS16(b, 0); pushU8(b, 1); pushU8(b, 1);
    pushU16(b, 2); pushU16(b, 2);

    // nf = 1 font (bitmap 2, clut 2)
    pushU16(b, 1);
    pushU16(b, 2); pushS16(b, 2);
    pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushF32(b, 12.0f);
    pushU32(b, 1); // ng = 1
    pushU32(b, 0); pushU32(b, 0);

    // LoadSwObjectsFromBrx: count = 4 (object 0 is CID_UNK11!)
    pushU16(b, 4);

    // Object 0: CID_UNK11 (107), contains 1 subglob + nested child CID_UNK11 + valid option 95 (all must disappear cleanly)
    pushS16(b, 107); pushS16(b, 100); pushS16(b, -1);
    pushAloBase(b);
    pushS16(b, 95); pushU32(b, 0xffffffffu); pushS16(b, -1);
    pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushU16(b, 1); // globCount=1
    pushU16(b, 0); // flags=0
    pushVec3(b, 0.0f, 0.0f, 0.0f); pushF32(b, 1.0f); pushS16(b, 0); pushU8(b, 0); pushU8(b, 0); pushU8(b, 0);
    pushU16(b, 1); // subglobCount = 1
    pushTestSubglob(b, 4, 5, 0);
    pushU16(b, 0); // subcelCount = 0
    pushU32(b, 0); // Alox
    pushU8(b, 0); // poseCopies
    pushU16(b, 1); // inner children: 1 CID_UNK11 inside CID_UNK11
    pushS16(b, 107); pushS16(b, 101); pushS16(b, -1);
    pushAloBase(b);
    pushS16(b, -1);
    pushEmptyGlobsetAndAloxTail(b);
    pushU16(b, 0);

    // Object 1: CID_VISMAP (134)
    pushS16(b, 134); pushS16(b, 200); pushS16(b, -1);
    pushU16(b, 2); // 2 vismap nodes
    pushVec3(b, 1.0f, 0.0f, 0.0f); pushF32(b, 5.0f); pushU32(b, 1); pushU32(b, 0x80000001u);
    pushVec3(b, 0.0f, 1.0f, 0.0f); pushF32(b, 6.0f); pushU32(b, 0x80000002u); pushU32(b, 0x80000003u);
    pushU16(b, 1); pushU32(b, 0x3u); // zones=1
    pushS16(b, 95); pushU32(b, 0x11223344u); pushS16(b, -1);

    // Object 2: CID_EMITTER (80) with option 366=4 (emitMesh) and emitColors
    pushS16(b, 80); pushS16(b, 300); pushS16(b, -1);
    pushAloBase(b);
    pushS16(b, 366); pushS16(b, 4); pushS16(b, -1);
    pushEmptyGlobsetAndAloxTail(b);
    pushU16(b, 0); // child objects
    pushS8(b, -1); // no curve
    // emitMesh: 2 pts, 1 tri, vec3
    pushU16(b, 2); for (int i = 0; i < 24; ++i) pushU8(b, 1);
    pushU16(b, 1); for (int i = 0; i < 10; ++i) pushU8(b, 2);
    pushVec3(b, 0.0f, 1.0f, 0.0f);
    // emitColors: 3 colors
    pushU16(b, 3);
    pushU8(b, 1); // fColorRanges
    pushU32(b, 0xff804020u);
    pushU32(b, 0x80ffffffu);
    pushU32(b, 0x00102030u);

    // Object 3: CID_ALO (2) with non-zero globset (subglob with 2 poses + subcel with 2 poses) and child list removing 1 CID_UNK11 + keeping 1 CID_LO
    pushS16(b, 2); pushS16(b, 400); pushS16(b, -1);
    pushAloBase(b);
    pushS16(b, 526); pushF32(b, 255.0f); pushS16(b, -1);
    // Globset: poseCount=2, globCount=1
    pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushU8(b, 2); pushU16(b, 1);
    pushU16(b, 0); // flags=0
    pushVec3(b, 0.0f, 0.0f, 0.0f); pushF32(b, 1.0f); pushS16(b, 0); pushU8(b, 0); pushU8(b, 0); pushU8(b, 0);
    pushU16(b, 1); // subglobCount=1
    pushTestSubglob(b, 6, 8, 2);
    pushU16(b, 1); // subcelCount=1
    pushTestSubcel(b, 2);
    pushU32(b, 0); // Alox
    pushU8(b, 0); // poseCopies
    // Child objects: count = 2 (child 0 = CID_LO 1, child 1 = CID_UNK11 107)
    pushU16(b, 2);
    pushS16(b, 1); pushS16(b, 401); pushS16(b, -1);
    pushS16(b, 95); pushU32(b, 0x44332211u); pushS16(b, -1);
    pushS16(b, 107); pushS16(b, 402); pushS16(b, -1);
    pushAloBase(b);
    pushS16(b, -1);
    pushEmptyGlobsetAndAloxTail(b);
    pushU16(b, 0);

    // Align to 16 bytes with non-zero check bytes
    while ((b.size() & 15u) != 0u) pushU8(b, 0);
    b.insert(b.end(), texPayload.begin(), texPayload.end());
    return b;
}
#endif // !TRACK_ALLOC

std::vector<std::uint8_t> buildStressFixture() {
    std::vector<std::uint8_t> b;
    b.reserve(16u * 1024u * 1024u);
    pushS32(b, 0); pushS32(b, 1);
    pushU8(b, 0);
    pushS16(b, 0);
    pushU32(b, 0);
    pushU32(b, 0); pushU32(b, 0); pushU32(b, 0);
    pushS16(b, -1);
    pushEncDesc(b, 0x100u, 0x200u, 0x0022u);
    pushNarrowStr(b, "The Hideout");
    pushEncDesc(b, 0x300u, 0x400u, 0x55u);

    // CLUT 0: 256 colors (1024 bytes), CLUT 1: 16 colors (64 bytes)
    std::vector<std::uint8_t> texPayload;
    const std::uint32_t clut256Off = 0;
    for (int i = 0; i < 256; ++i) {
        texPayload.push_back(static_cast<std::uint8_t>((i * 3) & 0xff));
        texPayload.push_back(static_cast<std::uint8_t>((i * 5) & 0xff));
        texPayload.push_back(static_cast<std::uint8_t>((i * 7) & 0xff));
        texPayload.push_back(0x80u);
    }
    const std::uint32_t clut16Off = static_cast<std::uint32_t>(texPayload.size());
    for (int i = 0; i < 16; ++i) {
        texPayload.push_back(static_cast<std::uint8_t>((i * 17) & 0xff));
        texPayload.push_back(static_cast<std::uint8_t>((255 - i * 11) & 0xff));
        texPayload.push_back(static_cast<std::uint8_t>((i * 23) & 0xff));
        texPayload.push_back(0x80u);
    }

    // Add 80 unique 256x256 8-bit bitmaps -> 80 * 256 KiB RGBA8 = 20 MiB output!
    // Add 150 unique 256x256 4-bit bitmaps (some with alpha < 255) -> 150 * 32/64 KiB BC = 4.8 MiB output!
    constexpr std::uint16_t kRgbaCount = 80;
    constexpr std::uint16_t kBcCount = 150;
    constexpr std::uint32_t kDim = 256;
    std::vector<std::uint32_t> bmOffsets;
    std::vector<std::uint32_t> bmSizes;
    bmOffsets.reserve(kRgbaCount + kBcCount);
    bmSizes.reserve(kRgbaCount + kBcCount);

    for (std::uint16_t i = 0; i < kRgbaCount; ++i) {
        bmOffsets.push_back(static_cast<std::uint32_t>(texPayload.size()));
        const std::uint32_t sz = kDim * kDim;
        bmSizes.push_back(sz);
        const std::size_t base = texPayload.size();
        texPayload.resize(base + sz);
        for (std::uint32_t p = 0; p < sz; ++p) {
            texPayload[base + p] = static_cast<std::uint8_t>((p + i * 17u) & 0xffu);
        }
    }
    for (std::uint16_t i = 0; i < kBcCount; ++i) {
        bmOffsets.push_back(static_cast<std::uint32_t>(texPayload.size()));
        const std::uint32_t sz = (kDim * kDim) / 2u;
        bmSizes.push_back(sz);
        const std::size_t base = texPayload.size();
        texPayload.resize(base + sz);
        for (std::uint32_t p = 0; p < sz; ++p) {
            texPayload[base + p] = static_cast<std::uint8_t>(((p + i * 29u) * 3u) & 0xffu);
        }
    }

    pushU16(b, 2); // nc = 2
    pushU32(b, 1); pushU16(b, 256); pushU16(b, 1); pushU32(b, clut256Off);
    pushU32(b, 1); pushU16(b, 16); pushU16(b, 1); pushU32(b, clut16Off);

    const std::uint16_t nb = kRgbaCount + kBcCount;
    pushU16(b, nb);
    for (std::uint16_t i = 0; i < nb; ++i) {
        pushU16(b, kDim); pushU16(b, kDim); pushU32(b, 1);
        pushS8(b, 0); pushS8(b, 0); pushU16(b, 0);
        pushU32(b, bmSizes[i]); pushU32(b, bmOffsets[i]);
    }

    // ns = 2 shaders: Shader 0 = Prelit (BC) with 150 4-bit textures (clut 1); Shader 1 = Effect (RGBA8) with 80 8-bit textures (clut 0)
    pushU16(b, 2); pushU16(b, 0);
    // Shader 0 (kind=1 -> BC1)
    pushU8(b, 1); pushU8(b, 0); pushS16(b, 1);
    for (int c = 0; c < 8; ++c) pushU8(b, 255);
    pushU32(b, 0); pushU16(b, 0); pushU8(b, 0); pushU8(b, static_cast<std::uint8_t>(kBcCount));
    pushU16(b, 0);
    for (std::uint16_t i = 0; i < kBcCount; ++i) {
        pushS16(b, static_cast<std::int16_t>(100 + i));
        pushS16(b, 0); pushU8(b, 1); pushU8(b, 1);
        pushU16(b, static_cast<std::uint16_t>(kRgbaCount + i));
        pushU16(b, 1);
    }
    // Shader 1 (kind=2 -> RGBA8)
    pushU8(b, 2); pushU8(b, 0); pushS16(b, 2);
    for (int c = 0; c < 8; ++c) pushU8(b, 255);
    pushU32(b, 0); pushU16(b, 0); pushU8(b, 0); pushU8(b, static_cast<std::uint8_t>(kRgbaCount));
    pushU16(b, 0);
    for (std::uint16_t i = 0; i < kRgbaCount; ++i) {
        pushS16(b, static_cast<std::int16_t>(300 + i));
        pushS16(b, 0); pushU8(b, 1); pushU8(b, 1);
        pushU16(b, i);
        pushU16(b, 0);
    }
    pushU16(b, 0); // nf = 0

    // Objects: 1 CID_ALO with 300 subglobs (vc=60, ic=200, poseCount=2 -> ~12 MB rebuilt geometry!)
    pushU16(b, 1);
    pushS16(b, 2); pushS16(b, 1); pushS16(b, -1);
    pushAloBase(b);
    pushS16(b, -1);
    pushU8(b, 0); pushU8(b, 0); pushU8(b, 0); pushU8(b, 2); pushU16(b, 1); // globCount=1, poseCount=2
    pushU16(b, 0);
    pushVec3(b, 0.0f, 0.0f, 0.0f); pushF32(b, 10.0f); pushS16(b, 0); pushU8(b, 0); pushU8(b, 0); pushU8(b, 0);
    constexpr std::uint16_t kSubglobs = 300;
    pushU16(b, kSubglobs);
    for (std::uint16_t s = 0; s < kSubglobs; ++s)
        pushTestSubglob(b, 60, 200, 2);
    pushU16(b, 0); // subcelCount=0
    pushU32(b, 0);
    pushU8(b, 0);
    pushU16(b, 0);

    while ((b.size() & 15u) != 0u) pushU8(b, 0);
    b.insert(b.end(), texPayload.begin(), texPayload.end());
    return b;
}

#ifndef TRACK_ALLOC
std::vector<std::uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("failed to read " + p.string());
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
}

bool statsEqual(const WalkStats& a, const WalkStats& b) {
    return a.objects == b.objects &&
           a.options == b.options &&
           a.geometries == b.geometries &&
           a.bspNodes == b.bspNodes &&
           a.stripVertices == b.stripVertices &&
           a.triangles == b.triangles;
}

void verifyEquivalent(const std::string& label,
                      const std::vector<std::uint8_t>& raw,
                      BrxSourceRegion region,
                      std::size_t maxBcCacheBytes) {
    namespace fs = std::filesystem;
    const fs::path dir = "/tmp/brx_stream_host_test";
    fs::create_directories(dir);
    const fs::path rawPath = dir / (label + ".brx");
    const fs::path refOut = dir / (label + "_Rebuilt.brx");
    const fs::path streamOut = dir / (label + "_stream.brx");

    {
        std::ofstream f(rawPath, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    }

    BrxWalker refWalker(StringEncoding::Narrow, region);
    refWalker.WalkFile(rawPath);

    BrxWalker streamWalker(StringEncoding::Narrow, region);
    streamWalker.SetMaxBcCacheBytes(maxBcCacheBytes);
    streamWalker.WalkMemory(raw.data(), raw.size(), streamOut);

    if (!statsEqual(refWalker.Stats(), streamWalker.Stats())) {
        throw std::runtime_error(label + ": WalkStats mismatch between WalkFile and WalkMemory");
    }

    const auto refBytes = readAll(refOut);
    const auto streamBytes = readAll(streamOut);
    if (refBytes != streamBytes) {
        std::size_t firstDiff = 0;
        while (firstDiff < refBytes.size() && firstDiff < streamBytes.size() &&
               refBytes[firstDiff] == streamBytes[firstDiff]) {
            ++firstDiff;
        }
        throw std::runtime_error(label + ": output mismatch (refSize=" +
            std::to_string(refBytes.size()) + " streamSize=" +
            std::to_string(streamBytes.size()) + " firstDiff=" +
            std::to_string(firstDiff) + ")");
    }
    std::fprintf(stderr, "[PASS] %s: exact byte-for-byte match (%zu bytes, maxBcCache=%zu)\n",
                 label.c_str(), streamBytes.size(), maxBcCacheBytes);
    fs::remove(rawPath);
    fs::remove(refOut);
    fs::remove(streamOut);
}
#endif // !TRACK_ALLOC

} // namespace

int main() {
    namespace fs = std::filesystem;
#ifdef TRACK_ALLOC
    (void)GetOptionSchema();
    const auto stressRaw = buildStressFixture();
    const fs::path outPath = "/tmp/brx_stream_host_test/stress_mem.brx";
    fs::create_directories(outPath.parent_path());

    {
        BrxWalker walker(StringEncoding::Narrow, BrxSourceRegion::NtscU);
        startTracking();
        walker.WalkMemory(stressRaw.data(), stressRaw.size(), outPath);
        stopTracking();
        const auto sz = fs::file_size(outPath);
        std::fprintf(stderr,
                     "[MEM default 6M cache] output=%.2f MiB | peakHeap=%.3f MiB | maxSingle=%.1f KiB | leaked=%zu B\n",
                     sz / (1024.0 * 1024.0), gPeakBytes / (1024.0 * 1024.0),
                     gMaxAlloc / 1024.0, gLiveBytes);
        assert(gPeakBytes > 0u);
        assert(gPeakBytes < 8u * 1024u * 1024u);
        assert(gMaxAlloc < 2u * 1024u * 1024u);
        assert(gLiveBytes == 0u);
    }
    {
        BrxWalker walker(StringEncoding::Narrow, BrxSourceRegion::NtscU);
        walker.SetMaxBcCacheBytes(256u * 1024u);
        startTracking();
        walker.WalkMemory(stressRaw.data(), stressRaw.size(), outPath);
        stopTracking();
        std::fprintf(stderr,
                     "[MEM 256K cache]     output=%.2f MiB | peakHeap=%.3f MiB | maxSingle=%.1f KiB | leaked=%zu B\n",
                     fs::file_size(outPath) / (1024.0 * 1024.0), gPeakBytes / (1024.0 * 1024.0),
                     gMaxAlloc / 1024.0, gLiveBytes);
        assert(gPeakBytes > 0u);
        assert(gPeakBytes < 4u * 1024u * 1024u);
        assert(gMaxAlloc < 2u * 1024u * 1024u);
        assert(gLiveBytes == 0u);
    }
    fs::remove(outPath);
    std::fprintf(stderr, "ALL MEMORY BOUNDS PASSED.\n");
#else
    const auto ntscFeatures = buildFeatureFixture(false);
    verifyEquivalent("ntsc_features_cached", ntscFeatures, BrxSourceRegion::NtscU, 6u * 1024u * 1024u);
    verifyEquivalent("ntsc_features_evicted", ntscFeatures, BrxSourceRegion::NtscU, 1u);

    const auto palFeatures = buildFeatureFixture(true);
    verifyEquivalent("pal_features_cached", palFeatures, BrxSourceRegion::Pal, 6u * 1024u * 1024u);

    const auto stressRaw = buildStressFixture();
    verifyEquivalent("stress_default_cache", stressRaw, BrxSourceRegion::NtscU, 6u * 1024u * 1024u);
    verifyEquivalent("stress_evict_256k", stressRaw, BrxSourceRegion::NtscU, 256u * 1024u);

    std::fprintf(stderr, "ALL BYTE-IDENTITY & ASAN/UBSAN CHECKS PASSED.\n");
#endif
    return 0;
}
