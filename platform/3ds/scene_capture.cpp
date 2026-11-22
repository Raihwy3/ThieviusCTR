#include "scene_capture.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
namespace {
int currentCid=-1;
bool suppressed=false;
CaneTransformScope* activeTransform=nullptr;
glm::mat4 world(1);
glm::vec3 spawn(0);bool haveSpawn=false;
unsigned skipped=0;
struct Batch { std::vector<CaneSceneVertex> vertices;unsigned shader,priority; };
std::vector<Batch> batches;
struct Texture {CaneSceneTexture header;std::vector<std::uint16_t> data;};
std::map<unsigned,Texture> textures;
float readFloat(const std::vector<uint8_t>& b,size_t at){float f;std::memcpy(&f,b.data()+at,4);if(!std::isfinite(f))throw std::runtime_error("Nonfinite mesh attribute");return f;}
uint16_t read16(const std::vector<uint8_t>& b,size_t at){return b.at(at)|(b.at(at+1)<<8);}
glm::mat4 matrix(CBinaryInputStream& s){glm::mat4 m(1);for(unsigned c=0;c<4;++c)for(unsigned r=0;r<3;++r)m[c][r]=s.F32Read();return m;}
unsigned morton(unsigned x,unsigned y){unsigned at=0;for(unsigned b=0;b<3;++b)at|=((x>>b)&1)<<(2*b)|((y>>b)&1)<<(2*b+1);return at;}
}
CaneObjectScope::CaneObjectScope(int cid,int oid):old(currentCid),hidden(suppressed){currentCid=cid;suppressed=suppressed||(oid>=0x0c&&oid<=0x2a);}
CaneObjectScope::~CaneObjectScope(){currentCid=old;suppressed=hidden;}
CaneTransformScope::CaneTransformScope(CBinaryInputStream& s):previous(world),parent(activeTransform){
    auto at=s.GetOffset();local=matrix(s);s.Seek(at);
    bool invalid=false;for(int c=0;c<4;++c)for(int r=0;r<3;++r)if(std::abs(local[c][r])>1e20f)invalid=true;
    if(invalid)local=glm::mat4(1);
    animated=parent&&parent->animated;world=previous*local;activeTransform=this;
}
CaneTransformScope::~CaneTransformScope(){
    if(currentCid==7&&!haveSpawn){spawn=glm::vec3(world[3]);haveSpawn=true;}
    world=previous;activeTransform=parent;
}
void CaneCaptureOption(int id,CBinaryInputStream& s){
    if(id==125){auto at=s.GetOffset();suppressed=suppressed||(s.U8Read()!=0);s.Seek(at);return;}
    if(!activeTransform||!activeTransform->optionsActive||(id!=29&&id!=32&&id!=35))return;
    auto at=s.GetOffset();auto v=s.ReadVector();s.Seek(at);
    glm::vec3 vec(v.x,v.y,v.z);
    if(id==29)activeTransform->local[3]=glm::vec4(vec,1);
    else {
        glm::mat4 rotation(1);
        if(id==32){float angle=glm::length(vec);if(angle>.0001f)rotation=glm::rotate(glm::mat4(1),angle,vec/angle);}
        else {
            vec*=.017453294f;
            rotation=glm::rotate(glm::mat4(1),vec.z,glm::vec3(0,0,1))*glm::rotate(glm::mat4(1),vec.y,glm::vec3(0,1,0))*glm::rotate(glm::mat4(1),vec.x,glm::vec3(1,0,0));
        }
        for(int c=0;c<3;++c)activeTransform->local[c]=rotation[c];
    }
    world=activeTransform->previous*activeTransform->local;
}
CaneGlobSet::CaneGlobSet(unsigned bones,unsigned poses):animated(bones||poses||(activeTransform&&activeTransform->animated)){if(animated&&activeTransform)activeTransform->animated=true;}
void CaneGlob::instance(int source,CBinaryInputStream& s){
    if(source<0||unsigned(source)>=index)throw std::runtime_error("Invalid mesh instance");
    transform=matrix(s);set.globs[index]=set.globs[source];
}
void CaneGlob::add(const std::vector<uint8_t>& b){
    unsigned vc=read16(b,0),tc=read16(b,2),shader=read16(b,4);
    if(b.size()<12+vc*72+tc*6)throw std::runtime_error("Truncated subglob");
    CaneLocalMesh mesh;mesh.shader=shader;mesh.priority=priority;mesh.vertices.reserve(tc*3);
    for(unsigned i=0;i<tc*3;++i){
        unsigned at=read16(b,12+vc*72+i*2);if(at>=vc)throw std::runtime_error("Invalid triangle index");
        unsigned base=12+at*72;CaneSceneVertex v{};
        for(unsigned n=0;n<3;++n)v.position[n]=readFloat(b,base+n*4);
        for(unsigned n=0;n<4;++n)v.color[n]=readFloat(b,base+24+n*4);
        for(unsigned n=0;n<2;++n)v.uv[n]=readFloat(b,base+40+n*4);
        mesh.vertices.push_back(v);
    }
    set.globs[index].push_back(std::move(mesh));
}
void CaneGlob::finish(){
    if(set.animated||suppressed){skipped+=set.globs[index].size();return;}
    if(priority!=1&&priority!=3&&priority!=4&&priority!=7&&priority!=8&&priority!=13){skipped+=set.globs[index].size();return;}
    // Some serialized instances retain undefined transform components.
    // Omit them explicitly in the static preview until runtime binding exists.
    bool placeholder=false;
    for(int c=0;c<4;++c)for(int r=0;r<3;++r)placeholder |= !std::isfinite(transform[c][r])||std::abs(transform[c][r])>1e20f;
    if(placeholder){skipped+=set.globs[index].size();return;}
    glm::mat4 combined=world*transform;
    for(const auto& mesh:set.globs[index]){
        if(mesh.vertices.empty())continue;
        Batch out{mesh.vertices,mesh.shader,priority};
        for(auto& v:out.vertices){auto p=combined*glm::vec4(v.position[0],v.position[1],v.position[2],1);for(unsigned i=0;i<3;++i){if(!std::isfinite(p[i]))throw std::runtime_error("Nonfinite transformed mesh");v.position[i]=p[i];}}
        batches.push_back(std::move(out));
    }
}
void CaneCaptureTexture(unsigned shader,const std::vector<uint8_t>& rgba,unsigned w,unsigned h,unsigned flags){
    if(textures.count(shader))return;
    if(!w||!h||rgba.size()!=size_t(w)*h*4)throw std::runtime_error("Invalid source texture");
    unsigned tw=8,th=8;while(tw<w&&tw<128)tw*=2;while(th<h&&th<128)th*=2;
    Texture tex{{tw,th,tw*th*2,flags},std::vector<uint16_t>(tw*th)};
    for(unsigned y=0;y<th;++y)for(unsigned x=0;x<tw;++x){
        unsigned sx=std::min(w-1,x*w/tw),sy=std::min(h-1,y*h/th);const auto* p=rgba.data()+(sy*w+sx)*4;
        unsigned dest=((y/8)*(tw/8)+x/8)*64+morton(x,y);
        tex.data[dest]=uint16_t((p[0]>>3)<<11|(p[1]>>3)<<6|(p[2]>>3)<<1|(p[3]>=64));
    }
    textures.emplace(shader,std::move(tex));
}
void CaneWriteScene(){
    const char* filename=std::getenv("CANE_SCENE_OUTPUT");if(!filename||!*filename)return;
    CaneSceneHeader header{0x454e4143,1,0,uint32_t(batches.size()),1,skipped,{spawn.x,spawn.y,spawn.z},{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f}};
    std::map<unsigned,unsigned> shaderTextures;std::vector<Texture> used{{{8,8,128,0},std::vector<uint16_t>(64,0xffff)}};
    std::vector<CaneSceneBatch> commands;
    for(const auto& b:batches){
        unsigned ti=0;
        if(textures.count(b.shader)){
            if(!shaderTextures.count(b.shader)){shaderTextures[b.shader]=used.size();used.push_back(textures.at(b.shader));}
            ti=shaderTextures.at(b.shader);
        }
        glm::vec3 lo(1e30f),hi(-1e30f);
        for(const auto& v:b.vertices)for(unsigned i=0;i<3;++i){lo[i]=std::min(lo[i],v.position[i]);hi[i]=std::max(hi[i],v.position[i]);header.minimum[i]=std::min(header.minimum[i],v.position[i]);header.maximum[i]=std::max(header.maximum[i],v.position[i]);}
        glm::vec3 center=(lo+hi)*.5f;
        commands.push_back({header.vertices,uint32_t(b.vertices.size()),ti,b.priority,{center.x,center.y,center.z},glm::length(hi-lo)*.5f});
        header.vertices+=b.vertices.size();
    }
    header.textures=used.size();
    std::ofstream file(filename,std::ios::binary);if(!file)throw std::runtime_error("Cannot write scene cache");
    auto write=[&](const void* p,size_t n){file.write((const char*)p,n);if(!file)throw std::runtime_error("Scene write failed");};
    write(&header,sizeof header);write(commands.data(),commands.size()*sizeof(CaneSceneBatch));
    for(const auto& b:batches)write(b.vertices.data(),b.vertices.size()*sizeof(CaneSceneVertex));
    size_t textureBytes=0;
    for(const auto& t:used){write(&t.header,sizeof t.header);write(t.data.data(),t.data.size()*2);textureBytes+=t.data.size()*2;}
    std::cout<<"3DS scene: "<<header.vertices/3<<" triangles, "<<header.batches<<" batches, "<<header.textures<<" textures ("<<textureBytes<<" bytes), "<<skipped<<" skipped animated/effect batches\n";
}
