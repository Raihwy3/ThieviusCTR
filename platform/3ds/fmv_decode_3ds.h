#pragma once

#include <3ds.h>
#include <citro3d.h>
#include <cstdint>
#include <string>

enum class CaneFmvKind { None, Bik, Cmov, M2v };

struct CaneFmvResolved {
    CaneFmvKind kind = CaneFmvKind::None;
    std::string videoPath;
    std::string wavPath;
};

struct CaneFmvDrawCallbacks {
    bool (*beginTarget)(void* ctx, bool wantWide, unsigned texW, unsigned texH,
                        unsigned srcW, unsigned srcH,
                        float u0, float v0, float u1, float v1,
                        float quadW, float quadH,
                        C3D_Tex** outTex0, C3D_Tex** outTex1) = nullptr;
    void (*presentFrame)(void* ctx, C3D_Tex* tex) = nullptr;
    void (*endTarget)(void* ctx) = nullptr;
    void* ctx = nullptr;
};

const char* CanePs3BikName3ds(int index);
bool CaneIsValidBikFile3ds(const char* path);
bool CaneIsValidM2vFile3ds(const char* path);
CaneFmvResolved CaneResolveCutscene3ds(const char* baseDir, int index);
bool CanePlayDecodedFmv3ds(int index, const CaneFmvResolved& src, const CaneFmvDrawCallbacks& draw);
