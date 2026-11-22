#pragma once
#include <citro3d.h>
#include <vector>
#include "scene_format.h"
struct CaneCamera { float x,y,z,yaw,pitch; };
class CaneScene {
public:
    CaneSceneHeader header{};
    std::vector<CaneSceneBatch> batches;
    std::vector<C3D_Tex> textures;
    CaneSceneVertex* vertices=nullptr;
    bool load(const char* path);
    void release();
    unsigned draw(const CaneCamera& camera,int projectionLocation);
};
CaneCamera caneResetCamera(const CaneSceneHeader& header);
