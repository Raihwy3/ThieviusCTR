#pragma once
#include "BinaryInputStream.h"
#include "scene_format.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
struct CaneLocalMesh { std::vector<CaneSceneVertex> vertices; std::uint32_t shader,priority; };
struct CaneObjectScope { int old; bool hidden; explicit CaneObjectScope(int cid,int oid); ~CaneObjectScope(); };
struct CaneTransformScope { glm::mat4 previous,local; CaneTransformScope* parent; bool optionsActive=true,animated=false; explicit CaneTransformScope(CBinaryInputStream&); ~CaneTransformScope(); };
struct CaneGlobSet {
    bool animated;
    std::vector<std::vector<CaneLocalMesh>> globs;
    CaneGlobSet(unsigned bones,unsigned poses);
};
struct CaneGlob {
    CaneGlobSet& set;unsigned index;unsigned priority=3;glm::mat4 transform{1};
    CaneGlob(CaneGlobSet& s,unsigned i):set(s),index(i){set.globs.emplace_back();}
    void instance(int source,CBinaryInputStream& s);
    void add(const std::vector<std::uint8_t>& bytes);
    void finish();
};
void CaneCaptureTexture(unsigned shader,const std::vector<std::uint8_t>& rgba,unsigned w,unsigned h,unsigned flags);
void CaneWriteScene();

void CaneCaptureOption(int id,CBinaryInputStream& stream);
