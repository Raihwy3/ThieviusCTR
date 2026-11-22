#pragma once
#include <cstdint>
struct CaneSceneHeader {
    std::uint32_t magic,version,vertices,batches,textures,skipped;
    float spawn[3],minimum[3],maximum[3];
};
struct CaneSceneVertex { float position[3],color[4],uv[2]; };
struct CaneSceneBatch { std::uint32_t first,count,texture,priority; float center[3],radius; };
struct CaneSceneTexture { std::uint32_t width,height,bytes,flags; };
static_assert(sizeof(CaneSceneHeader)==60);
static_assert(sizeof(CaneSceneVertex)==36);
static_assert(sizeof(CaneSceneBatch)==32);
static_assert(sizeof(CaneSceneTexture)==16);
