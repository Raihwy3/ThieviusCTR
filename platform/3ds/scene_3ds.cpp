#include "scene_3ds.h"
#include "bis.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <limits>
namespace {
bool finite3(const float* p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);}
}
bool CaneScene::load(const char* path){
    release();
    try {
        CBinaryInputStream input(path);input.ReadBytes(&header,sizeof header);
        if(header.magic!=0x454e4143||header.version!=1||!header.vertices||header.vertices>700000||header.batches>65536||!header.textures||header.textures>2048||!finite3(header.spawn)||!finite3(header.minimum)||!finite3(header.maximum))throw std::runtime_error("Invalid scene header");
        size_t vb=size_t(header.vertices)*sizeof(CaneSceneVertex),bb=size_t(header.batches)*sizeof(CaneSceneBatch);
        if(bb+vb>input.Remaining())throw std::runtime_error("Truncated scene");
        batches.resize(header.batches);input.ReadBytes(batches.data(),bb);
        for(const auto& b:batches)if(b.first>header.vertices||b.count>header.vertices-b.first||b.count%3||b.texture>=header.textures||!finite3(b.center)||!std::isfinite(b.radius)||b.radius<0)throw std::runtime_error("Invalid scene batch");
        vertices=(CaneSceneVertex*)linearAlloc(vb);if(!vertices)throw std::runtime_error("No memory for scene vertices");
        input.ReadBytes(vertices,vb);
        for(unsigned i=0;i<header.vertices;++i)for(unsigned n=0;n<9;++n){float v;std::memcpy(&v,(const char*)&vertices[i]+n*4,4);if(!std::isfinite(v))throw std::runtime_error("Invalid vertex attribute");}
        GSPGPU_FlushDataCache(vertices,vb);
        textures.reserve(header.textures);size_t textureBytes=0;
        for(unsigned i=0;i<header.textures;++i){
            CaneSceneTexture h{};input.ReadBytes(&h,sizeof h);
            if(h.width<8||h.height<8||h.width>128||h.height>128||(h.width&(h.width-1))||(h.height&(h.height-1))||h.bytes!=h.width*h.height*2||h.bytes>input.Remaining())throw std::runtime_error("Invalid scene texture");
            textureBytes+=h.bytes;if(textureBytes>12*1024*1024)throw std::runtime_error("Scene texture budget exceeded");
            C3D_Tex tex{};if(!C3D_TexInit(&tex,h.width,h.height,GPU_RGBA5551))throw std::runtime_error("No memory for scene textures");
            textures.push_back(tex);input.ReadBytes(textures.back().data,h.bytes);C3D_TexFlush(&textures.back());
            C3D_TexSetFilter(&textures.back(),GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(&textures.back(),GPU_REPEAT,GPU_REPEAT);
        }
        if(input.Remaining())throw std::runtime_error("Trailing scene bytes");
        printf("Paris scene loaded\nTriangles: %lu\nBatches: %lu  Textures: %lu\n",(unsigned long)header.vertices/3,(unsigned long)header.batches,(unsigned long)header.textures);
        return true;
    } catch(const std::exception& e){printf("Scene: %s\n",e.what());release();return false;}
}
void CaneScene::release(){for(auto& t:textures)C3D_TexDelete(&t);textures.clear();batches.clear();if(vertices)linearFree(vertices);vertices=nullptr;header={};}
CaneCamera caneResetCamera(const CaneSceneHeader& h){return {h.spawn[0]-2000,h.spawn[1]-3000,h.spawn[2]+1400,.982794f,-.30f};}
unsigned CaneScene::draw(const CaneCamera& camera,int location){
    C3D_Mtx projection,view,combined;
    float cp=std::cos(camera.pitch),sp=std::sin(camera.pitch),cy=std::cos(camera.yaw),sy=std::sin(camera.yaw);
    C3D_FVec eye=FVec3_New(camera.x,camera.y,camera.z),look=FVec3_New(camera.x+cp*cy,camera.y+cp*sy,camera.z+sp);
    Mtx_LookAt(&view,eye,look,FVec3_New(0,0,1),false);
    Mtx_PerspTilt(&projection,C3D_AngleFromDegrees(60),C3D_AspectRatioTop,2,30000,false);
    Mtx_Multiply(&combined,&projection,&view);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,location,&combined);
    C3D_BufInfo* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,vertices,sizeof(CaneSceneVertex),3,0x210);
    unsigned drawn=0,lastTexture=~0u;
    for(const auto& b:batches){
        float dx=b.center[0]-camera.x,dy=b.center[1]-camera.y,dz=b.center[2]-camera.z;
        float ahead=dx*cp*cy+dy*cp*sy+dz*sp;
        if(ahead+b.radius<2||ahead-b.radius>30000)continue;
        float side=-dx*sy+dy*cy,up=-dx*sp*cy-dy*sp*sy+dz*cp;
        if(std::fabs(side)>ahead*.9622505f+b.radius*1.4f||std::fabs(up)>ahead*.5773503f+b.radius*1.2f)continue;
        if(b.texture!=lastTexture){C3D_TexBind(0,&textures[b.texture]);lastTexture=b.texture;}
        C3D_DrawArrays(GPU_TRIANGLES,b.first,b.count);++drawn;
    }
    return drawn;
}
