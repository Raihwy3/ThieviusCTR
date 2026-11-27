// Native submission of original render packets. Gameplay owns transforms,
// animation, hidden flags, visibility and object lifetime; no scene snapshot.
#include <citro3d.h>
#include "fmv_decode_3ds.h"
#include "native_display_math.inc"
#include "render_batch_3ds.h"
#include "native_texture_etc.inc"
#include "flat_map_3ds.h"
extern "C" {
#include "vendor/PICASR/fsr3ds.inc"
}
#include <deque>
#include <map>
#include <set>
#include <cstring>
#include <malloc.h>
#include <dirent.h>
extern "C" {extern const unsigned char cane_shader[],cane_shader_end[];}
extern "C" {extern const unsigned char cane_movie_shader[],cane_movie_shader_end[];}
extern "C" {extern const unsigned char cane_stereo1_shader[],cane_stereo1_shader_end[];}
extern "C" {extern const unsigned char cane_flat1_shader[],cane_flat1_shader_end[];}
#include "font.h"
#include "assets/buttons-new-3ds.inc"
#include "coin.h"
#include "pzo.h"
#include "binoc.h"
#include "tv.h"
#include "speaker.h"
#include "blip.h"
#include "dysh.h"
#include "shadow.h"
#include "light.h"
#include "wipe.h"
#include "keyhole.h"
#include "po.h"
#include "wm.h"
extern void CanePumpVagStream3ds();
extern void CaneVagStats3ds(bool*,bool*,unsigned*,float*,float*,unsigned*,unsigned*);
struct CanePacket {GLOB* pglob;ALO* palo;glm::mat4 model;float alpha;RP rp;float z;float darken=1;bool dynamic=false;bool liveLight=false;TV* tv=nullptr;};
static std::vector<CanePacket> canePackets;
struct CaneParticles {BLIPG* group;glm::mat4 model;float alpha;};
static std::vector<CaneParticles> caneParticles;
struct CaneCaster {DYSH* owner;glm::mat4 model;float alpha;};
static std::vector<CaneCaster> caneCasters;
static TV* caneTvContext=nullptr;
static std::vector<RPLCEL> caneCelPackets;
void CaneCelNative(const RPLCEL& packet) {
    if(caneTvContext || !packet.palo || !packet.pglob || caneCelPackets.size()>=4096)return;
    caneCelPackets.push_back(packet);
}
enum class CaneHudLayer {Top,Coins,Health,Dialogue};
static bool caneLoading=false;
static bool caneStartupPresentation=true;
bool CaneTrophiesGameplay3ds() {return !caneStartupPresentation;}
static const char* caneLoadingText="Loading...";
static CaneHudLayer caneHudLayer=CaneHudLayer::Top;
static TV* canePortraitContext=nullptr;
static bool caneBottomAvailable=false,caneBottomActive=false;
struct CaneHudBatch {
    std::vector<BLOTLINEVERTEX> vertices;GLuint texture;TV* tv=nullptr;bool additive=false;
    CaneHudLayer layer=CaneHudLayer::Top;TV* portrait=nullptr;
    float scale=1,dx=0,dy=0;
};
static std::vector<CaneHudBatch> caneHud;
static std::vector<std::vector<BLOTLINEVERTEX>> caneHudVertPool;
static std::vector<BLOTLINEVERTEX> caneClipScratchA,caneClipScratchB;
static bool caneCompassClip=false,caneRectClip=false;
static glm::vec4 caneClipRect(0);
static void CaneClipUiInto(const BLOTLINEVERTEX*,size_t,std::vector<BLOTLINEVERTEX>&);
static void CaneClipRectUiInto(const BLOTLINEVERTEX*,size_t,std::vector<BLOTLINEVERTEX>&);
static void CaneRecycleHudBuffers() {
    for(auto& batch:caneHud) {
        if(!batch.vertices.empty() && caneHudVertPool.size()<64) {
            batch.vertices.clear();
            caneHudVertPool.push_back(std::move(batch.vertices));
        }
    }
    caneHud.clear();
}
static void CaneClearHudPool() {
    caneHud.clear();
    caneHudVertPool.clear();
    caneClipScratchA.clear();
    caneClipScratchB.clear();
}
static void CaneAppendHudVertices(const BLOTLINEVERTEX* data,size_t count,GLuint texture,bool additive) {
    if(count>30000 || caneHud.size()>1000)throw std::runtime_error("Native HUD budget exceeded");
    if(!count)return;
    static unsigned inspected=0;
    if(inspected++<6) {
        const auto& v=data[0];
        std::fprintf(stderr,"HUD glyph tex=%u pos=%.1f,%.1f rgba=%.2f,%.2f,%.2f,%.2f\n",unsigned(texture),v.position.x,v.position.y,v.color.r,v.color.g,v.color.b,v.color.a);
    }
    const BLOTLINEVERTEX* srcData=data;
    size_t srcCount=count;
    if(caneCompassClip) {
        CaneClipUiInto(srcData,srcCount,caneClipScratchA);
        srcData=caneClipScratchA.data();srcCount=caneClipScratchA.size();
    }
    if(caneRectClip && srcCount) {
        CaneClipRectUiInto(srcData,srcCount,caneClipScratchB);
        srcData=caneClipScratchB.data();srcCount=caneClipScratchB.size();
    }
    if(!srcCount)return;
    if(!caneHud.empty() && !caneHud.back().tv && caneHud.back().texture==texture &&
       caneHud.back().additive==additive && caneHud.back().layer==caneHudLayer &&
       caneHud.back().portrait==canePortraitContext &&
       caneHud.back().vertices.size()+srcCount<=30000) {
        caneHud.back().vertices.insert(caneHud.back().vertices.end(),srcData,srcData+srcCount);
    } else {
        std::vector<BLOTLINEVERTEX> buf;
        if(!caneHudVertPool.empty()) {
            buf=std::move(caneHudVertPool.back());
            caneHudVertPool.pop_back();
            buf.clear();
        }
        buf.insert(buf.end(),srcData,srcData+srcCount);
        caneHud.push_back({std::move(buf),texture,nullptr,additive,caneHudLayer,canePortraitContext});
    }
}
void CaneGlyphsNativeAdditive(const std::vector<BLOTLINEVERTEX>& vertices,GLuint texture,bool additive) {
    CaneAppendHudVertices(vertices.data(),vertices.size(),texture,additive);
}
void CaneGlyphsNative(const std::vector<BLOTLINEVERTEX>& vertices,GLuint texture) {
    CaneAppendHudVertices(vertices.data(),vertices.size(),texture,false);
}
void CaneQuadNative(float x,float y,float w,float h,GLuint texture,const glm::vec4& color) {
    BLOTLINEVERTEX vertices[6];
    const float xy[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
    for(unsigned i=0;i<6;++i) {
        vertices[i].position=glm::vec3(x+xy[i][0]*w,y+xy[i][1]*h,0);
        vertices[i].uv=glm::vec2(xy[i][0],1-xy[i][1]);vertices[i].color=color;
    }
    CaneAppendHudVertices(vertices,6,texture,false);
}
#include "runtime_ui.inc"
#include "runtime_bottom_ui.inc"
RPL& CaneQueueNative(const RPL& packet) {
    if(packet.PFNDRAWRPL==PreTvContext)caneTvContext=packet.ptv;
    if(packet.PFNDRAWRPL==PostTvContext)caneTvContext=nullptr;
    if(canePackets.size()>=20000)throw std::runtime_error("Native packet limit exceeded");
    static RPL context;context=packet;return context;
}
static std::vector<const LIGHT*> caneDynamicLights;
void CaneSubmitNative(const RPL& packet) {

    if(packet.rp==RP_DynamicTexture) {
        if(packet.pdysh && packet.pdysh->pshadowGen && caneCasters.size()<4)
            caneCasters.push_back({packet.pdysh,packet.ro.model,packet.ro.uAlpha});
        return;
    }
    if(packet.rp==RP_Blip) {
        if(packet.pblipg && caneParticles.size()<256)caneParticles.push_back({packet.pblipg,packet.ro.model,packet.ro.uAlpha});
        return;
    }
    if(!packet.pglob || !packet.palo)return;
    if(packet.rp!=RP_Background && packet.rp!=RP_Opaque && packet.rp!=RP_Cutout &&
       packet.rp!=RP_OpaqueAfterProjVolume && packet.rp!=RP_CutoutAfterProjVolume &&
       packet.rp!=RP_MurkClear && packet.rp!=RP_MurkOpaque && packet.rp!=RP_MurkFill &&
       packet.rp!=RP_Translucent && packet.rp!=RP_ProjVolume && packet.rp!=RP_Foreground &&
       packet.rp!=RP_WorldMap && !(caneTvContext && packet.rp==RP_BlotContext))return;
    if(canePackets.size()>=20000)throw std::runtime_error("Native packet limit exceeded");
    bool liveLight=false;
    if(packet.ro.fDynamicLight && !caneDynamicLights.empty()) {
        const glm::vec3 center=glm::vec3(packet.ro.model*glm::vec4(packet.pglob->posCenter,1));
        for(const LIGHT* light:caneDynamicLights) {
            if(light->lightk==LIGHTK_Direction) {liveLight=true;break;}
            const float radius=packet.pglob->sRadius+light->lmFallOffS.gMax;
            if(glm::distance2(center,light->xf.posWorld)<=radius*radius) {liveLight=true;break;}
        }
    }
    canePackets.push_back({packet.pglob,packet.palo,packet.ro.model,packet.ro.uAlpha,packet.rp,packet.z,packet.ro.darken,packet.ro.fDynamic!=0 || caneTvContext || packet.rp==RP_WorldMap,liveLight || caneTvContext,caneTvContext});
}
class CaneRuntimeGraphics {
    struct Vertex {float position[3];uint8_t color[4];float uv[2];};
    static_assert(sizeof(Vertex)==24 && offsetof(Vertex,color)==12 && offsetof(Vertex,uv)==16);
    static inline uint8_t packByte(float v) {
        int i=int(v*255.0f+0.5f);
        return uint8_t(i<0?0:(i>255?255:i));
    }
    static inline void setVertexColor(Vertex& v,float r,float g,float b,float a) {
        v.color[0]=packByte(r);v.color[1]=packByte(g);v.color[2]=packByte(b);v.color[3]=packByte(a);
    }
    struct IndexBuf {uint16_t* data=nullptr;unsigned count=0;const INDICE* source=nullptr;GLuint vao=0;u32 physOffset=~0u;};
    struct WorldChunk {Vertex* vertices=nullptr;unsigned used=0;unsigned dirtyMin=~0u;unsigned dirtyMax=0;u32 bufOffset=0;};
    static constexpr unsigned kWorldChunkVerts=65536;
    static constexpr unsigned kMaxWorldChunks=12;
    std::vector<WorldChunk> worldChunks;
    static constexpr unsigned kLocalChunkVerts=16384;
    static constexpr unsigned kMaxLocalChunks=16;
    std::vector<WorldChunk> localChunks;
    struct LocalFreeBlock {int chunk=-1;unsigned base=0;unsigned cap=0;};
    std::vector<LocalFreeBlock> localFreeList;
    struct SubCache {
        int worldChunk=-1;unsigned worldBase=0;
        std::vector<uint16_t> worldIndices;
        Vertex* localVerts=nullptr;u32 localBufOffset=0;size_t count=0;const VERTICE* source=nullptr;
        int localChunk=-1;unsigned localBase=0;unsigned localCap=0;bool localOwned=false;
        const uint16_t* localIndices=nullptr;unsigned localIndexCount=0;u32 localIdxPhysOffset=~0u;
        glm::vec3 worldCenter{0};float worldRadius=0;
        uint16_t batchKey=0xFFFFu;
        uint8_t texMode=0;int16_t id0=-1,id1=-1,id2=-1;
        C3D_Tex* tex0=nullptr;C3D_Tex* tex1=nullptr;C3D_Tex* tex2=nullptr;
        bool texValid=false;bool threeWay=false;bool hasSoftAlpha=false;
    };
    struct GlobCache {
        GLuint vao=0;glm::mat4 model{1};unsigned used=0,seenFrame=0;
        bool moved=false;bool flipped=false;bool anyWorldBaked=false;bool instanced=false;
        std::vector<SubCache> subs;
    };
    struct GlobKey {
        const GLOB* glob;const ALO* palo;
        bool operator==(const GlobKey& o) const {return glob==o.glob && palo==o.palo;}
    };
    struct GlobKeyHash {
        size_t operator()(const GlobKey& k) const {return (uintptr_t(k.glob)>>4)^(uintptr_t(k.palo)*2654435761u);}
    };
    static inline float fastRsqrt(float x) {
        uint32_t i;
        std::memcpy(&i,&x,sizeof(i));
        i=0x5f3759dfu-(i>>1);
        float y;
        std::memcpy(&y,&i,sizeof(y));
        return y*(1.5f-0.5f*x*y*y);
    }
    enum class DrawKind : uint8_t { Opaque,StandardAlpha,BinaryCutout,TwoPassAlpha,TwoPassAdditive,OnePassAlpha,OnePassAdditive,ProjVolume };
    enum class BlendMode : uint8_t { Opaque,Alpha,Additive,Unknown };
    struct Draw {
        const Vertex* vertices;u32 bufOffset;const uint16_t* indices;unsigned count;
        C3D_Tex* tex0;C3D_Tex* tex1;C3D_Tex* tex2;uint8_t texMode;bool worldBaked;bool flipped;bool hasSoftAlpha;
        const CanePacket* material;RP phase;float distance;const SHD* shader;const SUBGLOB* sub;
        uint32_t mvpIdx;float tintDarken,tintAlpha;DrawKind kind;u32 idxPhysOffset=~0u;
    };
    struct BatchKeyDesc {
        int chunkIdx=-1;uint8_t texMode=0;C3D_Tex* tex0=nullptr;C3D_Tex* tex1=nullptr;C3D_Tex* tex2=nullptr;
        bool operator==(const BatchKeyDesc& o) const {
            return chunkIdx==o.chunkIdx && texMode==o.texMode && tex0==o.tex0 && tex1==o.tex1 && tex2==o.tex2;
        }
    };
    struct BatchKeyHash {
        size_t operator()(const BatchKeyDesc& k) const {
            size_t h=size_t(k.chunkIdx)*2654435761u^k.texMode;
            h^=uintptr_t(k.tex0)*1140071481u;
            h^=uintptr_t(k.tex1)*1402946736u;
            h^=uintptr_t(k.tex2)*1609587929u;
            return h^(h>>16);
        }
    };
    struct BatchEntry {
        const uint16_t* src;unsigned count;uint16_t key;float distSq;
    };
    struct MergedDraw {
        int chunkIdx;uint8_t texMode;C3D_Tex* tex0;C3D_Tex* tex1;C3D_Tex* tex2;
        unsigned indexOffset;unsigned indexCount;float minDistSq;
    };
    struct FadedWorldDraw {
        const uint16_t* src;unsigned count;const Vertex* chunkVerts;u32 bufOffset;uint8_t texMode;
        C3D_Tex* tex0;C3D_Tex* tex1;C3D_Tex* tex2;const CanePacket* material;RP phase;const SUBGLOB* sub;bool hasSoftAlpha;
    };
    struct ShadowReceiver {
        unsigned mapIndex;const Vertex* vertices;const CanePacket* packet;const SUBGLOB* sub;bool worldBaked;
    };
    struct CasterSub {const Vertex* vertices;u32 bufOffset;const uint16_t* indices;unsigned indexCount;};
    struct ShadowPass {unsigned firstSub,subCount,overlayFirst=0,overlayCount=0;glm::mat4 projection;float alpha;bool renderMap=true;};
    struct CasterVert {const SUBGLOB* sub;const Vertex* vertices;u32 bufOffset;};
    struct ParticleDraw {unsigned first,count;C3D_Tex* tex;bool additive;uint8_t maxAlpha;};
    struct HudDraw {
        unsigned first,count;C3D_Tex* tex;TV* tv;bool additive=false;
        CaneHudLayer layer=CaneHudLayer::Top;
        float scale=1,dx=0,dy=0;
    };
    struct ActivePose {size_t index;float weight;};
    DVLB_s* shader=nullptr;shaderProgram_s program{};
    DVLB_s* movieShader=nullptr;shaderProgram_s movieProgram{};
    bool movieProgramStarted=false;
    int movieProjection=-1,movieTint=-1;
    DVLB_s* stereo1Shader=nullptr;shaderProgram_s stereo1Program{};
    bool stereo1ProgramStarted=false;
    DVLB_s* flat1Shader=nullptr;shaderProgram_s flat1Program{};
    bool flat1ProgramStarted=false;
    shaderProgram_s* boundProgram=nullptr;
    bool sceneUseStereo=false;
    bool tintValid=false;bool inFrameCmd=false;float lastTintR=0,lastTintG=0,lastTintB=0,lastTintA=0;
    static u32 physOffsetFor(const void* ptr) {
        if(!ptr)return ~0u;
        const u32 phys=osConvertVirtToPhys(const_cast<void*>(ptr));
        return phys>=0x18000000u?(phys-0x18000000u):~0u;
    }
    static u32 bufOffsetFor(const void* ptr) {
        C3D_BufInfo info;
        BufInfo_Init(&info);
        BufInfo_Add(&info,ptr,sizeof(Vertex),3,0x210);
        return info.buffers[0].offset;
    }
    C3D_RenderTarget* target=nullptr;
    C3D_RenderTarget* rightTarget=nullptr;
    C3D_RenderTarget* bottomTarget=nullptr;
    PrintConsole* bottomConsole=nullptr;
    std::vector<u16> bottomConsoleShadow;
    const uint32_t* bottomConsoleGen=nullptr;
    struct BottomFbStamp {void* ptr=nullptr;uint32_t gen=0;bool valid=false;};
    BottomFbStamp bottomFbStamps[2]{};
    bool bottomOutput=false;
    C3D_Tex postTexture{};
    C3D_RenderTarget* postTarget=nullptr;
    bool postTextureReady=false;
    int displayMode=0,displayRequest=-1,shadowSize=128;
    bool stereoWasActive=false;
    bool started=false,programStarted=false;
    int projection=-1,tint=-1,stereo=-1,filterOffset=-1,filterOffsetNegative=-1;
    struct PreLight {
        const LIGHT* light;
        glm::vec3 dir;
        glm::vec3 pos;
        glm::vec3 rgbaColor;
        float maxR,maxRSq;
        float fallOffX,fallOffY;
        float ruShadow,duShadow;
        float ruMidtone,duMidtone;
        float ruHighlight,duHighlight;
        uint8_t kind;
        bool fDynamic;
        bool fExcludeDynamic;
    };
    static PreLight makePreLight(const LIGHT* light) {
        glm::vec3 dir(0);
        if(light->lightk==LIGHTK_Direction) {glm::vec3 d=light->xf.matWorld[2];float l=glm::length(d);if(l>.0001f)dir=d/l;}
        else if(light->lightk==LIGHTK_Frustrum || light->lightk==LIGHTK_Spot) {glm::vec3 d=light->xf.matWorld*light->normalLocal;float l=glm::length(d);if(l>.0001f)dir=d/l;}
        const float r=light->lmFallOffS.gMax;
        return {light,dir,light->xf.posWorld,light->rgbaColor,r,r*r,
                light->agFallOff.x,light->agFallOff.y,
                light->ltfn.ruShadow,light->ltfn.duShadow,
                light->ltfn.ruMidtone,light->ltfn.duMidtone,
                light->ltfn.ruHighlight,light->ltfn.duHighlight,
                uint8_t(light->lightk),light->fDynamic!=0,light->fExcludeDynamicObjects!=0};
    }
    static inline glm::vec3 transformPoint(const glm::mat4& m,const glm::vec3& p) {
        return glm::vec3(
            m[0][0]*p.x+m[1][0]*p.y+m[2][0]*p.z+m[3][0],
            m[0][1]*p.x+m[1][1]*p.y+m[2][1]*p.z+m[3][1],
            m[0][2]*p.x+m[1][2]*p.y+m[2][2]*p.z+m[3][2]);
    }
    std::vector<PreLight> frameLights;
    size_t frameDirLightCount=0;
    struct WorkerScratch {
        std::vector<PreLight> meshLights;
        std::vector<glm::mat3> boneNormMats;
        std::vector<ActivePose> activePoses;
        std::vector<glm::mat4> boneMvps;
        std::vector<glm::vec4> celClip;
        std::vector<glm::vec2> celNdc;
        std::vector<uint8_t> celValid;
        std::vector<glm::vec4> shadowUv;
        std::vector<uint8_t> shadowMask;
        std::vector<uint8_t> boneReady;
        void reserveDefault() {
            meshLights.reserve(64);boneNormMats.reserve(64);activePoses.reserve(16);
            boneMvps.reserve(64);boneReady.reserve(64);celClip.reserve(1024);celNdc.reserve(1024);celValid.reserve(1024);
            shadowUv.reserve(512);shadowMask.reserve(512);
        }
    };
    WorkerScratch mainScratch;
    C3D_Tex white{};
    C3D_Tex newButtonTexture{};
    int buttonImage=-1;
    uint16_t* texStage=nullptr;size_t texStageBytes=0;
    unsigned vramTexCount=0,fcramTexCount=0,etcTexCount=0;
    size_t vramTextureBytes=0,fcramTextureBytes=0,textureSavedBytes=0;
    static size_t vramTextureBudget(int mode) {
        if(mode==0)return 4096u*1024u;
        if(mode==3)return 1280u*1024u;
        if(mode==6)return 1792u*1024u;
        return 3456u*1024u;
    }
    static size_t vramTextureHeadroom(int mode) {
        return mode==3?384u*1024u:512u*1024u;
    }
    struct ShadowMap {
        C3D_Tex texture{};C3D_RenderTarget* target=nullptr;
        const DYSH* lastOwner=nullptr;glm::mat4 lastProjection{0};
        float lastAlpha=-1.0f;uint64_t lastPoseHash=0;
        unsigned lastUpdateFrame=0;bool valid=false;
    };
    ShadowMap shadowMaps[4];
    static uint64_t hashCasterState(const DYSH* owner) {
        const auto& set=owner->globset;
        uint64_t meta[4]={uint64_t(set.boneMatrices.size()),uint64_t(set.agPoses.size()),uint64_t(set.aglob.size()),
                          set.aglob.empty()?0ull:uint64_t(set.aglob[0].VAO)};
        uint64_t h=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(meta),sizeof(meta));
        if(!set.boneMatrices.empty())
            h=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(set.boneMatrices.data()),set.boneMatrices.size()*sizeof(glm::mat4),h);
        if(!set.agPoses.empty())
            h=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(set.agPoses.data()),set.agPoses.size()*sizeof(float),h);
        return h;
    }
    struct TexSlot {C3D_Tex tex{};bool loaded=false;bool inVram=false;};
    std::vector<TexSlot> textures;
    std::vector<cane_texture_etc::PreparedImage> preparedImages;
    struct SharedAtlas {uint64_t key;std::vector<uint8_t> pixels;};
    std::vector<SharedAtlas> sharedAtlases;
    size_t sharedAtlasBytes=0;
    static constexpr size_t kSharedAtlasBudget=640*1024;
    CaneFlatMap<GLuint,int> glTexToImage;
    std::deque<GlobCache> globCachePool;
    std::vector<uint32_t> globCacheFree;
    CaneFlatMap<GlobKey,uint32_t,GlobKeyHash> globCacheMap;
    CaneFlatMap<const SUBGLOB*,IndexBuf,CanePtrHash> indexBuffers;
    struct CelCache {const CELPOINT* source=nullptr;size_t count=0;GLuint vao=0;std::vector<unsigned> indices,first;};
    std::deque<CelCache> celCachePool;
    CaneFlatMap<const GLOB*,uint32_t,CanePtrHash> celCacheMap;
    Vertex* dynamic[2]{};u32 dynamicBufOffset[2]{};size_t dynamicCapacity=0,meshBytes=0;
    Vertex* postQuad=nullptr;u32 postQuadBufOffset=0;
    uint16_t* batchIndices[2]{};u32 batchIndicesPhys[2]{~0u,~0u};size_t batchIndexCapacity=0;
    unsigned slot=0,frame=0;
    CaneFlatMap<BatchKeyDesc,uint16_t,BatchKeyHash> batchKeyMap;
    std::vector<BatchKeyDesc> batchKeyTable;
    std::vector<uint32_t> bucketHead,entryNext;
    std::vector<uint16_t> touchedKeys;
    struct LiveKey {
        const SUBGLOB* sub=nullptr;const ALO* palo=nullptr;
        bool operator==(const LiveKey& o) const {return sub==o.sub && palo==o.palo;}
    };
    struct LiveKeyHash {
        size_t operator()(const LiveKey& k) const {return (uintptr_t(k.sub)>>4)^(uintptr_t(k.palo)*2654435761u);}
    };
    struct LiveEntry {
        Vertex* verts[2]{nullptr,nullptr};u32 bufOffset[2]{0,0};
        int chunk[2]{-1,-1};unsigned base[2]{0,0};unsigned cap[2]{0,0};bool owned[2]{false,false};
        const VERTICE* source=nullptr;size_t count=0;GLuint vao=0;
        glm::mat4 model{1};
        uint8_t curSlot=0,texMode=0;bool hasSoftAlpha=false;
        int16_t i0=-1,i1=-1,i2=-1;
        unsigned lastRelightFrame=0,usedFrame=0,seenThisFrame=0;
    };
    CaneFlatMap<LiveKey,LiveEntry,LiveKeyHash> liveLightCache;
    struct LiveDecision {uint32_t packetIdx=0,subIdx=0;bool active=false,relit=false;};
    std::vector<LiveDecision> liveDecisions;
    struct OwnedLiveFlush {Vertex* ptr=nullptr;size_t bytes=0;};
    std::vector<OwnedLiveFlush> ownedLiveFlush;
    std::vector<Draw> phaseBuckets[RP_Max],tvDraws;
    std::vector<BatchEntry> staticBatches[RP_Max];
    std::vector<MergedDraw> mergedDraws[RP_Max];
    std::vector<FadedWorldDraw> fadedWorldDraws;
    std::vector<ShadowReceiver> shadowReceivers;
    std::vector<CasterVert> casterDynVerts[4];
    std::vector<CasterSub> casterSubs;
    std::vector<ShadowPass> shadowPasses;
    std::vector<ParticleDraw> particleDraws;
    std::vector<HudDraw> hudDraws;
    static constexpr unsigned outlineBudget=4096;
    static bool isBinaryAlphaTex(const C3D_Tex* tex) {
        return !tex || tex->fmt==GPU_RGBA5551 || tex->fmt==GPU_ETC1;
    }
    struct CelJob {const RPLCEL* packet;const CelCache* cache;glm::mat4 mvp;glm::vec4 color;float minLinePixels;};
    struct alignas(32) AnimJob {
        uint32_t packetIdx,subIdx,dynOffset,boneNormOffset,boneNormCount;
        float scale;uint8_t texMode;bool hasSoftAlpha;bool forceOneTex;int16_t i0,i1,i2;
        Vertex* dstOverride=nullptr;LiveKey liveKey{};uint8_t liveWriteSlot=0;
    };
    struct AnimDrawSlot {uint32_t drawIdx;uint8_t bucket;bool isTv;};
    struct HudJob {const CaneHudBatch* hud;uint32_t dynOffset;};
    struct CasterJob {uint32_t mapIndex;GLOB* glob;SUBGLOB* sub;uint32_t dynOffset;float scale;};
    struct ParticleJob {uint32_t packetIdx;uint32_t maxBlips;C3D_Tex* tableTex[32]{};};
    struct ShadowMapJobInfo {const SHADOW* shadow;glm::vec3 negCastDir;glm::vec3 camPos;float cl;uint8_t alphaByte;};
    enum class WaveMode : uint32_t {None=0,Prep1=1,ShadowClip=2};
    static constexpr unsigned kMaxWorkers=2;
    struct Worker {
        CaneRuntimeGraphics* owner=nullptr;
        Thread handle=nullptr;
        LightEvent kick{};
        LightEvent done{};
        WorkerScratch scratch;
        int coreId=-1;
        unsigned index=0;
        unsigned jobsDone=0;
        u64 busyTicks=0;
    };
    Worker workers[kMaxWorkers];
    unsigned workerCount=0;
    uint32_t poolStop=0;
    WaveMode waveMode=WaveMode::None;
    uint32_t waveNextJob=0,waveTotalJobs=0;
    std::vector<CelJob> celJobs;
    std::vector<std::vector<Vertex>> celJobVerts;
    std::vector<AnimJob> animJobs;
    std::vector<AnimDrawSlot> animDrawSlots;
    std::vector<glm::mat3> allBoneNormMats;
    std::vector<HudJob> hudJobs;
    std::vector<CasterJob> casterJobs;
    std::vector<ParticleJob> particleJobs;
    std::vector<std::vector<Vertex>> particleJobVerts;
    std::vector<std::vector<ParticleDraw>> particleJobDraws;
    glm::vec3 frameCamRight{1,0,0},frameCamUp{0,1,0};
    ShadowMapJobInfo shadowMapInfo[4]{};
    std::vector<std::vector<Vertex>> shadowJobOut;
    std::vector<C3D_Mtx> packetMvps;
    std::vector<uint8_t> packetMvpValid;
    std::vector<uint32_t> packetGcIdx;
    struct WaveGuard {
        CaneRuntimeGraphics* g;bool active;
        void finish() {if(active) {g->waitWave();active=false;}}
        ~WaveGuard() {finish();}
    };
    static unsigned morton(unsigned x,unsigned y) {
        unsigned n=0;for(unsigned b=0;b<3;++b)n|=((x>>b)&1)<<(2*b)|((y>>b)&1)<<(2*b+1);return n;
    }
    static int imageFor(const SUBGLOB& sub) {
        if(!sub.pshd || sub.pshd->atex.empty() || sub.pshd->atex[0].diffuseImageIndex.empty())return -1;
        int frame=sub.uvSai?sub.uvSai->iframe:0;
        const auto& images=sub.pshd->atex[0].diffuseImageIndex;
        return images[std::clamp(frame,0,int(images.size())-1)];
    }
    static std::array<byte,4> pixel(const BMP& image,unsigned x,unsigned y) {
        const auto& bytes=image.diffuseTexture;
        if(image.textureFormat==0) {
            size_t offset=(size_t(y)*image.bmpWidth+x)*4;
            if(offset+4>bytes.size())throw std::runtime_error("Truncated actor RGBA image");
            return {bytes[offset],bytes[offset+1],bytes[offset+2],bytes[offset+3]};
        }
        if(image.textureFormat>2)throw std::runtime_error("Unsupported actor texture encoding");
        bool bc3=image.textureFormat==2;
        size_t offset=(size_t(y/4)*((image.bmpWidth+3)/4)+x/4)*(bc3?16:8);
        if(offset+(bc3?16:8)>bytes.size())throw std::runtime_error("Truncated actor BC image");
        const byte* block=bytes.data()+offset;
        const byte* color=block+(bc3?8:0);
        auto u16=[](const byte* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
        unsigned c0=u16(color),c1=u16(color+2);
        auto rgb=[](unsigned c) {
            unsigned r=(c>>11)&31,g=(c>>5)&63,b=c&31;
            return std::array<byte,4>{byte((r<<3)|(r>>2)),byte((g<<2)|(g>>4)),byte((b<<3)|(b>>2)),255};
        };
        std::array<std::array<byte,4>,4> palette{rgb(c0),rgb(c1)};
        for(unsigned c=0;c<3;++c) {
            if(c0>c1 || bc3) {
                palette[2][c]=(2*unsigned(palette[0][c])+palette[1][c])/3;
                palette[3][c]=(unsigned(palette[0][c])+2*palette[1][c])/3;
            } else {palette[2][c]=(unsigned(palette[0][c])+palette[1][c])/2;palette[3][c]=0;}
        }
        unsigned index=(y%4)*4+x%4;
        unsigned bits=unsigned(color[4])|(unsigned(color[5])<<8)|(unsigned(color[6])<<16)|(unsigned(color[7])<<24);
        auto result=palette[(bits>>(index*2))&3];result[3]=255;
        if(bc3) {
            std::array<unsigned,8> alpha{block[0],block[1]};
            if(alpha[0]>alpha[1])for(unsigned i=1;i<=6;++i)alpha[i+1]=((7-i)*alpha[0]+i*alpha[1])/7;
            else {for(unsigned i=1;i<=4;++i)alpha[i+1]=((5-i)*alpha[0]+i*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
            uint64_t selectors=0;for(unsigned b=0;b<6;++b)selectors|=uint64_t(block[b+2])<<(8*b);
            result[3]=alpha[(selectors>>(index*3))&7];
        }
        return result;
    }
    int imageForGlTex(GLuint glTex) {
        if(!glTex)return -1;
        if(int* found=glTexToImage.find(glTex)) {
            int idx=*found;
            if(idx>=0 && size_t(idx)<g_abmp.size() && g_abmp[idx].glDiffuseMap==glTex)return idx;
        }
        for(size_t i=0;i<g_abmp.size();++i)if(g_abmp[i].glDiffuseMap==glTex) {glTexToImage[glTex]=int(i);return int(i);}
        return -1;
    }
    C3D_Tex* selectedTexture(int image,C3D_Tex* original) {
        return image==buttonImage && newButtonTexture.data && CaneUseNewButtons3ds()?&newButtonTexture:original;
    }
    static void generateMipmaps16(uint16_t* base,unsigned width,unsigned height,int maxLevel,bool rgba4) {
        const uint16_t* src=base;
        unsigned sw=width,sh=height;
        for(int lvl=1;lvl<=maxLevel;++lvl) {
            const unsigned dw=sw>>1,dh=sh>>1;
            uint16_t* dst=const_cast<uint16_t*>(src)+size_t(sw)*sh;
            const unsigned sTilesW=sw>>3,dTilesW=dw>>3;
            for(unsigned dy=0;dy<dh;++dy) {
                const unsigned sy=dy<<1,dty=(dy>>3)*dTilesW,sty=(sy>>3)*sTilesW;
                for(unsigned dx=0;dx<dw;++dx) {
                    const unsigned sx=dx<<1;
                    const unsigned sBase=(sty+(sx>>3))*64u+morton(sx,sy);
                    const unsigned dIdx=(dty+(dx>>3))*64u+morton(dx,dy);
                    const uint16_t p0=src[sBase],p1=src[sBase+1],p2=src[sBase+2],p3=src[sBase+3];
                    if(rgba4) {
                        const unsigned r=(((p0>>12)&15)+((p1>>12)&15)+((p2>>12)&15)+((p3>>12)&15)+2)>>2;
                        const unsigned g=(((p0>>8)&15)+((p1>>8)&15)+((p2>>8)&15)+((p3>>8)&15)+2)>>2;
                        const unsigned b=(((p0>>4)&15)+((p1>>4)&15)+((p2>>4)&15)+((p3>>4)&15)+2)>>2;
                        const unsigned a=((p0&15)+(p1&15)+(p2&15)+(p3&15)+2)>>2;
                        dst[dIdx]=uint16_t((r<<12)|(g<<8)|(b<<4)|a);
                    } else {
                        const unsigned a0=p0&1u,a1=p1&1u,a2=p2&1u,a3=p3&1u;
                        const unsigned aCnt=a0+a1+a2+a3;
                        if(aCnt>0) {
                            unsigned r=0,g=0,b=0;
                            if(a0) {r+=(p0>>11)&31;g+=(p0>>6)&31;b+=(p0>>1)&31;}
                            if(a1) {r+=(p1>>11)&31;g+=(p1>>6)&31;b+=(p1>>1)&31;}
                            if(a2) {r+=(p2>>11)&31;g+=(p2>>6)&31;b+=(p2>>1)&31;}
                            if(a3) {r+=(p3>>11)&31;g+=(p3>>6)&31;b+=(p3>>1)&31;}
                            r=(r+(aCnt>>1))/aCnt;
                            g=(g+(aCnt>>1))/aCnt;
                            b=(b+(aCnt>>1))/aCnt;
                            dst[dIdx]=uint16_t((r<<11)|(g<<6)|(b<<1)|(aCnt>=2?1u:0u));
                        } else {
                            const unsigned r=(((p0>>11)&31)+((p1>>11)&31)+((p2>>11)&31)+((p3>>11)&31)+2)>>2;
                            const unsigned g=(((p0>>6)&31)+((p1>>6)&31)+((p2>>6)&31)+((p3>>6)&31)+2)>>2;
                            const unsigned b=(((p0>>1)&31)+((p1>>1)&31)+((p2>>1)&31)+((p3>>1)&31)+2)>>2;
                            dst[dIdx]=uint16_t((r<<11)|(g<<6)|(b<<1));
                        }
                    }
                }
            }
            src=dst;sw=dw;sh=dh;
        }
    }
    C3D_Tex* texture(int image,bool atlas=false,bool smoothAlpha=false,unsigned limit=128) {
        if(image<0 || size_t(image)>=g_abmp.size())return &white;
        if(size_t(image)<textures.size() && textures[image].loaded)return selectedTexture(image,&textures[image].tex);
        if(size_t(image)>=textures.size())textures.resize(g_abmp.size());
        BMP& source=g_abmp[image];
        if(!source.bmpWidth || !source.bmpHeight || source.diffuseTexture.empty())
            throw std::runtime_error("Actor image pixels are unavailable");
        CFontBrx* joy=FFontLoaded(1)?PfontFromFont(1):nullptr;
        const GLYFF* cross=nullptr;
        if(joy) {
            auto it=joy->m_aglyff.find(static_cast<uint16_t>('X'));
            if(it!=joy->m_aglyff.end())cross=&it->second;
        }
        const bool controllerAtlas=joy && joy->m_pbmp==&source && source.bmpWidth==256 && source.bmpHeight==128 &&
            cross && cross->x==1 && cross->y==1 && cross->dx==32;
        CFontBrx* textFont=PfontFromFont(0);
        const bool textAtlas=textFont && textFont->m_pbmp==&source;
        if(controllerAtlas || textAtlas)atlas=true;
        unsigned width=8,height=8,maximum=atlas?1024:limit;
        while(width<source.bmpWidth && width<maximum)width*=2;
        while(height<source.bmpHeight && height<maximum)height*=2;
        // Decide alpha precision from the source, not whichever draw happens
        // to request this shared image first. BC3 explicitly carries alpha.
        if(source.textureFormat==2)smoothAlpha=true;
        else if(source.textureFormat==0 && !smoothAlpha && !textAtlas) {
            for(size_t a=3;a<source.diffuseTexture.size();a+=4) {
                const byte alpha=source.diffuseTexture[a];
                if(alpha!=0 && alpha!=255) {smoothAlpha=true;break;}
            }
        }
        GPU_TEXCOLOR fmt=textAtlas?GPU_LA8:smoothAlpha?GPU_RGBA4:GPU_RGBA5551;
        size_t bytes=size_t(width)*height*2;
        std::vector<uint8_t> compressed;
        if(!atlas && size_t(image)<preparedImages.size()) {
            for(auto& prepared:preparedImages[image]) {
                if(prepared.width==width && prepared.height==height && !prepared.data.empty()) {
                    compressed.swap(prepared.data);fmt=GPU_ETC1;
                    textureSavedBytes+=bytes-compressed.size();bytes=compressed.size();++etcTexCount;break;
                }
            }
        }
        if(size_t(image)<preparedImages.size())preparedImages[image]={};
        // Fonts/button sheets recur across worlds. Retain their converted,
        // swizzled bytes with a fixed cap, independent of world object pointers.
        uint64_t atlasKey=0;
        const std::vector<uint8_t>* sharedPixels=nullptr;
        const bool sharedEligible=(controllerAtlas || textAtlas) && bytes<=kSharedAtlasBudget;
        if(sharedEligible) {
            const uint64_t meta[]={uint64_t(source.bmpWidth),uint64_t(source.bmpHeight),uint64_t(source.textureFormat),uint64_t(width),uint64_t(height),uint64_t(fmt),uint64_t(textAtlas),4};
            atlasKey=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(meta),sizeof meta);
            atlasKey=cane_texture_etc::hashBytes(source.diffuseTexture.data(),source.diffuseTexture.size(),atlasKey);
            for(const auto& entry:sharedAtlases)if(entry.key==atlasKey && entry.pixels.size()==bytes) {sharedPixels=&entry.pixels;break;}
        }
        const bool useMipmaps=!atlas && !textAtlas && !controllerAtlas &&
            fmt!=GPU_ETC1 && (fmt==GPU_RGBA5551 || fmt==GPU_RGBA4) &&
            width>=16 && height>=16;
        const int maxLevel=useMipmaps?std::min(2,C3D_TexCalcMaxLevel(width,height)):0;
        const size_t totalBytes=(maxLevel>0)?size_t(C3D_TexCalcTotalSize(u32(bytes),maxLevel)):bytes;
        C3D_Tex tex{};
        bool inVram=false;
        if((!atlas || (textAtlas && vramTextureBudget(displayMode)>=2560u*1024u)) &&
           vramTextureBytes+totalBytes<=vramTextureBudget(displayMode) &&
           vramSpaceFree()>=totalBytes+vramTextureHeadroom(displayMode)) {
            C3D_TexInitParams params{u16(width),u16(height),u8(maxLevel),fmt,GPU_TEX_2D,true};
            inVram=C3D_TexInitWithParams(&tex,nullptr,params);
        }
        if(!inVram) {
            C3D_TexInitParams params{u16(width),u16(height),u8(maxLevel),fmt,GPU_TEX_2D,false};
            if(!C3D_TexInitWithParams(&tex,nullptr,params))throw std::runtime_error("Actor texture allocation failed");
        }
        auto& slotEntry=textures[image];slotEntry.tex=tex;slotEntry.loaded=true;slotEntry.inVram=inVram;
        uint16_t* out=nullptr;
        if(inVram) {
            if(totalBytes>texStageBytes) {
                if(texStage)linearFree(texStage);
                texStage=static_cast<uint16_t*>(linearAlloc(totalBytes));
                if(!texStage) {texStageBytes=0;throw std::runtime_error("Texture staging allocation failed");}
                texStageBytes=totalBytes;
            }
            out=texStage;
            ++vramTexCount;vramTextureBytes+=totalBytes;
        } else {
            out=static_cast<uint16_t*>(slotEntry.tex.data);
            ++fcramTexCount;fcramTextureBytes+=totalBytes;
        }
        // Extend ink colour through transparent padding so linear sampling
        // does not multiply edge coverage by a second dark colour fringe.
        unsigned glyphInk=255;
        if(!sharedPixels && textAtlas && source.textureFormat==0) {
            glyphInk=0;
            for(size_t i=0;i<source.diffuseTexture.size();i+=4)glyphInk=std::max(glyphInk,unsigned(source.diffuseTexture[i]));
        }

        if(sharedPixels) {
            std::memcpy(out,sharedPixels->data(),bytes);
            std::fprintf(stderr,"Shared atlas cache hit: %ux%u (%lu KiB retained)\n",width,height,(unsigned long)(sharedAtlasBytes/1024));
        }
        else if(!compressed.empty())std::memcpy(out,compressed.data(),compressed.size());
        else for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            unsigned sx=x*source.bmpWidth/width,sy=y*source.bmpHeight/height;
            auto p=pixel(source,sx,sy);
            unsigned index=((y/8)*(width/8)+x/8)*64+morton(x,y);
            // Preserve the full-resolution atlas and its alpha ramp.
            // Retain the lighter stroke edge without spatial averaging.
            unsigned textCoverage=p[3];
            if(textAtlas && textCoverage) {
                const unsigned edge=std::min({textCoverage,
                    unsigned(pixel(source,sx?sx-1:sx,sy)[3]),
                    unsigned(pixel(source,std::min(sx+1,unsigned(source.bmpWidth-1)),sy)[3]),
                    unsigned(pixel(source,sx,sy?sy-1:sy)[3]),
                    unsigned(pixel(source,sx,std::min(sy+1,unsigned(source.bmpHeight-1)))[3])});
                textCoverage=(textCoverage+3*edge+2)/4;
            }
            out[index]=textAtlas?uint16_t(((p[3]?unsigned(p[0]):glyphInk)<<8)|textCoverage):smoothAlpha?uint16_t((p[0]>>4)<<12|(p[1]>>4)<<8|(p[2]>>4)<<4|(p[3]>>4)):
                uint16_t((p[0]>>3)<<11|(p[1]>>3)<<6|(p[2]>>3)<<1|(p[3]>=64));
        }
        if(sharedEligible && !sharedPixels) {
            // Optional reuse must never prevent a level from loading.
            try {
                while(sharedAtlasBytes+bytes>kSharedAtlasBudget && !sharedAtlases.empty()) {
                    sharedAtlasBytes-=sharedAtlases.front().pixels.size();sharedAtlases.erase(sharedAtlases.begin());
                }
                const auto* begin=reinterpret_cast<const uint8_t*>(out);
                SharedAtlas entry{atlasKey,std::vector<uint8_t>(begin,begin+bytes)};
                sharedAtlases.push_back(std::move(entry));sharedAtlasBytes+=bytes;
            }catch(const std::bad_alloc&) {}
        }
        if(maxLevel>0)generateMipmaps16(out,width,height,maxLevel,fmt==GPU_RGBA4);
        if(textAtlas)std::fprintf(stderr,"Native text atlas: %ux%u LA8, full 8-bit glyph coverage\n",width,height);
        if(textAtlas)std::fprintf(stderr,"Native text readability: full-resolution point sampling, reduced stroke weight\n");
        // Text must not inherit the world's bilinear smoothing.
        C3D_TexSetFilter(&slotEntry.tex,textAtlas?GPU_NEAREST:GPU_LINEAR,
                        textAtlas?GPU_NEAREST:GPU_LINEAR);
        if(maxLevel>0)C3D_TexSetFilterMipmap(&slotEntry.tex,GPU_NEAREST);
        C3D_TexSetWrap(&slotEntry.tex,GPU_REPEAT,GPU_REPEAT);
        if(inVram) {
            GSPGPU_FlushDataCache(texStage,totalBytes);
            C3D_TexLoadImage(&slotEntry.tex,texStage,GPU_TEXFACE_2D,maxLevel>0?-1:0);
        } else {
            C3D_TexFlush(&slotEntry.tex);
        }
        if(controllerAtlas) {
            // Keep both GPU atlases. Switching the setting never requires
            // reloading game assets whose CPU pixels have already been freed.
            if(!C3D_TexInit(&newButtonTexture,256,128,GPU_RGBA4))throw std::runtime_error("New 3DS button texture allocation failed");
            auto* replacement=static_cast<uint16_t*>(newButtonTexture.data);
            for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
                auto p=pixel(source,x,y);
                uint16_t color=(p[0]>>4)<<12|(p[1]>>4)<<8|(p[2]>>4)<<4|(p[3]>>4);
                for(const auto& rect:caneNewButtonRects) {
                    if(x<rect.x || x>=rect.x+rect.width || y<rect.y || y>=rect.y+32)continue;
                    color=caneNewButtonPixels[rect.offset+(y-rect.y)*rect.width+x-rect.x];break;
                }
                replacement[((y/8)*32+x/8)*64+morton(x,y)]=color;
            }
            C3D_TexSetFilter(&newButtonTexture,GPU_LINEAR,GPU_LINEAR);
            C3D_TexSetWrap(&newButtonTexture,GPU_REPEAT,GPU_REPEAT);
            C3D_TexFlush(&newButtonTexture);buttonImage=image;
            std::fprintf(stderr,"Controller icon atlases ready: PS2 + New 3DS (256x128)\n");
        }
        // The GPU cache now owns the native copy for this world's lifetime.
        // Original draw callbacks do not need the retained desktop upload bytes.
        std::vector<byte>().swap(source.diffuseTexture);
        return selectedTexture(image,&slotEntry.tex);
    }

    static bool hasActivePose(const CanePacket& packet) {
        if(packet.pglob->poseCount<=0)return false;
        const auto& agPoses=packet.palo->globset.agPoses;
        const size_t np=std::min(agPoses.size(),size_t(packet.pglob->poseCount));
        for(size_t i=0;i<np;++i)if(std::abs(agPoses[i])>1e-5f)return true;
        return false;
    }
    static bool animated(const CanePacket& packet,const SUBGLOB& sub,bool activePose) {
        return packet.liveLight || (sub.cibnd>0 && packet.palo->globset.cbnd>0) || activePose;
    }
    static bool animated(const CanePacket& packet,const SUBGLOB& sub) {
        return animated(packet,sub,hasActivePose(packet));
    }
    static bool isLiveLightOnly(const CanePacket& packet,const SUBGLOB& sub,bool activePose) {
        return packet.liveLight && !packet.tv && !(sub.cibnd>0 && packet.palo->globset.cbnd>0) && !activePose;
    }
    static void resolveThreeWayImages(const SUBGLOB& sub,int fallback,int& i0,int& i1,int& i2) {
        const auto& tex=sub.pshd->atex[0];int f=sub.uvSai?sub.uvSai->iframe:0;
        auto pick=[&](const std::vector<int32_t>& imgs,int def) {
            return imgs.empty()?def:imgs[std::clamp(f,0,int(imgs.size())-1)];
        };
        i0=pick(tex.ambientImageIndex,fallback);
        i1=pick(tex.diffuseImageIndex,fallback);
        i2=pick(tex.saturateImageIndex,fallback);
    }
    void resolveSubTextures(SubCache& sc,bool hasUvSai,C3D_Tex*& t0,C3D_Tex*& t1,C3D_Tex*& t2) {
        if(!sc.texValid)return;
        if(!hasUvSai && sc.tex0) {t0=sc.tex0;t1=sc.tex1;t2=sc.tex2;return;}
        if(hasUvSai && sc.threeWay) {
            t1=texture(sc.id1);
            t0=sc.id0==sc.id1?t1:texture(sc.id0,false,false,64);
            t2=sc.id2==sc.id1?t1:(sc.id2==sc.id0?t0:texture(sc.id2,false,false,64));
            return;
        }
        if(sc.texMode==0) {
            t0=texture(sc.id0);t1=nullptr;t2=nullptr;
        } else if(sc.texMode==1) {
            t1=texture(sc.id1);
            t0=sc.id0==sc.id1?t1:texture(sc.id0,false,false,64);
            t2=nullptr;
        } else {
            t1=texture(sc.id1);
            t0=sc.id0==sc.id1?t1:texture(sc.id0,false,false,64);
            t2=sc.id2==sc.id1?t1:(sc.id2==sc.id0?t0:texture(sc.id2,false,false,64));
        }
        sc.tex0=t0;sc.tex1=t1;sc.tex2=t2;
    }
    const IndexBuf& indexBufferFor(const SUBGLOB& sub,GLuint vao) {
        auto& buf=indexBuffers[&sub];
        unsigned count=unsigned(sub.indices.size()*3);
        if(buf.data && buf.source==sub.indices.data() && buf.count==count && buf.vao==vao)return buf;
        if(buf.data) {C3D_FrameSync();linearFree(buf.data);buf={};}
        size_t bytes=size_t(count)*sizeof(uint16_t);
        buf.data=static_cast<uint16_t*>(linearAlloc(bytes));
        if(!buf.data)throw std::runtime_error("Native index buffer allocation failed");
        const size_t nVerts=sub.vertices.size();
        uint16_t* dst=buf.data;
        for(const auto& tri:sub.indices) {
            *dst++=tri.v1<nVerts?tri.v1:0;
            *dst++=tri.v2<nVerts?tri.v2:0;
            *dst++=tri.v3<nVerts?tri.v3:0;
        }
        GSPGPU_FlushDataCache(buf.data,bytes);
        buf.count=count;buf.source=sub.indices.data();buf.vao=vao;buf.physOffset=physOffsetFor(buf.data);
        return buf;
    }
    u64 tkAnim=0,tkMoved=0;
    void shadeVertexDir(WorkerScratch& sc,Vertex& vertex,const SUBGLOB& sub,size_t index,glm::vec3 normal,bool skipNormalMat,const glm::mat3& normalMat,float baseAmbient,float baseMid,bool anyHighlight,bool sameAll,bool sameSatDiff,bool approxOneTex,uint8_t& maxR,uint8_t& maxB) const {
        const auto& input=sub.vertices[index];
        if(!skipNormalMat)normal=normalMat*normal;
        const float nlSq=glm::dot(normal,normal);
        normal=nlSq>1e-8f?normal*fastRsqrt(nlSq):glm::vec3(0);
        float ambient=baseAmbient,mid=baseMid;
        float base=(input.color.r+input.color.g+input.color.b)*(1.0f/3.0f);
        float wA=input.color.a;
        if(!anyHighlight) {
            for(const auto& pl:sc.meshLights) {
                float diffuse=glm::dot(normal,pl.dir);
                diffuse+=diffuse*diffuse*diffuse;
                const float a=diffuse*pl.ruShadow+pl.duShadow;
                if(a>0.0f)ambient+=a;
                const float m=diffuse*pl.ruMidtone+pl.duMidtone;
                if(m>0.0f)mid+=m;
            }
            mid=std::max(0.0f,std::min(mid,1.0f));
            float wR=std::max(std::min(1.0f-mid,ambient),0.0f)*base;
            float wG=mid*base;
            if(sameAll) {
                float s=std::min(1.0f,wR+wG);
                setVertexColor(vertex,s,s,s,wA);
            } else if(approxOneTex) {
                float s=std::min(1.0f,wR*0.6f+wG);
                setVertexColor(vertex,s,s,s,wA);
            } else {
                setVertexColor(vertex,wR,wG,0.0f,wA);
                if(vertex.color[0]>maxR)maxR=vertex.color[0];
            }
            return;
        }
        glm::vec3 highlight(0);
        for(const auto& pl:sc.meshLights) {
            float diffuse=glm::dot(normal,pl.dir);
            diffuse+=diffuse*diffuse*diffuse;
            const float a=diffuse*pl.ruShadow+pl.duShadow;
            if(a>0.0f)ambient+=a;
            const float m=diffuse*pl.ruMidtone+pl.duMidtone;
            if(m>0.0f)mid+=m;
            const float hl=diffuse*pl.ruHighlight+pl.duHighlight;
            if(hl>0.0f)highlight+=pl.rgbaColor*hl;
        }
        float remaining=1.0f-std::max({highlight.r,highlight.g,highlight.b});
        mid=std::max(0.0f,std::min(mid,remaining));
        float wR=std::max(std::min(remaining-mid,ambient),0.0f)*base;
        float wG=mid*base;
        highlight=glm::min(highlight,glm::vec3(1.0f))*base;
        float wB=(highlight.r+highlight.g+highlight.b)*(1.0f/3.0f);
        if(sameAll) {
            float s=std::min(1.0f,wR+wG+wB);
            setVertexColor(vertex,s,s,s,wA);
        } else if(approxOneTex) {
            float s=std::min(1.0f,wR*0.6f+wG+wB*1.25f);
            setVertexColor(vertex,s,s,s,wA);
        } else {
            if(sameSatDiff) {wG=std::min(1.0f,wG+wB);wB=0.0f;}
            setVertexColor(vertex,wR,wG,wB,wA);
            if(vertex.color[0]>maxR)maxR=vertex.color[0];
            if(vertex.color[2]>maxB)maxB=vertex.color[2];
        }
    }
    void shadeVertex(WorkerScratch& sc,Vertex& vertex,const SUBGLOB& sub,size_t index,const glm::vec3& world,glm::vec3 normal,bool alreadyWorldNorm,const glm::mat3& normalMat,float baseAmbient,float baseMid,bool sameAll,bool sameSatDiff,bool approxOneTex,uint8_t& maxR,uint8_t& maxB) const {
        const auto& input=sub.vertices[index];
        if(!alreadyWorldNorm)normal=normalMat*normal;
        const float nlSq=glm::dot(normal,normal);
        normal=nlSq>1e-8f?normal*fastRsqrt(nlSq):glm::vec3(0);
        float ambient=baseAmbient,mid=baseMid;glm::vec3 highlight(0);
        for(const auto& pl:sc.meshLights) {
            float attenuation=1,diffuse=0;
            if(pl.kind==LIGHTK_Direction) {
                diffuse=glm::dot(normal,pl.dir);
            } else if(pl.kind==LIGHTK_Position) {
                const glm::vec3 direction=pl.pos-world;
                const float distSq=glm::dot(direction,direction);
                if(distSq<1e-8f || distSq>pl.maxRSq)continue;
                const float invLen=fastRsqrt(distSq);
                diffuse=glm::dot(normal,direction)*invLen;
                attenuation=std::clamp(pl.fallOffX+pl.fallOffY*invLen,0.0f,1.0f);
            } else if(pl.kind==LIGHTK_Frustrum || pl.kind==LIGHTK_Spot) {
                const LIGHT* light=pl.light;
                glm::vec4 clip=light->frustum*glm::vec4(world,1);
                if(clip.w<=0 || std::abs(clip.x)>clip.w || std::abs(clip.y)>clip.w || std::abs(clip.z)>clip.w)continue;
                float invW=1.0f/clip.w;
                float x=std::abs(clip.x*invW),y=std::abs(clip.y*invW);
                glm::vec4 sample(x,y,x*x+y*y,clip.w);
                glm::vec4 falloff=glm::clamp(light->falloffScale*sample+light->falloffBias,glm::vec4(0),glm::vec4(1));
                attenuation=falloff.x*falloff.y*falloff.z*falloff.w;
                diffuse=glm::dot(normal,pl.dir);
            } else continue;
            diffuse+=diffuse*diffuse*diffuse;
            ambient+=std::max(diffuse*pl.ruShadow+pl.duShadow,0.0f)*attenuation;
            mid+=std::max(diffuse*pl.ruMidtone+pl.duMidtone,0.0f)*attenuation;
            const float hl=std::max(diffuse*pl.ruHighlight+pl.duHighlight,0.0f)*attenuation;
            if(hl>0.0f)highlight+=pl.rgbaColor*hl;
        }
        float remaining=1.0f-std::max({highlight.r,highlight.g,highlight.b});
        mid=std::max(0.0f,std::min(mid,remaining));
        float base=(input.color.r+input.color.g+input.color.b)*(1.0f/3.0f);
        float wR=std::max(std::min(remaining-mid,ambient),0.0f)*base;
        float wG=mid*base;
        highlight=glm::min(highlight,glm::vec3(1.0f))*base;
        float wB=(highlight.r+highlight.g+highlight.b)*(1.0f/3.0f);
        float wA=input.color.a;
        if(sameAll) {
            float s=std::min(1.0f,wR+wG+wB);
            setVertexColor(vertex,s,s,s,wA);
        } else if(approxOneTex) {
            float s=std::min(1.0f,wR*0.6f+wG+wB*1.25f);
            setVertexColor(vertex,s,s,s,wA);
        } else {
            if(sameSatDiff) {wG=std::min(1.0f,wG+wB);wB=0.0f;}
            setVertexColor(vertex,wR,wG,wB,wA);
            if(vertex.color[0]>maxR)maxR=vertex.color[0];
            if(vertex.color[2]>maxB)maxB=vertex.color[2];
        }
    }
    void flatten(WorkerScratch& sc,Vertex* out,const CanePacket& packet,const SUBGLOB& sub,bool skin,bool bakeWorld,bool lit,float modelMaxScale,const glm::mat3* precompBoneNorms=nullptr,size_t precompBoneCount=0,uint8_t* outTexMode=nullptr,int* outI0=nullptr,int* outI1=nullptr,int* outI2=nullptr,bool* outSoftAlpha=nullptr,bool forceOneTex=false) const {
        sc.meshLights.clear();
        const bool isThreeWay=sub.pshd && sub.pshd->shdk==SHDK_ThreeWay;
        const bool hasThreeWayTex=isThreeWay && !sub.pshd->atex.empty();
        const bool doLight=isThreeWay && lit;
        bool needWorld=bakeWorld;
        if(doLight) {
            if(packet.tv) {
                if(const LIGHT* light=packet.tv->plight) {
                    const PreLight pl=makePreLight(light);
                    sc.meshLights.push_back(pl);
                    if(pl.kind!=LIGHTK_Direction)needWorld=true;
                }
            } else {
                const glm::vec3 center=transformPoint(packet.model,sub.posCenter);
                const float radius=sub.sRadius*modelMaxScale;
                const size_t nLights=frameLights.size();
                for(size_t li=0;li<frameDirLightCount;++li) {
                    const PreLight& pl=frameLights[li];
                    if(packet.dynamic && pl.fExcludeDynamic)continue;
                    if(pl.fDynamic && !packet.dynamic && !packet.liveLight)continue;
                    sc.meshLights.push_back(pl);
                }
                for(size_t li=frameDirLightCount;li<nLights;++li) {
                    const PreLight& pl=frameLights[li];
                    if(packet.dynamic && pl.fExcludeDynamic)continue;
                    if(pl.fDynamic && !packet.dynamic && !packet.liveLight)continue;
                    const float reach=radius+pl.maxR;
                    if(std::abs(center.x-pl.pos.x)>reach || std::abs(center.y-pl.pos.y)>reach || std::abs(center.z-pl.pos.z)>reach)continue;
                    if(glm::distance2(center,pl.pos)>reach*reach)continue;
                    needWorld=true;
                    sc.meshLights.push_back(pl);
                }
            }
        }
        int i0=-1,i1=-1,i2=-1;uint8_t texMode=0;
        const int fallback=imageFor(sub);
        const bool staticTex=(sub.uvSai==nullptr);
        bool sameAll=false,sameSatDiff=false,approxOneTex=false;
        if(lit && hasThreeWayTex) {
            resolveThreeWayImages(sub,fallback,i0,i1,i2);
            sameAll=staticTex && (i0==i1) && (i2==i1);
            sameSatDiff=staticTex && !sameAll && (i2==i1);
            if(!sameAll && staticTex && forceOneTex) {
                approxOneTex=true;
                texMode=0;
            } else {
                texMode=sameAll?0:(sameSatDiff?1:2);
            }
        } else if(lit && isThreeWay) {
            i0=-1;sameAll=true;texMode=0;
        } else {
            i0=fallback;texMode=0;
        }
        const glm::mat3 normalMat(packet.model);
        const auto& glob=*packet.pglob;
        const auto& set=packet.palo->globset;
        const bool hasSkin=skin && sub.cibnd>0 && set.cbnd>0;
        const bool hasPoses=skin && glob.poseCount>0 && !set.agPoses.empty();
        sc.activePoses.clear();
        if(hasPoses) {
            const size_t np=std::min(set.agPoses.size(),size_t(glob.poseCount));
            for(size_t pose=0;pose<np;++pose) {
                float w=set.agPoses[pose];
                if(std::abs(w)>1e-5f)sc.activePoses.push_back({pose,w});
            }
        }
        const bool hasActivePoses=!sc.activePoses.empty();
        const size_t nBones=set.boneMatrices.size();
        const glm::mat3* boneNorms=nullptr;
        if(hasSkin && doLight) {
            if(precompBoneNorms && precompBoneCount==nBones) {
                boneNorms=precompBoneNorms;
            } else {
                sc.boneNormMats.resize(nBones);
                for(size_t b=0;b<nBones;++b)sc.boneNormMats[b]=normalMat*glm::mat3(set.boneMatrices[b]);
                boneNorms=sc.boneNormMats.data();
            }
        }
        uint8_t maxR=0,maxB=0;
        bool softAlpha=false;
        const float baseAmbient=doLight?g_psw->lsmDefault.uShadow:0.0f;
        const float baseMid=doLight?(g_psw->lsmDefault.uMidtone+sub.unSelfIllum*.000031f):0.0f;
        const size_t vCount=sub.vertices.size();
        if(!doLight && !hasSkin && !hasActivePoses) {
            if(bakeWorld) {
                for(size_t index=0;index<vCount;++index) {
                    const auto& input=sub.vertices[index];auto& vertex=out[index];
                    const glm::vec3 outPos=transformPoint(packet.model,input.pos);
                    vertex.position[0]=outPos.x;vertex.position[1]=outPos.y;vertex.position[2]=outPos.z;
                    vertex.uv[0]=input.uv.x;vertex.uv[1]=1.0f-input.uv.y;
                    setVertexColor(vertex,input.color.r,input.color.g,input.color.b,input.color.a);
                    if(vertex.color[3]>0 && vertex.color[3]<230)softAlpha=true;
                }
            } else {
                for(size_t index=0;index<vCount;++index) {
                    const auto& input=sub.vertices[index];auto& vertex=out[index];
                    vertex.position[0]=input.pos.x;vertex.position[1]=input.pos.y;vertex.position[2]=input.pos.z;
                    vertex.uv[0]=input.uv.x;vertex.uv[1]=1.0f-input.uv.y;
                    setVertexColor(vertex,input.color.r,input.color.g,input.color.b,input.color.a);
                    if(vertex.color[3]>0 && vertex.color[3]<230)softAlpha=true;
                }
            }
        } else {
            const bool noLights=doLight && sc.meshLights.empty();
            bool allDirectional=doLight && !noLights;
            bool anyHighlight=false;
            if(allDirectional) {
                for(const auto& pl:sc.meshLights) {
                    if(pl.kind!=LIGHTK_Direction) {allDirectional=false;break;}
                    if(pl.ruHighlight!=0.0f || pl.duHighlight!=0.0f)anyHighlight=true;
                }
            }
            bool useLocalDir=false;
            if(allDirectional && !hasSkin) {
                const float s0=glm::dot(normalMat[0],normalMat[0]);
                const float s1=glm::dot(normalMat[1],normalMat[1]);
                const float s2=glm::dot(normalMat[2],normalMat[2]);
                if(s0>1e-6f && std::abs(s0-s1)<=s0*1e-3f && std::abs(s0-s2)<=s0*1e-3f &&
                   std::abs(glm::dot(normalMat[0],normalMat[1]))<=s0*1e-3f &&
                   std::abs(glm::dot(normalMat[0],normalMat[2]))<=s0*1e-3f &&
                   std::abs(glm::dot(normalMat[1],normalMat[2]))<=s0*1e-3f) {
                    useLocalDir=true;
                    const glm::mat3 invRot=glm::transpose(normalMat);
                    for(auto& pl:sc.meshLights) {
                        glm::vec3 ld=invRot*pl.dir;
                        const float l2=glm::dot(ld,ld);
                        pl.dir=l2>1e-8f?ld*fastRsqrt(l2):glm::vec3(0);
                    }
                }
            }
            const float noLightMid=noLights?std::max(0.0f,std::min(baseMid,1.0f)):0.0f;
            const float noLightWR=noLights?std::max(std::min(1.0f-noLightMid,baseAmbient),0.0f):0.0f;
            const float noLightScalar=sameAll?(noLightWR+noLightMid):(noLightWR*0.6f+noLightMid);
            for(size_t index=0;index<vCount;++index) {
                const auto& input=sub.vertices[index];auto& vertex=out[index];
                glm::vec3 position=input.pos;
                glm::vec3 normal=input.normal;
                if(hasActivePoses) {
                    for(const auto& ap:sc.activePoses) {
                        size_t delta=(size_t(sub.baseVertex)+index)*size_t(glob.poseCount)+ap.index;
                        if(delta<glob.poseDpos.size())position+=glm::vec3(glob.poseDpos[delta])*ap.weight;
                    }
                }
                bool alreadyWorldNorm=false;
                if(hasSkin) {
                    glm::vec3 skPos(0),skNorm(0);float total=0;
                    for(unsigned b=0;b<4;++b) {
                        float w=input.boneWeights[b];
                        if(w!=0) {
                            size_t bone=input.boneIndices[b];
                            if(bone<nBones) {
                                const glm::mat4& bm=set.boneMatrices[bone];
                                if(w==1.0f && total==0.0f) {
                                    skPos=transformPoint(bm,position);
                                    if(doLight && !noLights)skNorm=boneNorms[bone]*normal;
                                    total=1.0f;break;
                                }
                                skPos+=transformPoint(bm,position)*w;
                                if(doLight && !noLights)skNorm+=boneNorms[bone]*normal*w;
                                total+=w;
                            }
                        }
                    }
                    if(total>0) {position=skPos;if(doLight && !noLights){normal=skNorm;alreadyWorldNorm=true;}}
                }
                glm::vec3 world(0);
                if(needWorld)world=transformPoint(packet.model,position);
                const glm::vec3& outPos=bakeWorld?world:position;
                vertex.position[0]=outPos.x;vertex.position[1]=outPos.y;vertex.position[2]=outPos.z;
                vertex.uv[0]=input.uv.x;vertex.uv[1]=1.0f-input.uv.y;
                if(noLights) {
                    const float base=(input.color.r+input.color.g+input.color.b)*(1.0f/3.0f);
                    if(sameAll || approxOneTex) {
                        const float s=std::min(1.0f,noLightScalar*base);
                        setVertexColor(vertex,s,s,s,input.color.a);
                    } else {
                        setVertexColor(vertex,noLightWR*base,noLightMid*base,0.0f,input.color.a);
                        if(vertex.color[0]>maxR)maxR=vertex.color[0];
                    }
                } else if(allDirectional) {
                    shadeVertexDir(sc,vertex,sub,index,normal,alreadyWorldNorm||useLocalDir,normalMat,baseAmbient,baseMid,anyHighlight,sameAll,sameSatDiff,approxOneTex,maxR,maxB);
                } else if(doLight) {
                    shadeVertex(sc,vertex,sub,index,world,normal,alreadyWorldNorm,normalMat,baseAmbient,baseMid,sameAll,sameSatDiff,approxOneTex,maxR,maxB);
                } else {
                    setVertexColor(vertex,input.color.r,input.color.g,input.color.b,input.color.a);
                }
                if(vertex.color[3]>0 && vertex.color[3]<230)softAlpha=true;
            }
        }
        if(lit && hasThreeWayTex && staticTex) {
            if(sameAll || approxOneTex) {
                texMode=0;i0=i1;i1=-1;i2=-1;
            } else if(maxB<8) {
                if(maxR<8 || i0==i1) {
                    for(size_t i=0;i<vCount;++i) {
                        int s=int(out[i].color[0])+int(out[i].color[1])+int(out[i].color[2]);
                        uint8_t u=uint8_t(s>255?255:s);
                        out[i].color[0]=u;out[i].color[1]=u;out[i].color[2]=u;
                    }
                    texMode=0;i0=i1;i1=-1;i2=-1;
                } else {
                    if(maxB>0)for(size_t i=0;i<vCount;++i) {
                        int s=int(out[i].color[1])+int(out[i].color[2]);
                        out[i].color[1]=uint8_t(s>255?255:s);
                        out[i].color[2]=0;
                    }
                    texMode=1;i2=-1;
                }
            } else {
                texMode=2;
            }
        }
        if(outTexMode)*outTexMode=texMode;
        if(outI0)*outI0=i0;if(outI1)*outI1=i1;if(outI2)*outI2=i2;
        if(outSoftAlpha)*outSoftAlpha=softAlpha;
    }
    void runCelJob(WorkerScratch& sc,uint32_t idx) {
        const auto& job=celJobs[idx];
        const auto& packet=*job.packet;
        const auto& glob=*packet.pglob;
        const auto& set=packet.palo->globset;
        const auto& cache=*job.cache;
        const glm::mat4& mvp=job.mvp;
        const glm::vec4& color=job.color;
        auto& out=celJobVerts[idx];
        out.clear();
        const bool hasCelSkin=set.cbnd>0 && !set.boneMatrices.empty();
        const size_t nB=hasCelSkin?set.boneMatrices.size():0;
        if(hasCelSkin) {
            sc.boneMvps.resize(nB);
            sc.boneReady.assign(nB,0);
        }
        sc.activePoses.clear();
        if(glob.poseCount>0 && !set.agPoses.empty()) {
            const size_t np=std::min(set.agPoses.size(),size_t(glob.poseCount));
            for(size_t pose=0;pose<np;++pose) {
                float w=set.agPoses[pose];
                if(std::abs(w)>1e-5f)sc.activePoses.push_back({pose,w});
            }
        }
        const bool hasActiveCelPoses=!sc.activePoses.empty();
        const size_t nUnique=cache.first.size();
        sc.celClip.resize(nUnique);
        sc.celNdc.resize(nUnique);
        sc.celValid.resize(nUnique);
        for(size_t unique=0;unique<nUnique;++unique) {
            size_t index=cache.first[unique];const auto& input=glob.edgePoints[index];glm::vec4 pos=input.position;
            if(hasActiveCelPoses) {
                for(const auto& ap:sc.activePoses) {
                    size_t delta=index*size_t(glob.poseCount)+ap.index;
                    if(delta<glob.edgePoseDpos.size())pos+=glm::vec4(glm::vec3(glob.edgePoseDpos[delta])*ap.weight,0);
                }
            }
            glm::vec4 c(0);float total=0;
            if(hasCelSkin) {
                const glm::vec4 lp(glm::vec3(pos),1);
                for(unsigned b=0;b<4;++b) {
                    const float w=input.boneWeights[b];
                    const uint32_t bone=input.boneIndices[b];
                    if(w!=0 && bone<nB) {
                        if(!sc.boneReady[bone]) {
                            sc.boneMvps[bone]=mvp*set.boneMatrices[bone];
                            sc.boneReady[bone]=1;
                        }
                        if(w==1.0f && total==0.0f) {c=sc.boneMvps[bone]*lp;total=1.0f;break;}
                        c+=sc.boneMvps[bone]*lp*w;total+=w;
                    }
                }
            }
            if(total==0)c=mvp*pos;
            sc.celClip[unique]=c;
            bool ok=std::isfinite(c.w) && c.w>.0001f;
            sc.celValid[unique]=ok?1:0;
            if(ok) {float invW=1.0f/c.w;sc.celNdc[unique]=glm::vec2(c.x*invW,c.y*invW);}
        }
        const float minLen=std::max(.001f,job.minLinePixels);
        const float minLenSq=minLen*minLen;
        const uint8_t lcol[4]={packByte(color.r),packByte(color.g),packByte(color.b),packByte(color.a)};
        static constexpr unsigned endpoints[6]={0,1,0,0,1,1};
        static constexpr float sides[6]={-1,-1,1,1,-1,1};
        for(size_t edge=0;edge+3<glob.edgePoints.size() && out.size()<outlineBudget*6;edge+=4) {
            const unsigned u0=cache.indices[edge+0],u1=cache.indices[edge+1],u2=cache.indices[edge+2],u3=cache.indices[edge+3];
            if(!(sc.celValid[u0] && sc.celValid[u1] && sc.celValid[u2] && sc.celValid[u3]))continue;
            const glm::vec2& a=sc.celNdc[u0],&b=sc.celNdc[u1],&c=sc.celNdc[u2],&d=sc.celNdc[u3];
            auto cross=[](glm::vec2 x,glm::vec2 y){return x.x*y.y-x.y*y.x;};
            if(cross(b-a,c-b)*cross(d-b,b-a)>0)continue;
            if((a.x<-1.02f && b.x<-1.02f)||(a.x>1.02f && b.x>1.02f)||(a.y<-1.02f && b.y<-1.02f)||(a.y>1.02f && b.y>1.02f))continue;
            const glm::vec2 delta=(b-a)*glm::vec2(120,200);
            const float lenSq=glm::dot(delta,delta);
            if(lenSq<minLenSq)continue;
            const float invLen=fastRsqrt(lenSq);
            const glm::vec2 offset=glm::vec2(-delta.y,delta.x)*invLen*1.03125f/glm::vec2(120,200);
            const glm::vec4& pa=sc.celClip[u0],&pb=sc.celClip[u1];
            for(unsigned i=0;i<6;++i) {
                glm::vec4 expanded=(endpoints[i]?pb:pa);
                expanded.x+=offset.x*expanded.w*sides[i];expanded.y+=offset.y*expanded.w*sides[i];
                expanded.z=std::clamp(expanded.z+.00001f*expanded.w,-expanded.w+.000001f,-.000001f);
                const float invW=1.0f/expanded.w;
                Vertex v;
                v.position[0]=expanded.x*invW;v.position[1]=expanded.y*invW;v.position[2]=expanded.z*invW;
                std::memcpy(v.color,lcol,4);
                v.uv[0]=0.0f;v.uv[1]=0.0f;
                out.push_back(v);
            }
        }
    }
    void runAnimJob(WorkerScratch& sc,uint32_t idx) {
        auto& job=animJobs[idx];
        const auto& packet=canePackets[job.packetIdx];
        const auto& sub=packet.pglob->asubglob[job.subIdx];
        const glm::mat3* precomp=job.boneNormCount?(&allBoneNormMats[job.boneNormOffset]):nullptr;
        uint8_t texMode=0;bool softAlpha=false;int i0=-1,i1=-1,i2=-1;
        Vertex* dst=job.dstOverride?job.dstOverride:(dynamic[slot]+job.dynOffset);
        flatten(sc,dst,packet,sub,true,false,true,job.scale,precomp,job.boneNormCount,&texMode,&i0,&i1,&i2,&softAlpha,job.forceOneTex);
        job.texMode=texMode;job.hasSoftAlpha=softAlpha;job.i0=int16_t(i0);job.i1=int16_t(i1);job.i2=int16_t(i2);
    }
    void runHudJob(uint32_t idx) const {
        const auto& job=hudJobs[idx];
        Vertex* dst=dynamic[slot]+job.dynOffset;
        const auto& src=job.hud->vertices;
        for(size_t i=0;i<src.size();++i) {
            const auto& input=src[i];auto& vertex=dst[i];
            std::memcpy(vertex.position,&input.position,sizeof vertex.position);
            if(job.hud->layer!=CaneHudLayer::Top) {
                vertex.position[0]=vertex.position[0]*job.hud->scale+job.hud->dx;
                vertex.position[1]=vertex.position[1]*job.hud->scale+job.hud->dy;
            }
            vertex.uv[0]=input.uv.x;vertex.uv[1]=1.0f-input.uv.y;
            setVertexColor(vertex,input.color.r,input.color.g,input.color.b,input.color.a);
        }
    }
    void runCasterJob(WorkerScratch& sc,uint32_t idx) const {
        const auto& job=casterJobs[idx];
        const auto& caster=caneCasters[job.mapIndex];
        CanePacket packet{job.glob,caster.owner,caster.model,caster.alpha,RP_DynamicTexture,0};
        flatten(sc,dynamic[slot]+job.dynOffset,packet,*job.sub,animated(packet,*job.sub),false,false,job.scale);
    }
    void runParticleJob(uint32_t idx) {
        const auto& job=particleJobs[idx];
        auto& outVerts=particleJobVerts[idx];
        auto& outDraws=particleJobDraws[idx];
        outVerts.clear();
        outDraws.clear();
        if(!job.maxBlips)return;
        const auto& packet=caneParticles[job.packetIdx];
        const auto& group=*packet.group;
        if(!group.pshd || group.pshd->atex.empty())return;
        const auto& tex=group.pshd->atex[0];
        const auto& images=group.pshd->shdk==SHDK_ThreeWay?tex.ambientImageIndex:tex.diffuseImageIndex;
        const int table=std::clamp(group.cqwTexture,0,32);
        if(!table || images.empty())return;
        const size_t wantVerts=size_t(job.maxBlips)*6u;
        if(outVerts.capacity()<wantVerts)outVerts.reserve(wantVerts);
        const auto curve=[](const CLQ& q,float t) {return q.g0+t*(q.g1+t*q.g2);};
        const glm::mat3 modelRot(packet.model);
        const bool isIdentity=(packet.model[0][0]==1.0f && packet.model[1][1]==1.0f && packet.model[2][2]==1.0f &&
                               packet.model[3][0]==0.0f && packet.model[3][1]==0.0f && packet.model[3][2]==0.0f &&
                               packet.model[0][1]==0.0f && packet.model[0][2]==0.0f &&
                               packet.model[1][0]==0.0f && packet.model[1][2]==0.0f &&
                               packet.model[2][0]==0.0f && packet.model[2][1]==0.0f);
        const bool additive=(group.pshd->grfshd&1)!=0;
        const int colors=std::clamp(group.crgba,0,32);
        const glm::vec3 right=frameCamRight,up=frameCamUp;
        unsigned emitted=0,links=0;
        for(BLIP* blip=group.dlBlip.pblipFirst;blip && links++<1024 && emitted<job.maxBlips;blip=blip->dle.pblipNext) {
            const auto& data=blip->ablipf[blip->iblipfLatest&1];
            const int cnt=std::clamp(blip->cblipe,0,44);
            for(int i=0;i<cnt && emitted<job.maxBlips;++i) {
                const auto& pos=data.ablipp[i];const auto& extra=data.ablipx[i];
                const float age=g_clock.t-pos.tCreated,scale=curve(group.clqScale,age);
                const float alpha=std::clamp(curve(group.clqAlpha,age),0.0f,1.0f)*packet.alpha;
                const float frameValue=curve(group.clqTexture,age);
                if(!std::isfinite(scale)||!std::isfinite(frameValue)||alpha<=.001f)continue;
                const int entry=((int(extra.itex0%table)+int(std::clamp(frameValue,-100000.0f,100000.0f)))%table+table)%table;
                C3D_Tex* pt=job.tableTex[entry];
                if(!pt)continue;
                glm::vec4 color(1);
                if(colors) {
                    int c=std::min(unsigned(colors-1),unsigned(extra.irgba));color=group.argba[c];
                    if(group.fColorRanges && c+1<colors)color=glm::mix(color,group.argba[c+1],std::clamp(curve(group.clqColor,age),0.0f,1.0f));
                }
                color.a*=alpha;
                const uint8_t pcol[4]={packByte(color.r),packByte(color.g),packByte(color.b),packByte(color.a)};
                glm::vec3 center=isIdentity?glm::vec3(pos.x,pos.y,pos.z):transformPoint(packet.model,glm::vec3(pos.x,pos.y,pos.z));
                glm::vec3 horizontal=right*scale,vertical=up*scale;
                if(group.blipok==BLIPOK_Rolling) {
                    float c=std::cos(extra.radRoll),s=std::sin(extra.radRoll);
                    horizontal=(right*c+up*s)*scale;vertical=(-right*s+up*c)*scale;
                } else if(group.blipok==BLIPOK_Flying) {
                    const auto& v=data.ablipv[i];
                    glm::vec3 line=modelRot*glm::vec3(v.x,v.y,v.z)*(group.rSFlying*g_clock.dt);
                    glm::vec2 screen(glm::dot(line,right),glm::dot(line,up));float length=glm::length(screen);
                    if(length>.0001f) {
                        const float invLen=1.0f/length;
                        horizontal=(right*(-screen.y*invLen)+up*(screen.x*invLen))*std::abs(scale);
                        if(length<2*std::abs(scale))line+=(right*screen.x+up*screen.y)*((2*std::abs(scale)-length)*invLen);
                    } else line=up*(2*std::abs(scale));
                    center+=line*.5f;vertical=line*.5f;
                }
                const size_t base=outVerts.size();
                if(outDraws.empty() || outDraws.back().tex!=pt || outDraws.back().additive!=additive)
                    outDraws.push_back({unsigned(base),0,pt,additive,0});
                if(pcol[3]>outDraws.back().maxAlpha)outDraws.back().maxAlpha=pcol[3];
                const glm::vec3 bl=center-horizontal-vertical;
                const glm::vec3 br=center+horizontal-vertical;
                const glm::vec3 tl=center-horizontal+vertical;
                const glm::vec3 tr=center+horizontal+vertical;
                outVerts.resize(base+6);
                Vertex* dst=outVerts.data()+base;
                auto writeV=[&](Vertex& vx,const glm::vec3& pt3,float u,float v) {
                    vx.position[0]=pt3.x;vx.position[1]=pt3.y;vx.position[2]=pt3.z;
                    std::memcpy(vx.color,pcol,4);
                    vx.uv[0]=u;vx.uv[1]=v;
                };
                writeV(dst[0],bl,0.0f,1.0f);
                writeV(dst[1],br,1.0f,1.0f);
                writeV(dst[2],tl,0.0f,0.0f);
                dst[3]=dst[2];
                dst[4]=dst[1];
                writeV(dst[5],tr,1.0f,0.0f);
                outDraws.back().count+=6;++emitted;
            }
        }
    }
    void runShadowJob(WorkerScratch& sc,uint32_t idx) {
        const auto& rec=shadowReceivers[idx];
        const auto& info=shadowMapInfo[rec.mapIndex];
        const SHADOW* shadow=info.shadow;
        auto& out=shadowJobOut[idx];
        out.clear();
        const auto& packet=*rec.packet;const auto& sub=*rec.sub;
        const size_t nVerts=sub.vertices.size();
        if(!nVerts)return;
        const glm::mat4 uvMat=rec.worldBaked?shadow->matWorldToUv:(shadow->matWorldToUv*packet.model);
        const glm::mat3 modelRot(packet.model);
        const glm::vec3 localNegCast=glm::transpose(modelRot)*info.negCastDir;
        const float c0=glm::dot(modelRot[0],modelRot[0]),c1=glm::dot(modelRot[1],modelRot[1]),c2=glm::dot(modelRot[2],modelRot[2]);
        const bool uniformRot=c0>1e-6f &&
            std::abs(c0-c1)<1e-3f*c0 && std::abs(c0-c2)<1e-3f*c0 &&
            std::abs(glm::dot(modelRot[0],modelRot[1]))<1e-3f*c0 &&
            std::abs(glm::dot(modelRot[0],modelRot[2]))<1e-3f*c0 &&
            std::abs(glm::dot(modelRot[1],modelRot[2]))<1e-3f*c0;
        sc.shadowUv.resize(nVerts);sc.shadowMask.resize(nVerts);
        uint8_t allMask=0x1Fu;
        for(size_t v=0;v<nVerts;++v) {
            const auto& vx=rec.vertices[v];
            const glm::vec4 uv=uvMat*glm::vec4(vx.position[0],vx.position[1],vx.position[2],1);
            sc.shadowUv[v]=uv;
            uint8_t m=0;
            if(uv.x<0)m|=1;
            if(uv.x>uv.w)m|=2;
            if(uv.y<0)m|=4;
            if(uv.y>uv.w)m|=8;
            if(uv.w<=.0001f)m|=16;
            sc.shadowMask[v]=m;
            allMask&=m;
        }
        if(allMask!=0)return;
        struct Point {glm::vec3 world;glm::vec4 uv;};
        auto distance=[](const Point& p,int plane) {
            switch(plane) {case 0:return p.uv.x;case 1:return p.uv.w-p.uv.x;case 2:return p.uv.y;case 3:return p.uv.w-p.uv.y;default:return p.uv.w-.0001f;}
        };
        auto worldPos=[&](unsigned vi) {
            const auto& vx=rec.vertices[vi];
            const glm::vec3 p(vx.position[0],vx.position[1],vx.position[2]);
            return rec.worldBaked?p:transformPoint(packet.model,p);
        };
        for(size_t t=0;t<sub.indices.size() && out.size()+30<=12288;++t) {
            const auto& tri=sub.indices[t];
            const unsigned v0=tri.v1,v1=tri.v2,v2=tri.v3;
            if(v0>=nVerts || v1>=nVerts || v2>=nVerts)continue;
            const uint8_t m0=sc.shadowMask[v0],m1=sc.shadowMask[v1],m2=sc.shadowMask[v2];
            if(m0&m1&m2)continue;
            if(info.cl>.001f) {
                const glm::vec3& n=sub.vertices[v0].normal;
                const float d=glm::dot(n,localNegCast);
                if(d<0.0f) {
                    const float nlSq=uniformRot?(c0*glm::dot(n,n)):glm::dot(modelRot*n,modelRot*n);
                    if(nlSq>1e-6f && d*d>=0.25f*nlSq)continue;
                }
            }
            const glm::vec3 w0=worldPos(v0),w1=worldPos(v1),w2=worldPos(v2);
            Point polygon[12],temp[12];
            Point* clipped=polygon;
            int count=3;
            clipped[0]={w0,sc.shadowUv[v0]};
            clipped[1]={w1,sc.shadowUv[v1]};
            clipped[2]={w2,sc.shadowUv[v2]};
            const uint8_t anyClip=m0|m1|m2;
            if(anyClip!=0) {
                Point* dst=temp;
                for(int plane=0;plane<5 && count;++plane) {
                    if(!(anyClip&(1u<<plane)))continue;
                    int next=0;
                    for(int i=0;i<count;++i) {
                        const auto& a=clipped[i];const auto& b=clipped[(i+1)%count];float da=distance(a,plane),db=distance(b,plane);
                        if(da>=0)dst[next++]=a;
                        if((da>=0)!=(db>=0)) {float tClip=da/(da-db);dst[next++]={glm::mix(a.world,b.world,tClip),glm::mix(a.uv,b.uv,tClip)};}
                    }
                    count=next;std::swap(clipped,dst);
                }
                if(count<3)continue;
            }
            const glm::vec3 centroid=(w0+w1+w2)*(1.0f/3.0f);
            const glm::vec3 centroidToCam=info.camPos-centroid;
            const float centroidDistSq=glm::dot(centroidToCam,centroidToCam);
            const bool useTriangleBias=(centroidDistSq>=100.0f);
            const glm::vec3 triBias=useTriangleBias?(centroidToCam*(.03f*fastRsqrt(centroidDistSq))):glm::vec3(0);
            Vertex polyVerts[12];
            for(int vi=0;vi<count;++vi) {
                const auto& point=clipped[vi];
                Vertex& vertex=polyVerts[vi];
                glm::vec3 world=point.world;
                if(useTriangleBias) {
                    world+=triBias;
                } else {
                    const glm::vec3 towards=info.camPos-point.world;
                    const float lenSq=glm::dot(towards,towards);
                    if(lenSq>1e-6f)world+=towards*(.03f*fastRsqrt(lenSq));
                }
                std::memcpy(vertex.position,&world,sizeof vertex.position);
                vertex.color[0]=vertex.color[1]=vertex.color[2]=0;vertex.color[3]=info.alphaByte;
                const float invW=1.0f/point.uv.w;
                vertex.uv[0]=point.uv.x*invW;vertex.uv[1]=point.uv.y*invW;
            }
            for(int i=1;i+1<count;++i) {
                out.push_back(polyVerts[0]);
                out.push_back(polyVerts[i]);
                out.push_back(polyVerts[i+1]);
            }
        }
    }
    unsigned drainWave(WorkerScratch& sc) {
        const WaveMode mode=waveMode;
        const uint32_t total=waveTotalJobs;
        const uint32_t nPart=uint32_t(particleJobs.size());
        const uint32_t nAnim=uint32_t(animJobs.size());
        const uint32_t nCel=uint32_t(celJobs.size());
        const uint32_t nCaster=uint32_t(casterJobs.size());
        unsigned done=0;
        for(;;) {
            uint32_t id=__atomic_fetch_add(&waveNextJob,1u,__ATOMIC_RELAXED);
            if(id>=total)break;
            if(mode==WaveMode::Prep1) {
                // Schedule heaviest jobs (particle groups, animated skinning) first
                // so no large straggler starts at the tail of Wave 1.
                if(id<nPart)runParticleJob(id);
                else if(id<nPart+nAnim)runAnimJob(sc,id-nPart);
                else if(id<nPart+nAnim+nCel)runCelJob(sc,id-nPart-nAnim);
                else if(id<nPart+nAnim+nCel+nCaster)runCasterJob(sc,id-nPart-nAnim-nCel);
                else runHudJob(id-nPart-nAnim-nCel-nCaster);
            } else if(mode==WaveMode::ShadowClip) {
                runShadowJob(sc,id);
            }
            ++done;
        }
        return done;
    }
    static void workerEntry(void* arg) {
        auto* w=static_cast<Worker*>(arg);
        auto* g=w->owner;
        for(;;) {
            LightEvent_Wait(&w->kick);
            if(__atomic_load_n(&g->poolStop,__ATOMIC_ACQUIRE))break;
            u64 t0=svcGetSystemTick();
            unsigned done=g->drainWave(w->scratch);
            w->busyTicks+=svcGetSystemTick()-t0;
            w->jobsDone+=done;
            __dmb();
            LightEvent_Signal(&w->done);
        }
    }
    void initWorkers() {
        mainScratch.reserveDefault();
        bool isNew3ds=false;
        APT_CheckNew3DS(&isNew3ds);
        Result limitRes=APT_SetAppCpuTimeLimit(isNew3ds?80:30);
        if(isNew3ds && R_FAILED(limitRes))limitRes=APT_SetAppCpuTimeLimit(50);
        if(isNew3ds && R_FAILED(limitRes))limitRes=APT_SetAppCpuTimeLimit(30);
        u32 actualLimit=0;
        APT_GetAppCpuTimeLimit(&actualLimit);
        s32 mainPrio=0x30;
        svcGetThreadPriority(&mainPrio,CUR_THREAD_HANDLE);
        // Keep render workers strictly lower priority than audio/DSP threads
        // (OpenDSP workers run at mainPrio - 1) and the main thread so audio
        // never stutters under heavy render load.
        s32 workerPrio=std::clamp<s32>(mainPrio+2,0x18,0x3F);
        const int candidates[2]={isNew3ds?2:1,isNew3ds?1:-1};
        workerCount=0;
        for(int c=0;c<2;++c) {
            int coreId=candidates[c];
            if(coreId<0)continue;
            Worker& w=workers[workerCount];
            w.owner=this;w.coreId=coreId;w.index=workerCount;w.jobsDone=0;w.busyTicks=0;
            w.scratch.reserveDefault();
            LightEvent_Init(&w.kick,RESET_ONESHOT);
            LightEvent_Init(&w.done,RESET_ONESHOT);
            w.handle=threadCreate(workerEntry,&w,64*1024,workerPrio,coreId,false);
            if(w.handle)++workerCount;
        }
        std::fprintf(stderr,"Native worker pool: n3ds=%d syscore_limit=%u (res=%08lx) workers=%u cores=[%d,%d]\n",
            int(isNew3ds),unsigned(actualLimit),(unsigned long)limitRes,workerCount,
            workerCount>0?workers[0].coreId:-1,workerCount>1?workers[1].coreId:-1);
    }
    void shutdownWorkers() {
        if(!workerCount)return;
        __atomic_store_n(&poolStop,1u,__ATOMIC_RELEASE);
        __dmb();
        for(unsigned i=0;i<workerCount;++i)LightEvent_Signal(&workers[i].kick);
        for(unsigned i=0;i<workerCount;++i) {
            if(workers[i].handle) {
                threadJoin(workers[i].handle,U64_MAX);
                threadFree(workers[i].handle);
                workers[i].handle=nullptr;
            }
        }
        workerCount=0;poolStop=0;
    }
    bool kickWave(WaveMode mode,uint32_t totalJobs) {
        waveMode=mode;waveTotalJobs=totalJobs;
        __atomic_store_n(&waveNextJob,0u,__ATOMIC_RELEASE);
        __dmb();
        if(!workerCount || !totalJobs)return false;
        for(unsigned i=0;i<workerCount;++i)LightEvent_Signal(&workers[i].kick);
        return true;
    }
    void waitWave() {
        for(unsigned i=0;i<workerCount;++i)LightEvent_Wait(&workers[i].done);
        __dmb();
    }
    void reserveDynamic(size_t count) {
        if(count<=dynamicCapacity)return;
        if(count>200000)throw std::runtime_error("Native animated vertex budget exceeded");
        count=std::max(count,size_t(16384));Vertex* next[2]{};
        C3D_FrameSync();for(auto& pointer:dynamic) {if(pointer)linearFree(pointer);pointer=nullptr;}dynamicCapacity=0;dynamicBufOffset[0]=dynamicBufOffset[1]=0;
        for(auto& pointer:next) {
            pointer=static_cast<Vertex*>(linearAlloc(count*sizeof(Vertex)));
            if(!pointer) {for(auto* memory:next)if(memory)linearFree(memory);throw std::runtime_error("Native animated buffer allocation failed");}
        }
        for(unsigned i=0;i<2;++i) {dynamic[i]=next[i];dynamicBufOffset[i]=bufOffsetFor(dynamic[i]);}
        dynamicCapacity=count;
    }
    void reserveBatchIndices(size_t count) {
        if(count<=batchIndexCapacity)return;
        if(count>600000)throw std::runtime_error("Native batch index budget exceeded");
        count=std::max(count,size_t(131072));uint16_t* next[2]{};
        C3D_FrameSync();for(auto& pointer:batchIndices) {if(pointer)linearFree(pointer);pointer=nullptr;}batchIndexCapacity=0;batchIndicesPhys[0]=batchIndicesPhys[1]=~0u;
        for(auto& pointer:next) {
            pointer=static_cast<uint16_t*>(linearAlloc(count*sizeof(uint16_t)));
            if(!pointer) {for(auto* memory:next)if(memory)linearFree(memory);throw std::runtime_error("Native batch index allocation failed");}
        }
        for(unsigned i=0;i<2;++i) {batchIndices[i]=next[i];batchIndicesPhys[i]=physOffsetFor(batchIndices[i]);}
        batchIndexCapacity=count;
    }
    bool allocWorldChunk(size_t vertCount,int& outChunk,unsigned& outBase) {
        if(vertCount>kWorldChunkVerts)return false;
        for(size_t c=0;c<worldChunks.size();++c) {
            if(worldChunks[c].used+vertCount<=kWorldChunkVerts) {
                outChunk=int(c);outBase=worldChunks[c].used;worldChunks[c].used+=unsigned(vertCount);
                return true;
            }
        }
        if(worldChunks.size()>=kMaxWorldChunks)return false;
        size_t bytes=size_t(kWorldChunkVerts)*sizeof(Vertex);
        if(meshBytes+bytes>24*1024*1024)return false;
        Vertex* mem=static_cast<Vertex*>(linearAlloc(bytes));
        if(!mem)return false;
        meshBytes+=bytes;
        worldChunks.push_back({mem,unsigned(vertCount),~0u,0,bufOffsetFor(mem)});
        outChunk=int(worldChunks.size()-1);outBase=0;
        return true;
    }
    bool allocLocalChunk(size_t vertCount,int& outChunk,unsigned& outBase,unsigned& outCap) {
        if(!vertCount || vertCount>kLocalChunkVerts)return false;
        for(size_t i=0;i<localFreeList.size();++i) {
            auto& b=localFreeList[i];
            if(b.cap>=vertCount && b.cap<=vertCount*2u+64u) {
                outChunk=b.chunk;outBase=b.base;
                if(b.cap-vertCount>=64u) {
                    b.base+=unsigned(vertCount);b.cap-=unsigned(vertCount);outCap=unsigned(vertCount);
                } else {
                    outCap=b.cap;localFreeList[i]=localFreeList.back();localFreeList.pop_back();
                }
                return true;
            }
        }
        for(size_t c=0;c<localChunks.size();++c) {
            if(localChunks[c].used+vertCount<=kLocalChunkVerts) {
                outChunk=int(c);outBase=localChunks[c].used;outCap=unsigned(vertCount);
                localChunks[c].used+=unsigned(vertCount);
                return true;
            }
        }
        if(localChunks.size()>=kMaxLocalChunks)return false;
        size_t bytes=size_t(kLocalChunkVerts)*sizeof(Vertex);
        if(meshBytes+bytes>24*1024*1024)return false;
        Vertex* mem=static_cast<Vertex*>(linearAlloc(bytes));
        if(!mem)return false;
        meshBytes+=bytes;
        localChunks.push_back({mem,unsigned(vertCount),~0u,0,bufOffsetFor(mem)});
        outChunk=int(localChunks.size()-1);outBase=0;outCap=unsigned(vertCount);
        return true;
    }
    void freeLocalAllocation(Vertex*& verts,int& chunk,unsigned& base,unsigned& cap,size_t count,bool& owned) {
        if(owned && verts) {linearFree(verts);meshBytes-=count*sizeof(Vertex);}
        else if(chunk>=0 && cap>0) {localFreeList.push_back({chunk,base,cap});}
        verts=nullptr;chunk=-1;base=0;cap=0;owned=false;
    }
    uint16_t getBatchKey(int chunkIdx,uint8_t texMode,C3D_Tex* t0,C3D_Tex* t1,C3D_Tex* t2) {
        const BatchKeyDesc desc{chunkIdx,texMode,t0,t1,t2};
        if(uint16_t* found=batchKeyMap.find(desc))return *found;
        if(batchKeyTable.size()>=0xFFFEu)return 0xFFFFu;
        const uint16_t key=uint16_t(batchKeyTable.size());
        batchKeyTable.push_back(desc);
        batchKeyMap[desc]=key;
        bucketHead.push_back(~0u);
        return key;
    }
    void setTint(float r,float g,float b,float a) {
        if(tintValid && r==lastTintR && g==lastTintG && b==lastTintB && a==lastTintA)return;
        tintValid=true;lastTintR=r;lastTintG=g;lastTintB=b;lastTintA=a;
        constexpr float k=1.0f/255.0f;
        if(inFrameCmd && gpuCmdBuf && tint>=0) {
            const C3D_FVec v=FVec4_New(r*k,g*k,b*k,a*k);
            GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG,0x80000000u|u32(tint));
            GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA,reinterpret_cast<const u32*>(&v),4);
            C3D_FVUnifDirty[GPU_VERTEX_SHADER][tint]=false;
        } else {
            C3D_FVUnifSet(GPU_VERTEX_SHADER,tint,r*k,g*k,b*k,a*k);
        }
    }
    void setStereo(float z=0,float w=0,float limit=0) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER,stereo,z,w,limit,0);
    }
    void bindProgram(shaderProgram_s* p) {
        if(boundProgram!=p) {C3D_BindProgram(p);boundProgram=p;}
    }
    void bindSceneProgram(bool use3Tex,bool useStereo) {
        sceneUseStereo=useStereo;
        shaderProgram_s* p=use3Tex?(useStereo?&program:&movieProgram):(useStereo?&stereo1Program:&flat1Program);
        bindProgram(p);
    }
    void bindGameplayProgram() {
        bindSceneProgram(false,false);
    }
    void unlitState() {
        setStereo();
        if(filterOffset>=0)C3D_FVUnifSet(GPU_VERTEX_SHADER,filterOffset,0,0,0,0);
        if(filterOffsetNegative>=0)C3D_FVUnifSet(GPU_VERTEX_SHADER,filterOffsetNegative,0,0,0,0);
        // UI and movies must not inherit the world's extra alpha sampler,
        // material coefficients, stencil masks or additive blend operation.
        bindSceneProgram(false,false);
        C3D_TexBind(1,&white);C3D_TexBind(2,&white);
        for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
        auto* env=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
        C3D_StencilTest(false,GPU_ALWAYS,0,255,0);C3D_CullFace(GPU_CULL_NONE);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
        C3D_AlphaTest(true,GPU_GREATER,0);
    }
    static C3D_Mtx matrix(const glm::mat4& source) {
        C3D_Mtx result;for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)result.r[row].c[3-col]=source[col][row];return result;
    }
public:
    unsigned packetCount=0,triangleCount=0,hudVertexCount=0,drawCallCount=0;float cpuMs=0,drawCmdMs=0,frameMs=0,gpuMs=0,updateMs=0;
    void attachBottomConsole(PrintConsole* console,const uint32_t* genPtr=nullptr) {
        bottomConsole=console;
        bottomConsoleGen=genPtr;
        bottomFbStamps[0]=bottomFbStamps[1]={};
        if(!console || !console->frameBuffer)return;
        bottomConsoleShadow.assign(console->frameBuffer,console->frameBuffer+320*240);
        console->frameBuffer=bottomConsoleShadow.data();
        // Keep console writes in RAM, as in Conker's dual-screen renderer;
        // they must never overwrite a GPU-rendered bottom framebuffer.
        bottomTarget=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        caneBottomAvailable=bottomTarget!=nullptr;
        caneBottomActive=caneBottomAvailable && !CaneBottomMonitor3ds();
        std::fprintf(stderr,"Bottom HUD target: %s\n",caneBottomAvailable?"ready":"unavailable; using monitor");
    }
    template<class Progress> bool prepareTextureCache(const std::string& worldPath,Progress progress) {
        const u64 start=osGetTime();
        std::fprintf(stderr,"Preparing texture cache: %lu images\n",(unsigned long)g_abmp.size());
        // ISO-only installs have no generated bundle. Do not hash every pixel
        // merely to construct the name of a file that cannot exist.
        bool hasBundles=false;
        if(DIR* directory=opendir("sdmc:/3ds/ThieviusCTR/Cache/Textures")) {
            while(auto* entry=readdir(directory))if(std::strncmp(entry->d_name,"world-v1-",9)==0) {hasBundles=true;break;}
            closedir(directory);
        }
        preparedImages.clear();
        if(!hasBundles) {
            progress(unsigned(g_abmp.size()),unsigned(g_abmp.size()));
            std::fprintf(stderr,"Texture cache: no bundles; skipped pixel hashing (%llu ms)\n",(unsigned long long)(osGetTime()-start));
            return true;
        }
        // Remember the content fingerprint against the source file's identity.
        // A changed BRX or changed texture descriptors invalidates this index.
        struct stat sourceStat{};
        const bool canIndex=stat(worldPath.c_str(),&sourceStat)==0;
        uint64_t identity=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(worldPath.data()),worldPath.size());
        const uint64_t sourceMeta[]={uint64_t(sourceStat.st_size),uint64_t(sourceStat.st_mtime),uint64_t(sourceStat.st_ctime),uint64_t(g_abmp.size())};
        identity=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(sourceMeta),sizeof sourceMeta,identity);
        for(const auto& source:g_abmp) {
            const uint64_t meta[]={uint64_t(source.bmpWidth),uint64_t(source.bmpHeight),uint64_t(source.textureFormat),uint64_t(source.diffuseTexture.size())};
            identity=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(meta),sizeof meta,identity);
        }
        char indexPath[160];std::snprintf(indexPath,sizeof indexPath,"sdmc:/3ds/ThieviusCTR/Cache/Textures/lookup-v1-%016llx.bin",(unsigned long long)identity);
        uint64_t fingerprint=14695981039346656037ull,record[4]{};
        bool indexed=false;
        if(canIndex)if(FILE* input=std::fopen(indexPath,"rb")) {
            indexed=std::fread(record,1,sizeof record,input)==sizeof record && std::fgetc(input)==EOF &&
                record[0]==0x3158444943545255ull && record[1]==identity &&
                record[3]==cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(record),3*sizeof(uint64_t));
            std::fclose(input);
            if(indexed)fingerprint=record[2];
        }
        if(!indexed)for(const auto& source:g_abmp) {
            const uint64_t meta[]={uint64_t(source.bmpWidth),uint64_t(source.bmpHeight),uint64_t(source.textureFormat),uint64_t(source.diffuseTexture.size())};
            fingerprint=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(meta),sizeof meta,fingerprint);
            fingerprint=cane_texture_etc::hashBytes(source.diffuseTexture.data(),source.diffuseTexture.size(),fingerprint);
        }
        if(canIndex && !indexed) {
            record[0]=0x3158444943545255ull;record[1]=identity;record[2]=fingerprint;
            record[3]=cane_texture_etc::hashBytes(reinterpret_cast<const uint8_t*>(record),3*sizeof(uint64_t));
            char temp[176];std::snprintf(temp,sizeof temp,"%s.tmp",indexPath);
            if(FILE* output=std::fopen(temp,"wb")) {
                bool ok=std::fwrite(record,1,sizeof record,output)==sizeof record;
                ok=std::fclose(output)==0 && ok;
                if(!ok || std::rename(temp,indexPath)!=0)std::remove(temp);
            }
        }
        std::fprintf(stderr,"Texture cache lookup: %s (%llu ms)\n",indexed?"index hit":"pixel fingerprint",(unsigned long long)(osGetTime()-start));
        char bundle[160];std::snprintf(bundle,sizeof bundle,"sdmc:/3ds/ThieviusCTR/Cache/Textures/world-v1-%016llx.bin",(unsigned long long)fingerprint);
        preparedImages.clear();preparedImages.resize(g_abmp.size());
        if(cane_texture_etc::loadBundle(bundle,fingerprint,preparedImages)) {
            progress(unsigned(g_abmp.size()),unsigned(g_abmp.size()));
            std::fprintf(stderr,"Texture cache bundle loaded in %llu ms\n",(unsigned long long)(osGetTime()-start));return true;
        }
        // A missing cache is normal on an ISO-only installation. Encoding
        // every image here blocks startup and delays the pending cutscene.
        // Keep original textures on cache misses; never require a CPU texture
        // conversion pass before gameplay or movie playback can proceed.
        progress(unsigned(g_abmp.size()),unsigned(g_abmp.size()));
        std::fprintf(stderr,"Texture cache absent: using original textures without startup encoding\n");
        return true;
    }
    bool evictVramTexturesToBudget(size_t targetBudget) {
        for(auto& item:textures) {
            if(vramTextureBytes<=targetBudget)return true;
            if(!item.loaded || !item.inVram)continue;
            const size_t bytes=C3D_TexCalcTotalSize(item.tex.size,item.tex.maxLevel);
            C3D_Tex replacement=item.tex;
            replacement.data=linearAlloc(bytes);
            if(!replacement.data)return false;
            std::memcpy(replacement.data,item.tex.data,bytes);
            GSPGPU_FlushDataCache(replacement.data,bytes);
            C3D_TexDelete(&item.tex);item.tex=replacement;item.inVram=false;
            --vramTexCount;++fcramTexCount;vramTextureBytes-=bytes;fcramTextureBytes+=bytes;
        }
        return vramTextureBytes<=targetBudget;
    }
    void applyGameplayDisplay(int requested) {
        requested=std::clamp(requested,0,6);
        if(target && displayRequest==requested && gfxIs3D()==(displayMode==2 || displayMode==6) && gfxIsWide()==(displayMode==1))return;
        C3D_FrameSync();
        if(postTarget) {C3D_RenderTargetDelete(postTarget);postTarget=nullptr;}
        if(postTextureReady) {C3D_TexDelete(&postTexture);postTextureReady=false;}
        if(target) {C3D_RenderTargetDetachOutput(target);C3D_RenderTargetDelete(target);target=nullptr;}
        if(rightTarget) {C3D_RenderTargetDetachOutput(rightTarget);C3D_RenderTargetDelete(rightTarget);rightTarget=nullptr;}
        displayRequest=requested;displayMode=requested;
        u8 model=CFG_MODEL_3DS;
        if(R_SUCCEEDED(cfguInit())) {CFGU_GetSystemModel(&model);cfguExit();}
        if((requested==1 && !canUseWideMode3ds()) ||
           ((requested==2 || requested==6) && (model==CFG_MODEL_2DS || model==CFG_MODEL_N2DSXL)))displayMode=0;
        if(displayMode==6 || displayMode==3) {
            if(!evictVramTexturesToBudget(0))displayMode=(displayMode==6)?2:0;
            else for(auto& map:shadowMaps) {if(map.target)C3D_RenderTargetDelete(map.target);if(map.texture.data)C3D_TexDelete(&map.texture);map={};}
        }
        const size_t wantedVram=vramTextureBudget(displayMode);
        if(vramTextureBytes>wantedVram)evictVramTexturesToBudget(wantedVram);
        const auto allocatePostTarget=[&]() {
            if(displayMode<3 || displayMode>5)return true;
            const bool ssaa=displayMode==3;
            const int texW=ssaa?512:256,texH=ssaa?1024:512;
            if(!C3D_TexInitVRAM(&postTexture,texW,texH,GPU_RGB565))return false;
            postTextureReady=true;C3D_TexSetFilter(&postTexture,GPU_LINEAR,GPU_LINEAR);
            postTarget=C3D_RenderTargetCreateFromTex(&postTexture,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8);
            if(!postTarget) {C3D_TexDelete(&postTexture);postTextureReady=false;return false;}
            return true;
        };
        const auto allocate=[&]() {
            target=C3D_RenderTargetCreate(displayMode==6?480:240,displayMode==1?800:400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
            if(!target)return false;
            if(displayMode==2 || displayMode==6) {
                rightTarget=C3D_RenderTargetCreate(displayMode==6?480:240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
                if(!rightTarget) {C3D_RenderTargetDelete(target);target=nullptr;return false;}
            }
            if(!allocatePostTarget()) {
                if(rightTarget) {C3D_RenderTargetDelete(rightTarget);rightTarget=nullptr;}
                C3D_RenderTargetDelete(target);target=nullptr;return false;
            }
            return true;
        };
        if(!allocate()) {
            bool ready=false;
            if(displayMode==6) {
                displayMode=2;
                std::fprintf(stderr,"Display allocation fallback: requested=%d actual=%d\n",requested,displayMode);
                ready=allocate();
            }
            if(!ready) {
                displayMode=0;
                evictVramTexturesToBudget(0);
                for(auto& map:shadowMaps) {if(map.target)C3D_RenderTargetDelete(map.target);if(map.texture.data)C3D_TexDelete(&map.texture);map={};}
                std::fprintf(stderr,"Display allocation fallback: requested=%d actual=%d\n",requested,displayMode);
                if(!allocate())throw std::runtime_error("Native render target allocation failed");
            }
        }
        gfxSetWide(displayMode==1);if(displayMode!=1)gfxSet3D(displayMode==2 || displayMode==6);
        const u32 transfer=GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(displayMode==6?GX_TRANSFER_SCALE_X:GX_TRANSFER_SCALE_NO);
        C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,transfer);
        if(rightTarget)C3D_RenderTargetSetOutput(rightTarget,GFX_TOP,GFX_RIGHT,transfer);
        g_gl.renderWidth=displayMode==1?800:400;
        std::fprintf(stderr,"Gameplay display: requested=%d actual=%d (%dx%d, eyes=%d, post=%d)\n",requested,displayMode,g_gl.renderWidth,displayMode==6?480:240,(displayMode==2 || displayMode==6)?2:1,postTarget!=nullptr);
    }
    void init() {
        if(!C3D_Init(0x200000))throw std::runtime_error("Citro3D initialization failed");started=true;
        postQuad=static_cast<Vertex*>(linearAlloc(sizeof(Vertex)*6));
        if(!postQuad)throw std::runtime_error("Post-process quad allocation failed");
        postQuadBufOffset=bufOffsetFor(postQuad);
        shader=DVLB_ParseFile((u32*)cane_shader,cane_shader_end-cane_shader);
        if(!shader||!shader->numDVLE)throw std::runtime_error("Native shader unavailable");
        shaderProgramInit(&program);programStarted=true;shaderProgramSetVsh(&program,&shader->DVLE[0]);
        bindProgram(&program);projection=shaderInstanceGetUniformLocation(program.vertexShader,"projection");
        movieShader=DVLB_ParseFile((u32*)cane_movie_shader,cane_movie_shader_end-cane_movie_shader);
        if(!movieShader || !movieShader->numDVLE)throw std::runtime_error("Original 2D movie shader unavailable");
        shaderProgramInit(&movieProgram);movieProgramStarted=true;
        shaderProgramSetVsh(&movieProgram,&movieShader->DVLE[0]);
        movieProjection=shaderInstanceGetUniformLocation(movieProgram.vertexShader,"projection");
        movieTint=shaderInstanceGetUniformLocation(movieProgram.vertexShader,"tint");
        tint=shaderInstanceGetUniformLocation(program.vertexShader,"tint");
        if(projection!=movieProjection || tint!=movieTint)
            throw std::runtime_error("2D/stereo shader uniform layouts differ");
        stereo=shaderInstanceGetUniformLocation(program.vertexShader,"stereo");
        stereo1Shader=DVLB_ParseFile((u32*)cane_stereo1_shader,cane_stereo1_shader_end-cane_stereo1_shader);
        if(!stereo1Shader || !stereo1Shader->numDVLE)throw std::runtime_error("Native 1-tex stereo shader unavailable");
        shaderProgramInit(&stereo1Program);stereo1ProgramStarted=true;
        shaderProgramSetVsh(&stereo1Program,&stereo1Shader->DVLE[0]);
        if(shaderInstanceGetUniformLocation(stereo1Program.vertexShader,"projection")!=projection ||
           shaderInstanceGetUniformLocation(stereo1Program.vertexShader,"tint")!=tint ||
           shaderInstanceGetUniformLocation(stereo1Program.vertexShader,"stereo")!=stereo)
            throw std::runtime_error("1-tex/3-tex stereo shader uniform layouts differ");
        flat1Shader=DVLB_ParseFile((u32*)cane_flat1_shader,cane_flat1_shader_end-cane_flat1_shader);
        if(!flat1Shader || !flat1Shader->numDVLE)throw std::runtime_error("Native 1-tex flat shader unavailable");
        shaderProgramInit(&flat1Program);flat1ProgramStarted=true;
        shaderProgramSetVsh(&flat1Program,&flat1Shader->DVLE[0]);
        if(shaderInstanceGetUniformLocation(flat1Program.vertexShader,"projection")!=projection ||
           shaderInstanceGetUniformLocation(flat1Program.vertexShader,"tint")!=tint)
            throw std::runtime_error("1-tex flat shader uniform layouts differ");
        filterOffset=shaderInstanceGetUniformLocation(program.vertexShader,"filterOffset");
        filterOffsetNegative=shaderInstanceGetUniformLocation(program.vertexShader,"filterOffsetNegative");setStereo();
        applyGameplayDisplay(CaneDisplayMode3ds());
        if(!C3D_TexInit(&white,8,8,GPU_RGBA5551))throw std::runtime_error("Native fallback texture allocation failed");
        std::memset(white.data,255,128);C3D_TexFlush(&white);
        auto* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
        AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_UNSIGNED_BYTE,4);AttrInfo_AddLoader(attr,2,GPU_FLOAT,2);
        auto* env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
        C3D_CullFace(GPU_CULL_NONE);C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);C3D_AlphaTest(true,GPU_GREATER,24);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
        initWorkers();
    }
    static bool canUseWideMode3ds() {
        static int cached = -1;
        if (cached >= 0) return cached != 0;
        s64 emuInfo = 0;
        const Result emuRes = svcGetSystemInfo(&emuInfo, 0x20000, 0);
        if (R_SUCCEEDED(emuRes)) {
            std::fprintf(stderr, "Wide mode disabled on emulator (svc 0x20000 res=%08lx out=%lld): using 400x240\n",
                         (unsigned long)emuRes, (long long)emuInfo);
            cached = 0;
            return false;
        }
        u8 model = 0;
        bool wide = true;
        if (R_SUCCEEDED(cfguInit())) {
            if (R_SUCCEEDED(CFGU_GetSystemModel(&model)) && model == CFG_MODEL_2DS) {
                wide = false;
            }
            cfguExit();
        }
        cached = wide ? 1 : 0;
        return wide;
    }
    C3D_RenderTarget* acquireWideTarget(bool wantWide, bool& wideActive) {
        wideActive=wantWide && canUseWideMode3ds();
        if(!wideActive)return nullptr;
        C3D_FrameSync();
        C3D_RenderTargetDetachOutput(target);
        gfxSetWide(true);
        C3D_RenderTarget* wt=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,-1);
        if(wt) {
            C3D_RenderTargetSetOutput(wt,GFX_TOP,GFX_LEFT,GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
            return wt;
        }
        gfxSetWide(false);
        C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
        wideActive=false;
        return nullptr;
    }
    void releaseWideTarget(C3D_RenderTarget*& wt) {
        if(!wt)return;
        C3D_RenderTargetDetachOutput(wt);
        C3D_RenderTargetDelete(wt);
        wt=nullptr;
        gfxSetWide(false);
        C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
    }
    void drawMovieQuad(C3D_RenderTarget* drawTarget,bool isWideTarget,float displayW,const Vertex* vertices,C3D_Tex* tex,bool syncDraw=true) {
        C3D_Mtx ortho;Mtx_OrthoTilt(&ortho,0,displayW,240,0,0,1,true);ortho.r[2]=FVec4_New(0,0,0,-.5f);
        C3D_FrameBegin(syncDraw?C3D_FRAME_SYNCDRAW:0);C3D_RenderTargetClear(drawTarget,isWideTarget?C3D_CLEAR_COLOR:C3D_CLEAR_ALL,0x000000ff,0);C3D_FrameDrawOn(drawTarget);
        tintValid=false;
        unlitState();
        // Movies retain the exact pre-stereo shader: no 3D branch, disparity
        // uniforms or post-process sample offsets enter their GPU pipeline.
        bindProgram(&movieProgram);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,movieProjection,&ortho);
        setTint(255,255,255,255);
        C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
        auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,vertices,sizeof(Vertex),3,0x210);
        C3D_TexBind(0,tex);C3D_DrawArrays(GPU_TRIANGLES,0,6);C3D_FrameEnd(0);
        bindGameplayProgram();
    }
    struct FmvDrawState {
        CaneRuntimeGraphics* gfx=nullptr;
        C3D_RenderTarget* wideTarget=nullptr;
        C3D_RenderTarget* drawTarget=nullptr;
        float displayW=400.0f;
        C3D_Tex tex[2]{};
        bool texOk[2]{false,false};
        Vertex* vertices=nullptr;
        u32 lastPresentVblank=0;
        bool hasPresented=false;
    };
    static bool fmvBeginTarget(void* ctx,bool wantWide,unsigned texW,unsigned texH,
                               unsigned,unsigned,
                               float u0,float v0,float u1,float v1,
                               float quadW,float quadH,
                               C3D_Tex** outTex0,C3D_Tex** outTex1) {
        auto* s=static_cast<FmvDrawState*>(ctx);
        if(!C3D_TexInit(&s->tex[0],texW,texH,GPU_RGB565))return false;
        s->texOk[0]=true;C3D_TexSetFilter(&s->tex[0],GPU_LINEAR,GPU_LINEAR);
        if(C3D_TexInit(&s->tex[1],texW,texH,GPU_RGB565)) {
            s->texOk[1]=true;C3D_TexSetFilter(&s->tex[1],GPU_LINEAR,GPU_LINEAR);
        }
        s->vertices=static_cast<Vertex*>(linearAlloc(sizeof(Vertex)*6));
        if(!s->vertices) {fmvEndTarget(ctx);return false;}
        bool wideActive=false;
        s->wideTarget=s->gfx->acquireWideTarget(wantWide,wideActive);
        s->drawTarget=s->wideTarget?s->wideTarget:s->gfx->target;
        s->displayW=wideActive?800.0f:400.0f;
        const float actualW=wideActive?quadW:(quadW>400.0f?std::min(400.0f,quadW*0.5f):quadW);
        const float x0=(s->displayW-actualW)*0.5f,y0=(240.0f-quadH)*0.5f;
        const float xy[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
        for(unsigned i=0;i<6;++i) {
            auto& v=s->vertices[i];
            v.position[0]=x0+xy[i][0]*actualW;v.position[1]=y0+xy[i][1]*quadH;v.position[2]=0;
            for(uint8_t& c:v.color)c=255;
            v.uv[0]=u0+xy[i][0]*(u1-u0);v.uv[1]=v0+xy[i][1]*(v1-v0);
        }
        GSPGPU_FlushDataCache(s->vertices,sizeof(Vertex)*6);
        *outTex0=&s->tex[0];
        *outTex1=s->texOk[1]?&s->tex[1]:nullptr;
        return true;
    }
    static void fmvPresentFrame(void* ctx,C3D_Tex* tex) {
        auto* s=static_cast<FmvDrawState*>(ctx);
        const u32 curVblank=C3D_FrameCounter(0);
        const bool needSync=s->hasPresented && (curVblank==s->lastPresentVblank);
        s->gfx->drawMovieQuad(s->drawTarget,s->wideTarget!=nullptr,s->displayW,s->vertices,tex,needSync);
        s->lastPresentVblank=C3D_FrameCounter(0);
        s->hasPresented=true;
    }
    static void fmvEndTarget(void* ctx) {
        auto* s=static_cast<FmvDrawState*>(ctx);
        C3D_FrameSync();
        if(s->vertices) {linearFree(s->vertices);s->vertices=nullptr;}
        if(s->texOk[1]) {C3D_TexDelete(&s->tex[1]);s->texOk[1]=false;}
        if(s->texOk[0]) {C3D_TexDelete(&s->tex[0]);s->texOk[0]=false;}
        s->gfx->releaseWideTarget(s->wideTarget);
        C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    }
    bool movie(int index) {
        const CaneFmvResolved resolved=CaneResolveCutscene3ds("sdmc:/3ds/ThieviusCTR",index);
        if(resolved.kind==CaneFmvKind::None)return false;
        if(resolved.kind==CaneFmvKind::Bik || resolved.kind==CaneFmvKind::M2v) {
            FmvDrawState state{this};
            CaneFmvDrawCallbacks cb{};
            cb.beginTarget=fmvBeginTarget;
            cb.presentFrame=fmvPresentFrame;
            cb.endTarget=fmvEndTarget;
            cb.ctx=&state;
            return CanePlayDecodedFmv3ds(index,resolved,cb);
        }
        std::unique_ptr<FILE,decltype(&fclose)> file(fopen(resolved.videoPath.c_str(),"rb"),fclose);
        if(!file)return false;
        uint32_t header[6]{};
        if(fread(header,4,6,file.get())!=6 || !header[4])return false;
        const uint32_t width=header[1],height=header[2],fps=header[3],frameCount=header[4],frameBytes=header[5];
        const bool isCmv1=(header[0]==0x31564d43u && width==320 && height==240 && fps==15 && frameCount<=18000);
        const uint32_t expectedBytes=(width/8u)*(height/8u)*32u;
        const bool fpsValid=(fps>=1u && fps<=60u) || (fps>=1000u && fps<=60000u);
        const bool isCmv2=(header[0]==0x32564d43u &&
                           width>=64u && width<=800u && (width%8u)==0u &&
                           height>=64u && height<=240u && (height%8u)==0u &&
                           fpsValid && frameCount<=360000u && frameBytes==expectedBytes);
        if(!isCmv1 && !isCmv2)return false;
        std::vector<uint32_t> offsets;
        if(isCmv1) {
            offsets.resize(frameCount);
            if(fseek(file.get(),frameBytes,SEEK_SET) || fread(offsets.data(),4,offsets.size(),file.get())!=offsets.size())return false;
        }
        const unsigned texW=(isCmv2 && width>512u)?1024u:512u;
        const unsigned texH=256u;
        C3D_Tex movieTexture{};
        if(!C3D_TexInit(&movieTexture,texW,texH,isCmv2?GPU_ETC1:GPU_RGBA5551))return false;
        if(isCmv2)std::memset(movieTexture.data,0,size_t(texW/8u)*size_t(texH/8u)*32u);
        auto* vertices=static_cast<Vertex*>(linearAlloc(sizeof(Vertex)*6));
        if(!vertices) {C3D_TexDelete(&movieTexture);return false;}
        C3D_TexSetFilter(&movieTexture,GPU_LINEAR,GPU_LINEAR);
        bool wideActive=false;
        C3D_RenderTarget* wideTarget=acquireWideTarget(isCmv2 && width>400u,wideActive);
        C3D_RenderTarget* const drawTarget=wideTarget?wideTarget:target;
        const float displayW=wideActive?800.0f:400.0f;
        float quadW=float(width);
        const float quadH=float(height);
        if(wideActive) {
            if(width==640u && height<=224u)quadW=800.0f;
        } else if(width>400u) {
            quadW=(height<=224u)?400.0f:std::min(400.0f,float(width)*0.5f);
        }
        const float x0=(displayW-quadW)*0.5f,y0=(240.0f-quadH)*0.5f;
        const float xy[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
        for(unsigned i=0;i<6;++i) {
            auto& v=vertices[i];v.position[0]=x0+xy[i][0]*quadW;v.position[1]=y0+xy[i][1]*quadH;v.position[2]=0;
            for(uint8_t& c:v.color)c=255;
            v.uv[0]=xy[i][0]*float(width)/float(texW);v.uv[1]=1.0f-xy[i][1]*float(height)/float(texH);
        }
        GSPGPU_FlushDataCache(vertices,sizeof(Vertex)*6);
        const bool musicWasPaused=FMusicSequencerPaused();PauseMusicSequencer();
        SetFmvAudioMode(true);
        PreloadVag(resolved.wavPath.c_str());ContinueVag();
        bool vLoaded=false,vStream=false;unsigned vRate=0,vRead=0,vTotal=0;float vMix=0,vCh2=0;
        CaneVagStats3ds(&vLoaded,&vStream,&vRate,&vMix,&vCh2,&vRead,&vTotal);
        const uint64_t fpsMilli=fps>=1000u?uint64_t(fps):uint64_t(fps)*1000ull;
        std::fprintf(stderr,"Movie %02d started (%s %ux%u @ %.4gfps wide=%d): %lu frames audio=%d stream=%d rate=%u mix=%.2f ch2=%.2f\n",
            index,isCmv2?"CMV2":"CMV1",unsigned(width),unsigned(height),double(fpsMilli)/1000.0,int(wideActive),(unsigned long)frameCount,int(vLoaded),int(vStream),vRate,vMix,vCh2);
        const u64 start=osGetTime();int previous=-1;std::vector<uint16_t> packed;std::vector<uint8_t> etcFrame(isCmv2?frameBytes:0);
        const unsigned tilesY=height/8u;
        const size_t rowBytes=size_t(width/8u)*32u;
        const size_t texRowBytes=size_t(texW/8u)*32u;
        bool valid=true;bool release=false;
        while(aptMainLoop()) {
            CanePumpVagStream3ds();
            hidScanInput();u32 keys=hidKeysHeld();if(!keys)release=true;
            const u32 skipButton=CaneSwapBA3ds()?KEY_A:KEY_B;
            if(release && (keys&(KEY_START|skipButton)))break;
            const unsigned frame=unsigned((uint64_t(osGetTime()-start)*fpsMilli)/1000000ull);
            if(frame>=frameCount)break;
            if(int(frame)==previous) {gspWaitForVBlank();continue;}
            previous=frame;
            if(isCmv2) {
                if(fseeko(file.get(),off_t(24)+off_t(frame)*off_t(frameBytes),SEEK_SET) || fread(etcFrame.data(),1,frameBytes,file.get())!=frameBytes) {valid=false;break;}
                C3D_FrameSync();
                auto* out=static_cast<uint8_t*>(movieTexture.data);
                for(unsigned ty=0;ty<tilesY;++ty)std::memcpy(out+size_t(ty)*texRowBytes,etcFrame.data()+size_t(ty)*rowBytes,rowBytes);
            } else {
                uint32_t bytes=0;
                if(fseek(file.get(),offsets[frame],SEEK_SET) || fread(&bytes,4,1,file.get())!=1 || bytes>524288 || bytes%2) {valid=false;break;}
                packed.resize(bytes/2);
                if(fread(packed.data(),1,bytes,file.get())!=bytes) {valid=false;break;}
                C3D_FrameSync();auto* out=static_cast<uint16_t*>(movieTexture.data);size_t n=0,at=0;
                while(at<packed.size()) {
                    unsigned token=packed[at++],count=token&32767;
                    if(!count || n+count>131072) {valid=false;break;}
                    if(token&32768) {
                        if(at==packed.size()) {valid=false;break;}
                        std::fill(out+n,out+n+count,packed[at++]);
                    } else {
                        if(at+count>packed.size()) {valid=false;break;}
                        std::memcpy(out+n,packed.data()+at,count*2);at+=count;
                    }
                    n+=count;
                }
                if(!valid || n!=131072) {valid=false;break;}
            }
            C3D_TexFlush(&movieTexture);
            drawMovieQuad(drawTarget,wideTarget!=nullptr,displayW,vertices,&movieTexture);
        }
        CaneVagStats3ds(&vLoaded,&vStream,&vRate,&vMix,&vCh2,&vRead,&vTotal);
        StopVag();SetFmvAudioMode(false);if(!musicWasPaused)ContinueMusicSequencer(true);C3D_FrameSync();linearFree(vertices);C3D_TexDelete(&movieTexture);
        releaseWideTarget(wideTarget);
        C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
        std::fprintf(stderr,"Movie %02d ended valid=%d read=%u/%u\n",index,valid,vRead,vTotal);
        return valid;
    }
    unsigned draw() {
        if(!g_psw || !g_pcm)return 0;
        applyGameplayDisplay(CaneDisplayMode3ds());
        caneBottomActive=caneBottomAvailable && (!CaneBottomMonitor3ds() || CaneDialogueOnBottom3ds() || caneLoading);
        if(caneBottomActive!=bottomOutput) {
            C3D_FrameSync();
            if(caneBottomActive)C3D_RenderTargetSetOutput(bottomTarget,GFX_BOTTOM,GFX_LEFT,
                GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565));
            else C3D_RenderTargetDetachOutput(bottomTarget);
            bottomOutput=caneBottomActive;
            bottomFbStamps[0]=bottomFbStamps[1]={};
        }
        if(!bottomOutput && bottomConsole && !bottomConsoleShadow.empty()) {
            void* fb=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,nullptr,nullptr);
            const uint32_t gen=bottomConsoleGen?*bottomConsoleGen:0u;
            const bool upToDate=bottomConsoleGen &&
                ((bottomFbStamps[0].valid && bottomFbStamps[0].ptr==fb && bottomFbStamps[0].gen==gen) ||
                 (bottomFbStamps[1].valid && bottomFbStamps[1].ptr==fb && bottomFbStamps[1].gen==gen));
            if(!upToDate) {
                std::memcpy(fb,bottomConsoleShadow.data(),bottomConsoleShadow.size()*sizeof(u16));
                GSPGPU_FlushDataCache(fb,bottomConsoleShadow.size()*sizeof(u16));
                const int idx=(bottomFbStamps[0].valid && bottomFbStamps[0].ptr==fb)?0
                             :(bottomFbStamps[1].valid && bottomFbStamps[1].ptr==fb)?1
                             :(!bottomFbStamps[0].valid?0:1);
                bottomFbStamps[idx]={fb,gen,true};
            }
        }
        if(shadowSize!=CaneShadowSize3ds()) {
            C3D_FrameSync();
            for(auto& map:shadowMaps) {if(map.target)C3D_RenderTargetDelete(map.target);if(map.texture.data)C3D_TexDelete(&map.texture);map={};}
            shadowSize=CaneShadowSize3ds();
        }
        u64 beginTick=svcGetSystemTick();
        u64 begin=osGetTime();++frame;slot^=1;
        for(int i=0;i<RP_Max;++i) {phaseBuckets[i].clear();staticBatches[i].clear();mergedDraws[i].clear();}
        tvDraws.clear();fadedWorldDraws.clear();shadowReceivers.clear();
        for(auto& cv:casterDynVerts)cv.clear();
        shadowPasses.clear();casterSubs.clear();particleDraws.clear();hudDraws.clear();
        canePackets.clear();caneParticles.clear();caneCasters.clear();caneCelPackets.clear();caneTvContext=nullptr;
        CaneRecycleHudBuffers();SetupCm(g_pcm);
        if(caneBottomActive)CaneBottomBackgroundNative();
        frameLights.clear();
        caneDynamicLights.clear();
        for(LIGHT* light=g_psw->dlLight.plightFirst;light;light=light->dleLight.plightNext) {
            if(!FIsLoInWorld(light))continue;
            if(frameLights.size()<MAX_LIGHTS)frameLights.push_back(makePreLight(light));
            if(light->fDynamic)caneDynamicLights.push_back(light);
        }
        frameDirLightCount=size_t(std::partition(frameLights.begin(),frameLights.end(),
            [](const PreLight& pl){return pl.kind==LIGHTK_Direction;})-frameLights.begin());
        if(frame<=2)std::fprintf(stderr,"frame %u: render world\n",frame);
        if(frame<=2)std::fprintf(stderr,"heap used=%d free=%d\n",mallinfo().uordblks,mallinfo().fordblks);
        u64 t0=osGetTime();
        RenderSw(g_psw,g_pcm);
        u64 tSw=osGetTime();
        if(frame<=2)std::fprintf(stderr,"frame %u: render UI, packets=%lu\n",frame,(unsigned long)canePackets.size());
        u64 pk[8]{};pk[0]=svcGetSystemTick();
        RenderUi(&g_ui);
        pk[1]=svcGetSystemTick();
        if(frame<=2)std::fprintf(stderr,"frame %u: HUD text\n",frame);
        // Keep original counter formatting, icon glyphs, colours and layout.
        for(int kind=0;kind<BLOTK_Max;++kind) {
            if(kind==BLOTK_Autosave)continue;
            BLOT* blot=PblotFromBlotk(kind);
            // The original top-screen pop-ups keep their own timing and animation.
            if(blot && blot->blots!=BLOTS_Hidden && blot->pvtblot && blot->pvtblot->pfnDrawBlot)
                blot->pvtblot->pfnDrawBlot(blot);
            const bool persistent=caneBottomActive && !caneLoading &&
                ((kind==BLOTK_Coin && CaneBottomCoins3ds()) ||
                 (kind==(CaneBottomBottles3ds()?BLOTK_Clue:BLOTK_Lives) && CaneBottomHealth3ds())) &&
                // The same Paris world becomes playable after the title sequence.
                !caneStartupPresentation &&
                (g_ui.uisPlaying!=UIS_Attract && g_ui.uis!=UIS_Splash);
            if(persistent && blot && blot->pfont && blot->pvtblot && blot->pvtblot->pfnDrawBlot) {
                const float savedX=blot->x,savedY=blot->y,savedOn=blot->uOn;
                const BLOTS savedState=blot->blots;
                caneHudLayer=kind==BLOTK_Coin?CaneHudLayer::Coins:CaneHudLayer::Health;
                blot->x=blot->xOn;blot->y=blot->yOn;blot->uOn=1;blot->blots=BLOTS_Visible;
                if(kind==BLOTK_Clue && g_psw->cclueAll==0) {
                    // The retail pop-up is empty in bottle-free levels, but
                    // the explicitly enabled persistent counter stays visible.
                    CTextBox box;box.SetPos(0,0);box.SetSize(512,64);box.SetTextColor(&blot->rgba);
                    blot->pfont->PushScaling(1,1);blot->pfont->DrawPchz("0/0?",&box);blot->pfont->PopScaling();
                } else blot->pvtblot->pfnDrawBlot(blot);
                blot->x=savedX;blot->y=savedY;blot->uOn=savedOn;blot->blots=savedState;
            }
            caneHudLayer=CaneHudLayer::Top;canePortraitContext=nullptr;
        }
        caneCompassClip=false;caneRectClip=false;
        CaneWipeNative();
        if(g_autosave.pfont)DrawAutoSave(&g_autosave);
        if(caneBottomActive)CaneBottomStatusNative();
        trophies::Draw();
        CanePrepareBottomHud();
        pk[2]=svcGetSystemTick();
        C3D_Mtx projectionMatrix,view,combined;
        glm::vec3 eye=g_pcm->pos,forward=g_pcm->mat[0],cameraUp=g_pcm->mat[2];
        Mtx_LookAt(&view,FVec3_New(eye.x,eye.y,eye.z),FVec3_New(eye.x+forward.x,eye.y+forward.y,eye.z+forward.z),FVec3_New(cameraUp.x,cameraUp.y,cameraUp.z),false);
        Mtx_PerspTilt(&projectionMatrix,std::clamp(g_pcm->radFOV,.02f,2.9f),C3D_AspectRatioTop,std::max(.01f,g_pcm->sNearClip),std::max(g_pcm->sNearClip+1.0f,g_pcm->sFarClip),false);
        Mtx_Multiply(&combined,&projectionMatrix,&view);
        C3D_Mtx skyCombined=combined;
        for(int c=0;c<4;++c)skyCombined.r[2].c[c]=combined.r[3].c[c]*-5e-7f;
        glm::mat4 worldToClip;
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)worldToClip[col][row]=combined.r[row].c[3-col];
        const float projRowX=glm::length(glm::vec3(worldToClip[0][0],worldToClip[1][0],worldToClip[2][0]))*200.f;
        const float projRowY=glm::length(glm::vec3(worldToClip[0][1],worldToClip[1][1],worldToClip[2][1]))*120.f;
        const float projPixelScale=std::max(projRowX,projRowY);
        auto estimateScreenPx=[&](const glm::mat4& model,const glm::vec3& localCenter,float radius)->float {
            if(!(std::isfinite(radius) && radius>0.0f))return 9999.0f;
            const glm::vec3 wc=transformPoint(model,localCenter);
            const float clipW=worldToClip[0][3]*wc.x+worldToClip[1][3]*wc.y+worldToClip[2][3]*wc.z+worldToClip[3][3];
            if(!(std::isfinite(clipW) && clipW>radius))return 9999.0f;
            return 2.0f*radius*projPixelScale/(clipW-radius);
        };
        auto estimateScreenPxWorld=[&](const glm::vec3& wc,float radius)->float {
            if(!(std::isfinite(radius) && radius>0.0f))return 9999.0f;
            const float clipW=worldToClip[0][3]*wc.x+worldToClip[1][3]*wc.y+worldToClip[2][3]*wc.z+worldToClip[3][3];
            if(!(std::isfinite(clipW) && clipW>radius))return 9999.0f;
            return 2.0f*radius*projPixelScale/(clipW-radius);
        };
        // Upload visible UI atlases at their original size before a world
        // material can install a reduced-resolution copy under the same ID.
        for(const auto& hud:caneHud) {
            int image=imageForGlTex(hud.texture);
            if(image>=0)texture(image,true);
        }
        // Particle groups are captured separately from mesh packets.
        canePackets.erase(std::remove_if(canePackets.begin(),canePackets.end(),[](const CanePacket& packet) {
            return !packet.pglob || !packet.palo || (packet.rp==RP_BlotContext && !packet.tv) || packet.rp==RP_Blip;
        }),canePackets.end());
        if(frame<=2)std::fprintf(stderr,"frame %u: build meshes\n",frame);
        packetCount=canePackets.size();triangleCount=0;
        for(unsigned i=0;i<workerCount;++i) {workers[i].jobsDone=0;workers[i].busyTicks=0;}
        for(const auto& caster:caneCasters) {
            RebuildShadow(caster.owner->pshadowGen);
        }
        celJobs.clear();
        size_t celEdgeCap=0;
        unsigned outlineLodSkipped=0,outlineLodReduced=0;
        for(const auto& packet:caneCelPackets) {
            const auto& glob=*packet.pglob;
            glm::vec4 color=packet.rocel.celRgba;color.a*=std::clamp(packet.rocel.uAlphaCelBorder,0.0f,1.0f);
            if(color.a<=.001f)continue;
            glm::mat4 mvp=worldToClip*packet.rocel.model;
            // Use the game's model bounds; retain full detail near the camera,
            // for Sly, and whenever projection/bounds are uncertain.
            float minLinePixels=0;
            if(packet.palo!=g_pjt && g_ui.uis==UIS_Playing) {
                const glm::mat3 model(packet.rocel.model);
                const float scale=std::max({glm::length(model[0]),glm::length(model[1]),glm::length(model[2])});
                const float radius=glob.sRadius*scale;
                const glm::vec4 center=mvp*glm::vec4(glob.posCenter,1);
                if(std::isfinite(radius) && radius>0 && std::isfinite(center.w) && center.w>radius) {
                    const float pixels=2.f*radius*projPixelScale/(center.w-radius);
                    if(pixels<20.f) {++outlineLodSkipped;continue;}
                    if(pixels<64.f) {minLinePixels=1.25f;++outlineLodReduced;}
                }
            }
            uint32_t* celSlot=celCacheMap.find(packet.pglob);
            if(!celSlot) {
                const uint32_t newIdx=uint32_t(celCachePool.size());
                celCachePool.emplace_back();
                celCacheMap[packet.pglob]=newIdx;
                celSlot=celCacheMap.find(packet.pglob);
            }
            auto& cache=celCachePool[*celSlot];
            if(cache.source!=glob.edgePoints.data() || cache.count!=glob.edgePoints.size() || cache.vao!=glob.VAO) {
                cache={};cache.source=glob.edgePoints.data();cache.count=glob.edgePoints.size();cache.vao=glob.VAO;
                cache.indices.resize(cache.count);
                std::map<std::array<uint32_t,12>,unsigned> uniquePoints;
                for(size_t point=0;point<cache.count;++point) {
                    std::array<uint32_t,12> key;std::memcpy(key.data(),&glob.edgePoints[point],sizeof(CELPOINT));
                    unsigned unique=cache.first.size();
                    if(glob.poseCount<=0) {
                        auto result=uniquePoints.emplace(key,unique);unique=result.first->second;
                        if(result.second)cache.first.push_back(point);
                    } else cache.first.push_back(point);
                    cache.indices[point]=unique;
                }
            }
            celJobs.push_back({&packet,&cache,mvp,color,minLinePixels});
            celEdgeCap+=glob.edgePoints.size()/4;
        }
        if(celJobVerts.size()<celJobs.size())celJobVerts.resize(celJobs.size());
        animJobs.clear();allBoneNormMats.clear();
        hudDraws.clear();hudDraws.reserve(caneHud.size());hudJobs.clear();
        casterJobs.clear();particleJobs.clear();
        liveDecisions.clear();ownedLiveFlush.clear();
        size_t wave1Used=4;
        packetGcIdx.assign(canePackets.size(),~0u);
        uint8_t casterHasPacket[4]{};
        unsigned liveLightSkipped=0;
        for(size_t pi=0;pi<canePackets.size();++pi) {
            const auto& packet=canePackets[pi];
            const auto& glob=*packet.pglob;const auto& asubs=glob.asubglob;
            if(asubs.empty())continue;
            for(size_t m=0;m<caneCasters.size();++m) {
                if(static_cast<const ALO*>(caneCasters[m].owner)==packet.palo) {casterHasPacket[m]=1;break;}
            }
            const bool activePose=hasActivePose(packet);
            float cachedScale=-1.0f;
            int packetForceOneTex=-1;
            uint32_t packetBoneOff=0,packetBoneCount=0;bool packetBonesBuilt=false;
            bool anyStaticSub=false;
            for(size_t si=0;si<asubs.size();++si) {
                const auto& sub=asubs[si];
                size_t vertCount=sub.vertices.size(),idxCount=sub.indices.size()*3;
                if(!vertCount || !idxCount)continue;
                if(!animated(packet,sub,activePose)) {anyStaticSub=true;continue;}
                if(cachedScale<0.0f) {
                    const glm::mat3 r(packet.model);
                    cachedScale=std::max({glm::length(r[0]),glm::length(r[1]),glm::length(r[2])});
                }
                const auto& set=packet.palo->globset;
                const bool hasSkin=sub.cibnd>0 && set.cbnd>0 && !set.boneMatrices.empty();
                const bool doLight=sub.pshd && sub.pshd->shdk==SHDK_ThreeWay;
                bool subForceOneTex=false;
                if(doLight && !sub.pshd->atex.empty()) {
                    if(packetForceOneTex<0) {
                        if(!packet.tv && packet.palo!=g_pjt && g_ui.uis==UIS_Playing) {
                            packetForceOneTex=(estimateScreenPx(packet.model,glob.posCenter,glob.sRadius*cachedScale)<48.0f)?1:0;
                        } else {
                            packetForceOneTex=0;
                        }
                    }
                    subForceOneTex=(packetForceOneTex>0);
                }
                if(isLiveLightOnly(packet,sub,activePose)) {
                    const LiveKey lk{&sub,packet.palo};
                    LiveEntry& le=liveLightCache[lk];
                    if(le.seenThisFrame==frame) {
                        liveDecisions.push_back({uint32_t(pi),uint32_t(si),false,false});
                    } else {
                        le.seenThisFrame=frame;le.usedFrame=frame;
                        if(!le.verts[0] || !le.verts[1] || le.source!=sub.vertices.data() || le.count!=vertCount || le.vao!=glob.VAO) {
                            if(le.verts[0] || le.verts[1])C3D_FrameSync();
                            freeLocalAllocation(le.verts[0],le.chunk[0],le.base[0],le.cap[0],le.count,le.owned[0]);
                            freeLocalAllocation(le.verts[1],le.chunk[1],le.base[1],le.cap[1],le.count,le.owned[1]);
                            le.lastRelightFrame=0;
                            bool ok=true;
                            for(int b=0;b<2 && ok;++b) {
                                int lc=-1;unsigned lb=0,lcap=0;
                                if(allocLocalChunk(vertCount,lc,lb,lcap)) {
                                    le.chunk[b]=lc;le.base[b]=lb;le.cap[b]=lcap;le.owned[b]=false;
                                    le.verts[b]=localChunks[lc].vertices+lb;
                                    le.bufOffset[b]=localChunks[lc].bufOffset+u32(lb*sizeof(Vertex));
                                } else {
                                    size_t bytes=vertCount*sizeof(Vertex);
                                    if(meshBytes+bytes<=24u*1024u*1024u) {
                                        if(Vertex* mem=static_cast<Vertex*>(linearAlloc(bytes))) {
                                            meshBytes+=bytes;
                                            le.verts[b]=mem;le.bufOffset[b]=bufOffsetFor(mem);
                                            le.chunk[b]=-1;le.base[b]=0;le.cap[b]=0;le.owned[b]=true;
                                        } else ok=false;
                                    } else ok=false;
                                }
                            }
                            if(ok) {
                                le.source=sub.vertices.data();le.count=vertCount;le.vao=glob.VAO;
                            } else {
                                freeLocalAllocation(le.verts[0],le.chunk[0],le.base[0],le.cap[0],vertCount,le.owned[0]);
                                freeLocalAllocation(le.verts[1],le.chunk[1],le.base[1],le.cap[1],vertCount,le.owned[1]);
                                le.source=nullptr;le.count=0;
                            }
                        }
                        if(le.verts[0] && le.verts[1]) {
                            const bool moved=(le.lastRelightFrame==0) || std::memcmp(&le.model,&packet.model,sizeof(glm::mat4))!=0;
                            const bool parityHit=(((uintptr_t(&sub)>>4)&1u)==(frame&1u));
                            const bool needRelight=moved || (frame-le.lastRelightFrame>=2u) || parityHit;
                            liveDecisions.push_back({uint32_t(pi),uint32_t(si),true,needRelight});
                            if(needRelight) {
                                const uint8_t writeSlot=le.curSlot^1u;
                                animJobs.push_back({uint32_t(pi),uint32_t(si),0,0,0,cachedScale,0,false,subForceOneTex,-1,-1,-1,le.verts[writeSlot],lk,writeSlot});
                                le.model=packet.model;le.curSlot=writeSlot;le.lastRelightFrame=frame;
                                if(le.chunk[writeSlot]>=0) {
                                    auto& lc=localChunks[le.chunk[writeSlot]];
                                    lc.dirtyMin=std::min(lc.dirtyMin,le.base[writeSlot]);
                                    lc.dirtyMax=std::max(lc.dirtyMax,le.base[writeSlot]+unsigned(vertCount));
                                } else {
                                    ownedLiveFlush.push_back({le.verts[writeSlot],vertCount*sizeof(Vertex)});
                                }
                            }
                            continue;
                        }
                        liveDecisions.push_back({uint32_t(pi),uint32_t(si),false,false});
                    }
                }
                uint32_t bOff=0,bCnt=0;
                if(hasSkin && doLight) {
                    if(!packetBonesBuilt) {
                        const glm::mat3 normalMat(packet.model);
                        packetBoneOff=uint32_t(allBoneNormMats.size());
                        packetBoneCount=uint32_t(set.boneMatrices.size());
                        allBoneNormMats.reserve(allBoneNormMats.size()+packetBoneCount);
                        for(size_t b=0;b<set.boneMatrices.size();++b)
                            allBoneNormMats.push_back(normalMat*glm::mat3(set.boneMatrices[b]));
                        packetBonesBuilt=true;
                    }
                    bOff=packetBoneOff;bCnt=packetBoneCount;
                }
                wave1Used=(wave1Used+3u)&~3u;
                animJobs.push_back({uint32_t(pi),uint32_t(si),uint32_t(wave1Used),bOff,bCnt,cachedScale,0,false,subForceOneTex,-1,-1,-1,nullptr,{},0});
                wave1Used+=vertCount;
            }
            if(anyStaticSub) {
                const GlobKey gk{&glob,packet.palo};
                uint32_t gcIdx;
                if(uint32_t* gcSlot=globCacheMap.find(gk)) {
                    gcIdx=*gcSlot;
                } else {
                    if(!globCacheFree.empty()) {gcIdx=globCacheFree.back();globCacheFree.pop_back();}
                    else {gcIdx=uint32_t(globCachePool.size());globCachePool.emplace_back();}
                    globCacheMap[gk]=gcIdx;
                }
                packetGcIdx[pi]=gcIdx;
                auto& gc=globCachePool[gcIdx];
                if(gc.seenFrame==frame)gc.instanced=true;
                else gc.seenFrame=frame;
            }
        }
        glm::mat4 uvToClip(0);uvToClip[0][0]=2;uvToClip[1][1]=2;uvToClip[3]=glm::vec4(-1,-1,-.5f,1);
        glm::mat4 casterProj[4]{};
        float casterAlpha[4]{};
        uint64_t casterPoseHash[4]{};
        bool casterNeedRender[4]{};
        unsigned shadowMapsSkipped=0;
        for(size_t m=0;m<caneCasters.size();++m) {
            const auto& caster=caneCasters[m];SHADOW* shadow=caster.owner->pshadowGen;
            const auto& map=shadowMaps[m];
            casterProj[m]=uvToClip*shadow->matWorldToUv*caster.model;
            casterAlpha[m]=std::clamp(caster.alpha,0.0f,1.0f);
            casterPoseHash[m]=hashCasterState(caster.owner);
            bool need=true;
            if(map.valid && map.target && map.lastOwner==caster.owner && std::abs(map.lastAlpha-casterAlpha[m])<1e-4f) {
                if(std::memcmp(&map.lastProjection,&casterProj[m],sizeof(glm::mat4))==0 && casterPoseHash[m]==map.lastPoseHash) {
                    need=false;
                } else if(static_cast<const ALO*>(caster.owner)!=g_pjt &&
                          (frame-map.lastUpdateFrame)<2u &&
                          glm::distance2(g_pcm->pos,glm::vec3(caster.model[3]))>1440000.0f &&
                          std::abs(casterProj[m][3][0]-map.lastProjection[3][0])<0.25f &&
                          std::abs(casterProj[m][3][1]-map.lastProjection[3][1])<0.25f) {
                    need=false;
                }
            }
            casterNeedRender[m]=need;
            if(!need)++shadowMapsSkipped;
        }
        size_t casterFallbackVertTotal=0;
        for(size_t m=0;m<caneCasters.size();++m) {
            if(!casterNeedRender[m])continue;
            const auto& caster=caneCasters[m];
            const glm::mat3 casterRot(caster.model);
            const float casterMaxScale=std::max({glm::length(casterRot[0]),glm::length(casterRot[1]),glm::length(casterRot[2])});
            for(auto& glob:caster.owner->globset.aglob) {
                for(auto& sub:glob.asubglob) {
                    const size_t vertCount=sub.vertices.size(),idxCount=sub.indices.size()*3;
                    if(!vertCount || !idxCount)continue;
                    if(!casterHasPacket[m]) {
                        wave1Used=(wave1Used+3u)&~3u;
                        casterJobs.push_back({uint32_t(m),&glob,&sub,uint32_t(wave1Used),casterMaxScale});
                        wave1Used+=vertCount;
                    } else {
                        casterFallbackVertTotal+=vertCount;
                    }
                }
            }
        }
        for(const auto& hud:caneHud) {
            C3D_Tex* ht=texture(imageForGlTex(hud.texture),true);
            wave1Used=(wave1Used+3u)&~3u;
            uint32_t hOff=uint32_t(wave1Used);
            hudDraws.push_back({hOff,unsigned(hud.vertices.size()),ht,hud.tv,hud.additive,hud.layer,hud.scale,hud.dx,hud.dy});
            if(!hud.vertices.empty())hudJobs.push_back({&hud,hOff});
            wave1Used+=hud.vertices.size();
        }
        size_t used=(wave1Used+3u)&~3u,totalBatchIndices=0;
        frameCamRight=glm::normalize(glm::vec3(g_pcm->mat[1]));
        frameCamUp=glm::normalize(glm::vec3(g_pcm->mat[2]));
        size_t particleBudget=0,particleRemain=8192;
        for(size_t pj=0;pj<caneParticles.size();++pj) {
            const auto& p=caneParticles[pj];
            const auto& group=*p.group;
            if(!group.pshd || group.pshd->atex.empty())continue;
            const auto& tex=group.pshd->atex[0];
            const auto& images=group.pshd->shdk==SHDK_ThreeWay?tex.ambientImageIndex:tex.diffuseImageIndex;
            const int table=std::clamp(group.cqwTexture,0,32);
            if(!table || images.empty())continue;
            size_t groupBudget=0;
            unsigned links=0;
            for(BLIP* b=group.dlBlip.pblipFirst;b && links++<1024;b=b->dle.pblipNext)
                groupBudget+=std::clamp(b->cblipe,0,44);
            if(!groupBudget)continue;
            ParticleJob pjob{};
            pjob.packetIdx=uint32_t(pj);
            pjob.maxBlips=uint32_t(std::min(groupBudget,particleRemain));
            if(particleRemain>pjob.maxBlips)particleRemain-=pjob.maxBlips;
            else particleRemain=0;
            for(int entry=0;entry<table;++entry) {
                const int imageFrame=group.aiframeTexture[entry];
                pjob.tableTex[entry]=(imageFrame>=0 && size_t(imageFrame)<images.size())?texture(images[imageFrame],false,true):nullptr;
            }
            particleBudget+=groupBudget;
            particleJobs.push_back(pjob);
        }
        if(particleJobVerts.size()<particleJobs.size())particleJobVerts.resize(particleJobs.size());
        if(particleJobDraws.size()<particleJobs.size())particleJobDraws.resize(particleJobs.size());
        size_t required=used+std::min(particleBudget,size_t(8192))*6+casterFallbackVertTotal+(caneCasters.empty()?0:12288)+std::min(celEdgeCap,size_t(outlineBudget))*6+12;
        reserveDynamic(required);
        pk[3]=svcGetSystemTick();
        WaveGuard guard1{this,kickWave(WaveMode::Prep1,uint32_t(celJobs.size()+animJobs.size()+hudJobs.size()+casterJobs.size()+particleJobs.size()))};
        size_t animCursor=0,liveDecCursor=0;
        animDrawSlots.clear();animDrawSlots.reserve(animJobs.size());
        unsigned animFlattens=0,movedFlattens=0,newFlattens=0,totalDraws=0,threeWayCount=0;tkAnim=tkMoved=0;
        for(size_t pi=0;pi<canePackets.size();++pi) {
            const auto& packet=canePackets[pi];
            const auto& glob=*packet.pglob;
            const auto& asubs=glob.asubglob;
            if(asubs.empty())continue;
            float cachedScale=-1.0f;
            auto getScale=[&]() {
                if(cachedScale<0.0f) {
                    const glm::mat3 r(packet.model);
                    cachedScale=std::max({glm::length(r[0]),glm::length(r[1]),glm::length(r[2])});
                }
                return cachedScale;
            };
            const bool isBatchablePhase=
                packet.rp==RP_Opaque || packet.rp==RP_Cutout ||
                packet.rp==RP_OpaqueAfterProjVolume || packet.rp==RP_CutoutAfterProjVolume;
            unsigned casterValidMask=0;
            if(isBatchablePhase && !caneCasters.empty()) {
                for(size_t m=0;m<caneCasters.size();++m) {
                    SHADOW* sh=caneCasters[m].owner->pshadowGen;
                    if(sh && sh->pshd && FShadowValid(sh,glob.grfglob))casterValidMask|=(1u<<m);
                }
            }
            int packetCasterIdx=-1;
            for(size_t m=0;m<caneCasters.size();++m) {
                if(static_cast<const ALO*>(caneCasters[m].owner)==packet.palo) {
                    packetCasterIdx=int(m);break;
                }
            }
            const uint32_t gcIdx=packetGcIdx[pi];
            GlobCache* gcPtr=(gcIdx!=~0u)?&globCachePool[gcIdx]:nullptr;
            bool needStaticMoveCheck=false,shouldReshadeMoved=false;
            int flipFlag=-1;
            auto isFlipped=[&]() {
                if(flipFlag<0)flipFlag=(glm::determinant(glm::mat3(packet.model))<0.0f)?1:0;
                return flipFlag!=0;
            };
            if(gcPtr) {
                auto& gc=*gcPtr;
                const bool reusedThisFrame=(gc.used==frame);
                if(gc.vao!=glob.VAO || gc.subs.size()!=asubs.size()) {
                    const bool wasInstanced=gc.instanced;
                    bool synced=false;
                    for(auto& sc:gc.subs)if(sc.localVerts) {
                        if(!synced) {C3D_FrameSync();synced=true;}
                        freeLocalAllocation(sc.localVerts,sc.localChunk,sc.localBase,sc.localCap,sc.count,sc.localOwned);
                    }
                    gc=GlobCache{};gc.vao=glob.VAO;gc.model=packet.model;gc.flipped=isFlipped();gc.subs.resize(asubs.size());
                    gc.seenFrame=frame;gc.instanced=wasInstanced;gc.moved=wasInstanced;
                } else if(!reusedThisFrame) {
                    bool badSource=false;
                    for(size_t si=0;si<asubs.size();++si) {
                        const auto& sc=gc.subs[si];const auto& sub=asubs[si];
                        if(sc.source && (sc.source!=sub.vertices.data() || sc.count!=sub.vertices.size())) {badSource=true;break;}
                    }
                    if(badSource) {
                        const bool wasInstanced=gc.instanced;
                        bool synced=false;
                        for(auto& sc:gc.subs)if(sc.localVerts) {
                            if(!synced) {C3D_FrameSync();synced=true;}
                            freeLocalAllocation(sc.localVerts,sc.localChunk,sc.localBase,sc.localCap,sc.count,sc.localOwned);
                        }
                        gc=GlobCache{};gc.vao=glob.VAO;gc.model=packet.model;gc.flipped=isFlipped();gc.subs.resize(asubs.size());
                        gc.seenFrame=frame;gc.instanced=wasInstanced;gc.moved=wasInstanced;
                    } else if(gc.anyWorldBaked && (gc.instanced || std::memcmp(&gc.model,&packet.model,sizeof(glm::mat4))!=0)) {
                        gc.anyWorldBaked=false;gc.moved=true;gc.flipped=isFlipped();gc.model=packet.model;
                        for(auto& sc:gc.subs) {
                            sc.worldChunk=-1;sc.worldBase=0;std::vector<uint16_t>().swap(sc.worldIndices);
                            sc.localIndices=nullptr;sc.localIndexCount=0;sc.localIdxPhysOffset=~0u;sc.batchKey=0xFFFFu;sc.texValid=false;sc.tex0=nullptr;
                        }
                    } else if(!gc.anyWorldBaked && std::memcmp(&gc.model,&packet.model,sizeof(glm::mat4))!=0) {
                        gc.flipped=isFlipped();needStaticMoveCheck=true;
                        glm::vec3 d=glm::vec3(packet.model[3])-glm::vec3(gc.model[3]);
                        shouldReshadeMoved=(glm::dot(d,d)>4096.0f || (((uintptr_t(&glob)*2654435761u)>>27)==(frame&31)));
                        if(!shouldReshadeMoved) {
                            bool anyThreeWay=false;
                            for(const auto& sub:asubs)if(sub.pshd && sub.pshd->shdk==SHDK_ThreeWay) {anyThreeWay=true;break;}
                            if(!anyThreeWay)gc.model=packet.model;
                        }
                    } else {
                        flipFlag=gc.flipped?1:0;
                    }
                }
                gc.used=frame;
            }
            const bool activePose=hasActivePose(packet);
            for(size_t si=0;si<asubs.size();++si) {
                auto& sub=asubs[si];
                size_t vertCount=sub.vertices.size(),idxCount=sub.indices.size()*3;if(!vertCount || !idxCount)continue;
                const bool isSubThreeWay=sub.pshd && sub.pshd->shdk==SHDK_ThreeWay && !sub.pshd->atex.empty();
                if(isSubThreeWay)++threeWayCount;
                const Vertex* vertices=nullptr;
                const SubCache* scPtr=nullptr;
                C3D_Tex* t0=nullptr,*t1=nullptr,*t2=nullptr;
                uint8_t texMode=0;
                bool subWorldBaked=false;
                if(animated(packet,sub,activePose)) {
                    LiveDecision ld{};
                    if(isLiveLightOnly(packet,sub,activePose) &&
                       liveDecCursor<liveDecisions.size() &&
                       liveDecisions[liveDecCursor].packetIdx==pi &&
                       liveDecisions[liveDecCursor].subIdx==si) {
                        ld=liveDecisions[liveDecCursor++];
                    }
                    const auto& ib=indexBufferFor(sub,glob.VAO);
                    if(ld.active) {
                        const LiveEntry& le=liveLightCache[{&sub,packet.palo}];
                        vertices=le.verts[le.curSlot];
                        const u32 liveBufOff=le.bufOffset[le.curSlot];
                        if(packetCasterIdx>=0 && casterNeedRender[packetCasterIdx])casterDynVerts[packetCasterIdx].push_back({&sub,vertices,liveBufOff});
                        Draw d{vertices,liveBufOff,ib.data,ib.count,nullptr,nullptr,nullptr,0,false,isFlipped(),false,&packet,packet.rp,packet.z,sub.pshd,&sub,uint32_t(pi),std::clamp(packet.darken,0.0f,1.0f),std::clamp(packet.alpha,0.0f,1.0f),DrawKind::StandardAlpha,ib.physOffset};
                        if(ld.relit) {
                            ++animCursor;++animFlattens;
                            uint8_t b=uint8_t(std::min(unsigned(packet.rp),unsigned(RP_Max-1)));
                            animDrawSlots.push_back({uint32_t(phaseBuckets[b].size()),b,false});
                            phaseBuckets[b].push_back(d);
                        } else {
                            ++liveLightSkipped;
                            int i0=le.i0,i1=le.i1,i2=le.i2;
                            if(sub.uvSai) {
                                if(isSubThreeWay)resolveThreeWayImages(sub,imageFor(sub),i0,i1,i2);
                                else i0=imageFor(sub);
                            }
                            d.texMode=le.texMode;d.hasSoftAlpha=le.hasSoftAlpha;
                            if(le.texMode==0) {d.tex0=texture(i0);}
                            else if(le.texMode==1) {d.tex1=texture(i1);d.tex0=(i0==i1?d.tex1:texture(i0,false,false,64));}
                            else {d.tex1=texture(i1);d.tex0=(i0==i1?d.tex1:texture(i0,false,false,64));d.tex2=(i2==i1?d.tex1:(i2==i0?d.tex0:texture(i2,false,false,64)));}
                            phaseBuckets[std::min(unsigned(packet.rp),unsigned(RP_Max-1))].push_back(d);
                        }
                    } else {
                        const uint32_t dynOff=animJobs[animCursor].dynOffset;
                        const u32 dynBufOff=dynamicBufOffset[slot]+u32(dynOff*sizeof(Vertex));
                        Vertex* dyn=dynamic[slot]+dynOff;
                        ++animCursor;
                        ++animFlattens;
                        vertices=dyn;
                        if(packetCasterIdx>=0 && casterNeedRender[packetCasterIdx])casterDynVerts[packetCasterIdx].push_back({&sub,dyn,dynBufOff});
                        Draw d{vertices,dynBufOff,ib.data,ib.count,nullptr,nullptr,nullptr,0,false,isFlipped(),false,&packet,packet.rp,packet.z,sub.pshd,&sub,uint32_t(pi),std::clamp(packet.darken,0.0f,1.0f),std::clamp(packet.alpha,0.0f,1.0f),DrawKind::StandardAlpha,ib.physOffset};
                        if(packet.tv) {
                            animDrawSlots.push_back({uint32_t(tvDraws.size()),0,true});
                            tvDraws.push_back(d);
                        } else {
                            uint8_t b=uint8_t(std::min(unsigned(packet.rp),unsigned(RP_Max-1)));
                            animDrawSlots.push_back({uint32_t(phaseBuckets[b].size()),b,false});
                            phaseBuckets[b].push_back(d);
                        }
                    }
                    triangleCount+=ib.count/3;++totalDraws;
                } else {
                    auto& gc=*gcPtr;
                    auto& sc=gc.subs[si];
                    scPtr=&sc;
                    const bool wantWorldBake=!packet.dynamic && !packet.tv && isBatchablePhase && !gc.moved && !gc.instanced && sc.localVerts==nullptr && glob.poseCount==0;
                    if(sc.worldChunk<0 && !sc.localVerts) {
                        int i0=-1,i1=-1,i2=-1;
                        int chunkIdx=-1;unsigned base=0;
                        if(wantWorldBake && allocWorldChunk(vertCount,chunkIdx,base)) {
                            WorldChunk& wc=worldChunks[chunkIdx];
                            flatten(mainScratch,wc.vertices+base,packet,sub,false,true,true,getScale(),nullptr,0,&sc.texMode,&i0,&i1,&i2,&sc.hasSoftAlpha);
                            wc.dirtyMin=std::min(wc.dirtyMin,base);
                            wc.dirtyMax=std::max(wc.dirtyMax,base+unsigned(vertCount));
                            sc.worldChunk=chunkIdx;sc.worldBase=base;sc.source=sub.vertices.data();sc.count=vertCount;
                            sc.worldCenter=glm::vec3(packet.model*glm::vec4(sub.posCenter,1));
                            sc.worldRadius=glob.rSubglobRadius*sub.sRadius;
                            sc.worldIndices.resize(idxCount);
                            uint16_t* dst=sc.worldIndices.data();
                            const bool flip=isFlipped();
                            for(const auto& tri:sub.indices) {
                                uint16_t v0=uint16_t(base+(tri.v1<vertCount?tri.v1:0));
                                uint16_t v1=uint16_t(base+(tri.v2<vertCount?tri.v2:0));
                                uint16_t v2=uint16_t(base+(tri.v3<vertCount?tri.v3:0));
                                if(flip)std::swap(v1,v2);
                                *dst++=v0;*dst++=v1;*dst++=v2;
                            }
                            gc.anyWorldBaked=true;++newFlattens;
                        } else {
                            int lChunk=-1;unsigned lBase=0,lCap=0;
                            if(allocLocalChunk(vertCount,lChunk,lBase,lCap)) {
                                WorldChunk& lc=localChunks[lChunk];
                                sc.localVerts=lc.vertices+lBase;
                                sc.localBufOffset=lc.bufOffset+u32(lBase*sizeof(Vertex));
                                sc.localChunk=lChunk;sc.localBase=lBase;sc.localCap=lCap;sc.localOwned=false;
                                sc.count=vertCount;sc.source=sub.vertices.data();
                                flatten(mainScratch,sc.localVerts,packet,sub,false,false,true,getScale(),nullptr,0,&sc.texMode,&i0,&i1,&i2,&sc.hasSoftAlpha);
                                lc.dirtyMin=std::min(lc.dirtyMin,lBase);
                                lc.dirtyMax=std::max(lc.dirtyMax,lBase+unsigned(vertCount));
                                ++newFlattens;
                            } else {
                                size_t bytes=vertCount*sizeof(Vertex);
                                if(meshBytes+bytes>24*1024*1024)throw std::runtime_error("Native static mesh budget exceeded");
                                sc.localVerts=static_cast<Vertex*>(linearAlloc(bytes));
                                if(!sc.localVerts)throw std::runtime_error("Native static mesh allocation failed");
                                sc.localBufOffset=bufOffsetFor(sc.localVerts);
                                sc.localChunk=-1;sc.localBase=0;sc.localCap=0;sc.localOwned=true;
                                sc.count=vertCount;sc.source=sub.vertices.data();meshBytes+=bytes;
                                flatten(mainScratch,sc.localVerts,packet,sub,false,false,true,getScale(),nullptr,0,&sc.texMode,&i0,&i1,&i2,&sc.hasSoftAlpha);
                                GSPGPU_FlushDataCache(sc.localVerts,bytes);++newFlattens;
                            }
                        }
                        sc.id0=int16_t(i0);sc.id1=int16_t(i1);sc.id2=int16_t(i2);
                        sc.threeWay=isSubThreeWay;sc.texValid=true;sc.tex0=nullptr;sc.batchKey=0xFFFFu;
                        resolveSubTextures(sc,sub.uvSai!=nullptr,t0,t1,t2);
                    } else if(sc.worldChunk>=0) {
                        if(sub.uvSai) {
                            if(isSubThreeWay) {
                                int i0,i1,i2;resolveThreeWayImages(sub,imageFor(sub),i0,i1,i2);
                                sc.id0=int16_t(i0);sc.id1=int16_t(i1);sc.id2=int16_t(i2);
                            } else {sc.id0=int16_t(imageFor(sub));}
                        }
                        resolveSubTextures(sc,sub.uvSai!=nullptr,t0,t1,t2);
                    } else {
                        if(needStaticMoveCheck && shouldReshadeMoved && sub.pshd && sub.pshd->shdk==SHDK_ThreeWay) {
                            int i0=-1,i1=-1,i2=-1;
                            {u64 k0=svcGetSystemTick();flatten(mainScratch,sc.localVerts,packet,sub,false,false,true,getScale(),nullptr,0,&sc.texMode,&i0,&i1,&i2,&sc.hasSoftAlpha);tkMoved+=svcGetSystemTick()-k0;}
                            if(sc.localChunk>=0) {
                                WorldChunk& lc=localChunks[sc.localChunk];
                                lc.dirtyMin=std::min(lc.dirtyMin,sc.localBase);
                                lc.dirtyMax=std::max(lc.dirtyMax,sc.localBase+unsigned(vertCount));
                            } else {
                                GSPGPU_FlushDataCache(sc.localVerts,vertCount*sizeof(Vertex));
                            }
                            sc.id0=int16_t(i0);sc.id1=int16_t(i1);sc.id2=int16_t(i2);sc.tex0=nullptr;
                            ++movedFlattens;
                        } else if(sub.uvSai) {
                            if(isSubThreeWay) {
                                int i0,i1,i2;resolveThreeWayImages(sub,imageFor(sub),i0,i1,i2);
                                sc.id0=int16_t(i0);sc.id1=int16_t(i1);sc.id2=int16_t(i2);
                            } else {sc.id0=int16_t(imageFor(sub));}
                        }
                        resolveSubTextures(sc,sub.uvSai!=nullptr,t0,t1,t2);
                    }
                    texMode=sc.texMode;
                    if(sc.worldChunk>=0) {
                        subWorldBaked=true;
                        vertices=worldChunks[sc.worldChunk].vertices+sc.worldBase;
                        const bool isOpaquePhase=(packet.rp==RP_Opaque || packet.rp==RP_OpaqueAfterProjVolume);
                        const bool isCutoutPhase=(packet.rp==RP_Cutout || packet.rp==RP_CutoutAfterProjVolume);
                        const C3D_Tex* alphaTex=(texMode>=1)?t1:t0;
                        bool batchableCutout=isCutoutPhase && packet.pglob->fTransluscentSort && !sc.hasSoftAlpha && isBinaryAlphaTex(alphaTex);
                        if(isCutoutPhase && !batchableCutout && packet.pglob->fTransluscentSort &&
                           !packet.tv && packet.palo!=g_pjt && g_ui.uis==UIS_Playing &&
                           packet.darken>=0.999f && packet.alpha>=0.999f &&
                           estimateScreenPxWorld(sc.worldCenter,sc.worldRadius)<40.0f) {
                            batchableCutout=true;
                        }
                        uint16_t bKey=0xFFFFu;
                        if((isOpaquePhase || batchableCutout) && packet.darken>=0.999f && packet.alpha>=0.999f) {
                            if(!sub.uvSai && sc.batchKey!=0xFFFFu)bKey=sc.batchKey;
                            else {
                                bKey=getBatchKey(sc.worldChunk,texMode,t0,t1,t2);
                                if(!sub.uvSai)sc.batchKey=bKey;
                            }
                        }
                        if(bKey!=0xFFFFu) {
                            staticBatches[packet.rp].push_back({sc.worldIndices.data(),unsigned(sc.worldIndices.size()),bKey,packet.z});
                            totalBatchIndices+=sc.worldIndices.size();
                        } else {
                            fadedWorldDraws.push_back({sc.worldIndices.data(),unsigned(sc.worldIndices.size()),worldChunks[sc.worldChunk].vertices,worldChunks[sc.worldChunk].bufOffset,texMode,t0,t1,t2,&packet,packet.rp,&sub,sc.hasSoftAlpha});
                            totalBatchIndices+=sc.worldIndices.size();
                        }
                        triangleCount+=unsigned(sc.worldIndices.size())/3;++totalDraws;
                    } else {
                        vertices=sc.localVerts;
                        if(packetCasterIdx>=0 && casterNeedRender[packetCasterIdx])casterDynVerts[packetCasterIdx].push_back({&sub,sc.localVerts,sc.localBufOffset});
                        if(!sc.localIndices) {
                            const auto& ib=indexBufferFor(sub,glob.VAO);
                            sc.localIndices=ib.data;sc.localIndexCount=ib.count;sc.localIdxPhysOffset=ib.physOffset;
                        }
                        Draw d{vertices,sc.localBufOffset,sc.localIndices,sc.localIndexCount,t0,t1,t2,texMode,false,isFlipped(),sc.hasSoftAlpha,&packet,packet.rp,packet.z,sub.pshd,&sub,uint32_t(pi),std::clamp(packet.darken,0.0f,1.0f),std::clamp(packet.alpha,0.0f,1.0f),DrawKind::StandardAlpha,sc.localIdxPhysOffset};
                        if(packet.tv)tvDraws.push_back(d);
                        else phaseBuckets[std::min(unsigned(packet.rp),unsigned(RP_Max-1))].push_back(d);
                        triangleCount+=sc.localIndexCount/3;++totalDraws;
                    }
                }
                if(casterValidMask && vertices) {
                    const glm::vec3 center=(subWorldBaked && scPtr)?scPtr->worldCenter:glm::vec3(packet.model*glm::vec4(sub.posCenter,1));
                    const float radius=(subWorldBaked && scPtr)?scPtr->worldRadius:(glob.rSubglobRadius*sub.sRadius);
                    for(size_t m=0;m<caneCasters.size();++m) {
                        if(!(casterValidMask&(1u<<m)))continue;
                        SHADOW* sh=caneCasters[m].owner->pshadowGen;
                        float reach=radius+sh->sRadiusEffect;
                        if(glm::distance2(center,sh->posEffect)<=reach*reach) {
                            shadowReceivers.push_back({unsigned(m),vertices,&packet,&sub,subWorldBaked});
                        }
                    }
                }
            }
            if(needStaticMoveCheck && shouldReshadeMoved && gcPtr)gcPtr->model=packet.model;
        }
        for(auto& wc:worldChunks) {
            if(wc.dirtyMin<wc.dirtyMax) {
                GSPGPU_FlushDataCache(wc.vertices+wc.dirtyMin,(wc.dirtyMax-wc.dirtyMin)*sizeof(Vertex));
                wc.dirtyMin=~0u;wc.dirtyMax=0;
            }
        }
        // Reserve before publishing any index pointers. Animated batches append
        // here after workers finish; no allocation may invalidate earlier draws.
        size_t animatedIndexBudget=0;
        for(const auto& job:animJobs) {
            if(job.dstOverride)continue;
            const auto& packet=canePackets[job.packetIdx];
            if(!packet.tv)animatedIndexBudget+=packet.pglob->asubglob[job.subIdx].indices.size()*3;
        }
        reserveBatchIndices(totalBatchIndices+animatedIndexBudget);
        size_t batchUsed=0;
        for(RP rp:{RP_Opaque,RP_Cutout,RP_OpaqueAfterProjVolume,RP_CutoutAfterProjVolume}) {
            auto& entries=staticBatches[rp];
            if(entries.empty())continue;
            touchedKeys.clear();
            entryNext.resize(entries.size());
            for(uint32_t idx=0;idx<uint32_t(entries.size());++idx) {
                const uint16_t k=entries[idx].key;
                if(bucketHead[k]==~0u)touchedKeys.push_back(k);
                entryNext[idx]=bucketHead[k];
                bucketHead[k]=idx;
            }
            auto& outMerged=mergedDraws[rp];
            outMerged.reserve(touchedKeys.size());
            for(uint16_t k:touchedKeys) {
                const unsigned startOff=unsigned(batchUsed);
                unsigned totalCount=0;
                float minD2=entries[bucketHead[k]].distSq;
                for(uint32_t idx=bucketHead[k];idx!=~0u;idx=entryNext[idx]) {
                    const auto& e=entries[idx];
                    std::memcpy(batchIndices[slot]+batchUsed,e.src,e.count*sizeof(uint16_t));
                    batchUsed+=e.count;
                    totalCount+=e.count;
                    if(e.distSq<minD2)minD2=e.distSq;
                }
                bucketHead[k]=~0u;
                const BatchKeyDesc& kd=batchKeyTable[k];
                outMerged.push_back({kd.chunkIdx,kd.texMode,kd.tex0,kd.tex1,kd.tex2,startOff,totalCount,minD2});
            }
            if((rp==RP_Opaque || rp==RP_OpaqueAfterProjVolume) && outMerged.size()>1) {
                auto band=[](float d2)->int {return d2<1000000.0f?0:(d2<6250000.0f?1:2);};
                std::sort(outMerged.begin(),outMerged.end(),[&](const MergedDraw& a,const MergedDraw& b) {
                    const int ba=band(a.minDistSq),bb=band(b.minDistSq);
                    if(ba!=bb)return ba<bb;
                    if(a.chunkIdx!=b.chunkIdx)return a.chunkIdx<b.chunkIdx;
                    if(a.texMode!=b.texMode)return a.texMode<b.texMode;
                    if(a.tex0!=b.tex0)return std::less<C3D_Tex*>{}(a.tex0,b.tex0);
                    if(a.tex1!=b.tex1)return std::less<C3D_Tex*>{}(a.tex1,b.tex1);
                    return std::less<C3D_Tex*>{}(a.tex2,b.tex2);
                });
            }
        }
        for(const auto& fw:fadedWorldDraws) {
            unsigned off=unsigned(batchUsed);
            std::memcpy(batchIndices[slot]+off,fw.src,fw.count*sizeof(uint16_t));
            batchUsed+=fw.count;
            const u32 idxPhys=batchIndicesPhys[slot]!=~0u?(batchIndicesPhys[slot]+u32(off*sizeof(uint16_t))):~0u;
            phaseBuckets[fw.phase].push_back({fw.chunkVerts,fw.bufOffset,batchIndices[slot]+off,fw.count,fw.tex0,fw.tex1,fw.tex2,fw.texMode,true,false,fw.hasSoftAlpha,fw.material,fw.phase,fw.material->z,fw.sub->pshd,fw.sub,~0u,std::clamp(fw.material->darken,0.0f,1.0f),std::clamp(fw.material->alpha,0.0f,1.0f),DrawKind::StandardAlpha,idxPhys});
        }
        drainWave(mainScratch);
        guard1.finish();
        for(auto& lc:localChunks) {
            if(lc.dirtyMin<lc.dirtyMax) {
                GSPGPU_FlushDataCache(lc.vertices+lc.dirtyMin,(lc.dirtyMax-lc.dirtyMin)*sizeof(Vertex));
                lc.dirtyMin=~0u;lc.dirtyMax=0;
            }
        }
        for(const auto& f:ownedLiveFlush) {
            if(f.ptr && f.bytes)GSPGPU_FlushDataCache(f.ptr,f.bytes);
        }
        unsigned particles=0;
        for(size_t pj=0;pj<particleJobs.size();++pj) {
            const auto& jVerts=particleJobVerts[pj];
            const auto& jDraws=particleJobDraws[pj];
            if(jVerts.empty() || jDraws.empty())continue;
            const unsigned baseVert=unsigned(used);
            std::memcpy(dynamic[slot]+used,jVerts.data(),jVerts.size()*sizeof(Vertex));
            used+=jVerts.size();
            particles+=unsigned(jVerts.size()/6u);
            for(const auto& pd:jDraws) {
                if(!pd.count)continue;
                if(!particleDraws.empty() && particleDraws.back().tex==pd.tex &&
                   particleDraws.back().additive==pd.additive &&
                   particleDraws.back().first+particleDraws.back().count==baseVert+pd.first) {
                    particleDraws.back().count+=pd.count;
                    if(pd.maxAlpha>particleDraws.back().maxAlpha)particleDraws.back().maxAlpha=pd.maxAlpha;
                } else {
                    particleDraws.push_back({baseVert+pd.first,pd.count,pd.tex,pd.additive,pd.maxAlpha});
                }
            }
        }
        pk[4]=svcGetSystemTick();
        for(size_t i=0;i<animJobs.size();++i) {
            const auto& job=animJobs[i];
            const auto& slotRef=animDrawSlots[i];
            Draw& d=slotRef.isTv?tvDraws[slotRef.drawIdx]:phaseBuckets[slotRef.bucket][slotRef.drawIdx];
            d.texMode=job.texMode;d.hasSoftAlpha=job.hasSoftAlpha;
            if(job.texMode==0) {d.tex0=texture(job.i0);}
            else if(job.texMode==1) {d.tex1=texture(job.i1);d.tex0=(job.i0==job.i1?d.tex1:texture(job.i0,false,false,64));}
            else {d.tex1=texture(job.i1);d.tex0=(job.i0==job.i1?d.tex1:texture(job.i0,false,false,64));d.tex2=(job.i2==job.i1?d.tex1:(job.i2==job.i0?d.tex0:texture(job.i2,false,false,64)));}
            if(job.liveKey.sub) {
                if(LiveEntry* lePtr=liveLightCache.find(job.liveKey)) {
                    lePtr->texMode=job.texMode;lePtr->hasSoftAlpha=job.hasSoftAlpha;
                    lePtr->i0=job.i0;lePtr->i1=job.i1;lePtr->i2=job.i2;
                }
            }
        }
        unsigned animatedDrawsSaved=0;
        // Batch opaque geometry only. Cutouts and light effects retain their
        // original order and per-material blending/depth passes.
        const auto dynamicOffset=[&](const Draw& d)->size_t {
            const uintptr_t p=uintptr_t(d.vertices),base=uintptr_t(dynamic[slot]);
            if(d.worldBaked || !d.material || d.material->alpha<.999f ||
               p<base || p>=base+wave1Used*sizeof(Vertex) || (p-base)%sizeof(Vertex))return size_t(-1);
            return (p-base)/sizeof(Vertex);
        };
        const auto sameTransform=[](const Draw& a,const Draw& b) {
            return a.material==b.material || (a.material && b.material &&
                a.material->alpha==b.material->alpha && a.material->darken==b.material->darken &&
                std::memcmp(&a.material->model,&b.material->model,sizeof(glm::mat4))==0);
        };
        const auto compatible=[&](const Draw& a,const Draw& b) {
            return sameTransform(a,b) && a.flipped==b.flipped && a.texMode==b.texMode &&
                a.tex0==b.tex0 && a.tex1==b.tex1 && a.tex2==b.tex2;
        };
        for(RP rp:{RP_Opaque,RP_OpaqueAfterProjVolume}) {
            auto& draws=phaseBuckets[rp];
            for(size_t first=0;first<draws.size();) {
                if(dynamicOffset(draws[first])==size_t(-1)) {++first;continue;}
                size_t end=first+1;
                while(end<draws.size() && sameTransform(draws[first],draws[end]) && dynamicOffset(draws[end])!=size_t(-1))++end;
                if(end-first>1)std::stable_sort(draws.begin()+first,draws.begin()+end,[](const Draw& a,const Draw& b) {
                    if(a.flipped!=b.flipped)return a.flipped<b.flipped;
                    if(a.texMode!=b.texMode)return a.texMode<b.texMode;
                    if(a.tex0!=b.tex0)return std::less<C3D_Tex*>{}(a.tex0,b.tex0);
                    if(a.tex1!=b.tex1)return std::less<C3D_Tex*>{}(a.tex1,b.tex1);
                    if(a.tex2!=b.tex2)return std::less<C3D_Tex*>{}(a.tex2,b.tex2);
                    return std::less<const Vertex*>{}(a.vertices,b.vertices);
                });
                first=end;
            }
            animatedDrawsSaved+=CaneMergeDynamicDraws(draws,batchIndices[slot],batchUsed,dynamicOffset,compatible);
            if(batchIndicesPhys[slot]!=~0u && batchUsed) {
                for(Draw& d:draws) {
                    if(d.indices>=batchIndices[slot] && d.indices<batchIndices[slot]+batchUsed) {
                        d.idxPhysOffset=batchIndicesPhys[slot]+u32((d.indices-batchIndices[slot])*sizeof(uint16_t));
                    }
                }
            }
            if(draws.size()>1) {
                const auto band=[](float d2)->int {return d2<810000.0f?0:1;};
                std::sort(draws.begin(),draws.end(),[&](const Draw& a,const Draw& b) {
                    const int ba=band(a.distance),bb=band(b.distance);
                    if(ba!=bb)return ba<bb;
                    if(a.texMode!=b.texMode)return a.texMode<b.texMode;
                    if(a.tex0!=b.tex0)return std::less<const void*>{}(a.tex0,b.tex0);
                    if(a.tex1!=b.tex1)return std::less<const void*>{}(a.tex1,b.tex1);
                    if(a.tex2!=b.tex2)return std::less<const void*>{}(a.tex2,b.tex2);
                    if(a.vertices!=b.vertices)return std::less<const void*>{}(a.vertices,b.vertices);
                    if(a.material!=b.material)return std::less<const void*>{}(a.material,b.material);
                    return a.flipped<b.flipped;
                });
            }
        }
        auto resolveDrawKinds=[&](std::vector<Draw>& list) {
            for(Draw& d:list) {
                if(d.phase==RP_ProjVolume) {d.kind=DrawKind::ProjVolume;continue;}
                if(d.phase==RP_Cutout || d.phase==RP_CutoutAfterProjVolume || d.phase==RP_Translucent) {
                    const bool cutout=(d.phase==RP_Cutout || d.phase==RP_CutoutAfterProjVolume);
                    const unsigned flags=d.shader?d.shader->grfshd:2;
                    const bool additive=!cutout?(flags!=2 && flags!=6):!d.material->pglob->fTransluscentSort;
                    const C3D_Tex* alphaTex=(d.texMode>=1)?(d.tex1?d.tex1:&white):(d.tex0?d.tex0:&white);
                    bool binaryCutout=cutout && !additive && !d.hasSoftAlpha &&
                        d.tintAlpha>=0.999f && isBinaryAlphaTex(alphaTex);
                    if(cutout && !binaryCutout && !additive && d.tintAlpha>=0.89f &&
                       !d.material->tv && d.material->palo!=g_pjt && g_ui.uis==UIS_Playing && d.sub) {
                        float scale=1.0f;
                        if(!d.worldBaked) {
                            const glm::mat3 r(d.material->model);
                            const float maxLenSq=std::max({glm::dot(r[0],r[0]),glm::dot(r[1],r[1]),glm::dot(r[2],r[2])});
                            scale=std::sqrt(maxLenSq);
                        } else if(d.material->pglob) {
                            scale=d.material->pglob->rSubglobRadius;
                        }
                        if(estimateScreenPx(d.material->model,d.sub->posCenter,d.sub->sRadius*scale)<40.0f)
                            binaryCutout=true;
                    }
                    if(binaryCutout)d.kind=DrawKind::BinaryCutout;
                    else if(d.tintAlpha>=0.89f)d.kind=additive?DrawKind::TwoPassAdditive:DrawKind::TwoPassAlpha;
                    else d.kind=additive?DrawKind::OnePassAdditive:DrawKind::OnePassAlpha;
                } else if((d.phase==RP_Opaque || d.phase==RP_OpaqueAfterProjVolume) && !d.material->tv && d.tintAlpha>=0.999f) {
                    d.kind=DrawKind::Opaque;
                } else {
                    d.kind=DrawKind::StandardAlpha;
                }
            }
        };
        for(int rp=0;rp<RP_Max;++rp)if(!phaseBuckets[rp].empty())resolveDrawKinds(phaseBuckets[rp]);
        if(!tvDraws.empty())resolveDrawKinds(tvDraws);
        // BinaryCutout draws are opaque + depth-writing + alpha-tested (>=128),
        // so sorting contiguous runs by texture/buffer cuts C3D_TexBind overhead
        // without changing the order of any soft-alpha or additive cutouts.
        for(RP rp:{RP_Cutout,RP_CutoutAfterProjVolume}) {
            auto& draws=phaseBuckets[rp];
            for(size_t first=0;first<draws.size();) {
                if(draws[first].kind!=DrawKind::BinaryCutout) {++first;continue;}
                size_t end=first+1;
                while(end<draws.size() && draws[end].kind==DrawKind::BinaryCutout)++end;
                if(end-first>1)std::sort(draws.begin()+first,draws.begin()+end,[](const Draw& a,const Draw& b) {
                    if(a.texMode!=b.texMode)return a.texMode<b.texMode;
                    if(a.tex0!=b.tex0)return std::less<const void*>{}(a.tex0,b.tex0);
                    if(a.tex1!=b.tex1)return std::less<const void*>{}(a.tex1,b.tex1);
                    if(a.tex2!=b.tex2)return std::less<const void*>{}(a.tex2,b.tex2);
                    if(a.bufOffset!=b.bufOffset)return a.bufOffset<b.bufOffset;
                    return a.flipped<b.flipped;
                });
                first=end;
            }
        }
        if(batchUsed)GSPGPU_FlushDataCache(batchIndices[slot],batchUsed*sizeof(uint16_t));
        if(frame%120==0)std::fprintf(stderr,"Native render savings: animated-draws=%u per-eye outline-small=%u outline-reduced=%u shadows-skipped=%u live-skip=%u\n",animatedDrawsSaved,outlineLodSkipped,outlineLodReduced,shadowMapsSkipped,liveLightSkipped);
        size_t casterJobCursor=0;
        for(size_t mapIndex=0;mapIndex<caneCasters.size();++mapIndex) {
            const auto& caster=caneCasters[mapIndex];SHADOW* shadow=caster.owner->pshadowGen;
            auto& map=shadowMaps[mapIndex];
            bool needRender=casterNeedRender[mapIndex];
            if(!map.target) {
                if(!C3D_TexInitVRAM(&map.texture,shadowSize,shadowSize,GPU_RGBA8))throw std::runtime_error("Native shadow texture allocation failed");
                C3D_TexSetFilter(&map.texture,GPU_LINEAR,GPU_LINEAR);
                C3D_TexSetWrap(&map.texture,GPU_CLAMP_TO_BORDER,GPU_CLAMP_TO_BORDER);map.texture.border=0;
                map.target=C3D_RenderTargetCreateFromTex(&map.texture,GPU_TEXFACE_2D,0,GPU_RB_DEPTH16);
                if(!map.target)throw std::runtime_error("Native shadow target allocation failed");
                needRender=true;
            }
            // Project the original animated caster into its authored shadow UVs.
            // Unlike the desktop clip conversion, PICA needs Z within [-W,0].
            ShadowPass pass{unsigned(casterSubs.size()),0,0,0,casterProj[mapIndex],casterAlpha[mapIndex],needRender};
            if(needRender) {
                if(!casterHasPacket[mapIndex]) {
                    while(casterJobCursor<casterJobs.size() && casterJobs[casterJobCursor].mapIndex<mapIndex)++casterJobCursor;
                    while(casterJobCursor<casterJobs.size() && casterJobs[casterJobCursor].mapIndex==mapIndex) {
                        const auto& job=casterJobs[casterJobCursor++];
                        const auto& ib=indexBufferFor(*job.sub,job.glob->VAO);
                        casterSubs.push_back({dynamic[slot]+job.dynOffset,dynamicBufOffset[slot]+u32(job.dynOffset*sizeof(Vertex)),ib.data,ib.count});
                        ++pass.subCount;
                    }
                } else {
                    const glm::mat3 casterRot(caster.model);
                    const float casterMaxScale=std::max({glm::length(casterRot[0]),glm::length(casterRot[1]),glm::length(casterRot[2])});
                    const auto& reused=casterDynVerts[mapIndex];
                    for(auto& glob:caster.owner->globset.aglob)for(auto& sub:glob.asubglob) {
                        size_t vertCount=sub.vertices.size(),idxCount=sub.indices.size()*3;if(!vertCount || !idxCount)continue;
                        const auto& ib=indexBufferFor(sub,glob.VAO);
                        const Vertex* cvPtr=nullptr;u32 cvBufOff=dynamicBufOffset[slot];
                        for(const auto& r:reused)if(r.sub==&sub) {cvPtr=r.vertices;cvBufOff=r.bufOffset;break;}
                        if(!cvPtr) {
                            const size_t base=used;
                            Vertex* dyn=dynamic[slot]+base;
                            CanePacket packet{&glob,caster.owner,caster.model,caster.alpha,RP_DynamicTexture,0};
                            flatten(mainScratch,dyn,packet,sub,animated(packet,sub),false,false,casterMaxScale);
                            used+=vertCount;cvPtr=dyn;cvBufOff=dynamicBufOffset[slot]+u32(base*sizeof(Vertex));
                        }
                        casterSubs.push_back({cvPtr,cvBufOff,ib.data,ib.count});
                        ++pass.subCount;
                    }
                }
                map.lastOwner=caster.owner;map.lastProjection=casterProj[mapIndex];
                map.lastAlpha=casterAlpha[mapIndex];map.lastPoseHash=casterPoseHash[mapIndex];
                map.lastUpdateFrame=frame;map.valid=true;
            }
            const float cl=glm::length(shadow->normalCast);
            const glm::vec3 negCastDir=cl>.001f?-shadow->normalCast/cl:glm::vec3(0);
            const uint8_t shadowAlphaByte=packByte(std::clamp(shadow->pshd?shadow->pshd->rgba.a:1.0f,0.0f,1.0f));
            shadowMapInfo[mapIndex]={shadow,negCastDir,g_pcm->pos,cl,shadowAlphaByte};
            shadowPasses.push_back(pass);
        }
        if(shadowJobOut.size()<shadowReceivers.size())shadowJobOut.resize(shadowReceivers.size());
        pk[5]=svcGetSystemTick();
        WaveGuard guard2{this,!shadowReceivers.empty()?kickWave(WaveMode::ShadowClip,uint32_t(shadowReceivers.size())):false};
        unsigned outlineFirst=unsigned(used),outlineVertCount=0;
        const size_t maxOutlineVerts=std::min(celEdgeCap,size_t(outlineBudget))*6;
        for(size_t ci=0;ci<celJobs.size() && outlineVertCount<maxOutlineVerts;++ci) {
            const auto& src=celJobVerts[ci];
            const size_t take=std::min(maxOutlineVerts-outlineVertCount,src.size());
            if(take) {
                std::memcpy(dynamic[slot]+used,src.data(),take*sizeof(Vertex));
                used+=take;outlineVertCount+=unsigned(take);
            }
        }
        const unsigned outlineSegments=outlineVertCount/6;
        if(!shadowReceivers.empty()) {
            drainWave(mainScratch);
            guard2.finish();
        }
        unsigned overlayVertices=0;
        for(size_t mapIndex=0;mapIndex<caneCasters.size();++mapIndex) {
            auto& pass=shadowPasses[mapIndex];
            pass.overlayFirst=unsigned(used);
            for(size_t r=0;r<shadowReceivers.size();++r) {
                if(shadowReceivers[r].mapIndex!=mapIndex)continue;
                const auto& src=shadowJobOut[r];
                size_t avail=(overlayVertices<12288)?((12288-overlayVertices)/3)*3:0;
                size_t take=std::min(src.size(),avail);
                if(take) {
                    std::memcpy(dynamic[slot]+used,src.data(),take*sizeof(Vertex));
                    used+=take;pass.overlayCount+=unsigned(take);overlayVertices+=unsigned(take);
                }
            }
        }
        pk[6]=svcGetSystemTick();
        if(frame%120==0)std::fprintf(stderr,"Native effects: particles=%u shadow-maps=%u receivers=%u shadow-volumes=%lu\n",particles,unsigned(shadowPasses.size()),overlayVertices/3,
            (unsigned long)phaseBuckets[RP_ProjVolume].size());
        if(frame%120==0)std::fprintf(stderr,"Native cel: packets=%u segments=%u lights=%u materials=%u\n",unsigned(caneCelPackets.size()),outlineSegments,unsigned(frameLights.size()),threeWayCount);
        hudVertexCount=0;for(const auto& hud:hudDraws)hudVertexCount+=hud.count;
        if(frame%120==0)std::fprintf(stderr,"Native binocular UI: state=%d portraits=%u vertices=%u\n",int(g_binoc.binocs),unsigned(tvDraws.size()),hudVertexCount);
        if(frame%120==0) {
            unsigned coins=0,health=0,dialogue=0,portraits=0;
            for(const auto& h:hudDraws) {
                if(h.layer==CaneHudLayer::Coins)coins+=h.count;
                if(h.layer==CaneHudLayer::Health)health+=h.count;
                if(h.layer==CaneHudLayer::Dialogue) {dialogue+=h.count;if(h.tv)++portraits;}
            }
            std::fprintf(stderr,"Bottom HUD: active=%d coins=%u health=%u dialogue=%u portraits=%u startup=%d\n",int(caneBottomActive),coins,health,dialogue,portraits,int(caneStartupPresentation));
        }
        unsigned wmOverlayFirst=0;
        if(!phaseBuckets[RP_WorldMap].empty()) {
            float pillarboxOpacity=1.0f;
            if(WM* pwm=g_wmc.pwmCurrent) {
                if(pwm->wms==WMS_Appearing && pwm->pasegOpen && pwm->pasegOpen->tMax>0.0f)
                    pillarboxOpacity=std::clamp((g_clock.tReal-pwm->tWms)/pwm->pasegOpen->tMax,0.0f,1.0f);
                else if(pwm->wms==WMS_Disappearing && pwm->pasegClose && pwm->pasegClose->tMax>0.0f)
                    pillarboxOpacity=1.0f-std::clamp((g_clock.tReal-pwm->tWms)/pwm->pasegClose->tMax,0.0f,1.0f);
            }
            const uint8_t pa=packByte(pillarboxOpacity);
            const float rects[2][4]={{0.0f,0.0f,40.0f,240.0f},{360.0f,0.0f,400.0f,240.0f}};
            const unsigned qxy[6][2]={{0,1},{2,1},{0,3},{0,3},{2,1},{2,3}};
            wmOverlayFirst=unsigned(used);
            for(const auto& r:rects) {
                for(const auto& q:qxy) {
                    auto& v=dynamic[slot][used++];
                    v.position[0]=r[q[0]];v.position[1]=r[q[1]];v.position[2]=0.0f;
                    v.color[0]=0;v.color[1]=0;v.color[2]=0;v.color[3]=pa;
                    v.uv[0]=0.0f;v.uv[1]=0.0f;
                }
            }
        }
        std::memset(dynamic[slot],0,3*sizeof(Vertex));
        if(used)GSPGPU_FlushDataCache(dynamic[slot],used*sizeof(Vertex));
        // Retire local caches for objects no longer submitted, without retaining game pointers.
        if(frame%60==0) {
            globCacheMap.eraseIf([&](const GlobKey&,uint32_t idx) {
                GlobCache& gc=globCachePool[idx];
                if(!gc.anyWorldBaked && frame-gc.used>120) {
                    for(auto& sc:gc.subs)if(sc.localVerts) {
                        freeLocalAllocation(sc.localVerts,sc.localChunk,sc.localBase,sc.localCap,sc.count,sc.localOwned);
                    }
                    gc=GlobCache{};
                    globCacheFree.push_back(idx);
                    return true;
                }
                return false;
            });
            liveLightCache.eraseIf([&](const LiveKey&,LiveEntry& le) {
                if(frame-le.usedFrame>120) {
                    freeLocalAllocation(le.verts[0],le.chunk[0],le.base[0],le.cap[0],le.count,le.owned[0]);
                    freeLocalAllocation(le.verts[1],le.chunk[1],le.base[1],le.cap[1],le.count,le.owned[1]);
                    return true;
                }
                return false;
            });
        }
        auto& transBucket=phaseBuckets[RP_Translucent];
        if(transBucket.size()>1)std::stable_sort(transBucket.begin(),transBucket.end(),[](const Draw& a,const Draw& b) {
            return a.distance>b.distance;
        });
        pk[7]=svcGetSystemTick();
        u64 tMesh=osGetTime();
        if(frame<=2)std::fprintf(stderr,"frame %u: GPU draws=%u\n",frame,totalDraws);
        cpuMs=float(double(svcGetSystemTick()-beginTick)/268111.856);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        inFrameCmd=true;
        tintValid=false;
        bindSceneProgram(false,false);
        u64 tBegin=osGetTime();
        C3D_CullFace(GPU_CULL_NONE);setStereo();
        auto* frameBuf=C3D_GetBufInfo();BufInfo_Init(frameBuf);BufInfo_Add(frameBuf,dynamic[slot],sizeof(Vertex),3,0x210);
        bool bufInfoFlushed=false,c3dStateDirty=true;
        u32 lastBufOffset=dynamicBufOffset[slot];
        auto setBufOffset=[&](u32 offset) {
            if(offset!=lastBufOffset) {
                frameBuf->buffers[0].offset=offset;lastBufOffset=offset;
                if(bufInfoFlushed && gpuCmdBuf)GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_OFFSET,offset);
            }
        };
        auto writeProjDirect=[&](const C3D_Mtx& m) {
            if(projection<0)return;
            if(gpuCmdBuf) {
                GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG,0x80000000u|u32(projection));
                GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA,reinterpret_cast<const u32*>(m.r),16);
                for(int i=0;i<4;++i)C3D_FVUnifDirty[GPU_VERTEX_SHADER][projection+i]=false;
            } else {
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projection,&m);
            }
        };
        auto writeStereoDirect=[&](float z,float w,float limit) {
            if(stereo<0)return;
            const C3D_FVec v=FVec4_New(z,w,limit,0.0f);
            if(gpuCmdBuf) {
                GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG,0x80000000u|u32(stereo));
                GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA,reinterpret_cast<const u32*>(&v),4);
                C3D_FVUnifDirty[GPU_VERTEX_SHADER][stereo]=false;
            } else {
                C3D_FVUnifSet(GPU_VERTEX_SHADER,stereo,z,w,limit,0.0f);
            }
        };
        auto drawElementsFast=[&](const uint16_t* indices,u32 idxPhysOffset,unsigned count) {
            if(!c3dStateDirty && bufInfoFlushed && idxPhysOffset!=~0u &&
               gpuCmdBuf && gpuCmdBufOffset+30u<=gpuCmdBufSize) {
                const u32 cmds[30]={
                    0x00000300u,GPUCMD_HEADER(0,0x2,GPUREG_PRIMITIVE_CONFIG),
                    0x00000001u,GPUCMD_HEADER(0,0xF,GPUREG_RESTART_PRIMITIVE),
                    idxPhysOffset|0x80000000u,GPUCMD_HEADER(0,0xF,GPUREG_INDEXBUFFER_CONFIG),
                    count,GPUCMD_HEADER(0,0xF,GPUREG_NUMVERTICES),
                    0x00000000u,GPUCMD_HEADER(0,0xF,GPUREG_VERTEX_OFFSET),
                    0x00000100u,GPUCMD_HEADER(0,0x2,0x0229),
                    0x00000100u,GPUCMD_HEADER(0,0x2,GPUREG_GEOSTAGE_CONFIG2),
                    0x00000000u,GPUCMD_HEADER(0,0x1,GPUREG_START_DRAW_FUNC0),
                    0x00000001u,GPUCMD_HEADER(0,0xF,GPUREG_DRAWELEMENTS),
                    0x00000001u,GPUCMD_HEADER(0,0x1,GPUREG_START_DRAW_FUNC0),
                    0x00000000u,GPUCMD_HEADER(0,0x2,0x0229),
                    0x00000000u,GPUCMD_HEADER(0,0x2,GPUREG_GEOSTAGE_CONFIG2),
                    0x00000001u,GPUCMD_HEADER(0,0xF,GPUREG_VTX_FUNC),
                    0x00000000u,GPUCMD_HEADER(0,0x8,GPUREG_PRIMITIVE_CONFIG),
                    0x00000000u,GPUCMD_HEADER(0,0x8,GPUREG_PRIMITIVE_CONFIG),
                };
                GPUCMD_AddRawCommands(cmds,30u);
            } else {
                C3D_DrawElements(GPU_TRIANGLES,count,C3D_UNSIGNED_SHORT,indices);
                c3dStateDirty=false;bufInfoFlushed=true;
            }
        };
        for(size_t i=0;i<shadowPasses.size();++i) {
            const auto& pass=shadowPasses[i];if(!pass.renderMap)continue;auto& map=shadowMaps[i];
            C3D_RenderTargetClear(map.target,C3D_CLEAR_ALL,0,0);C3D_FrameDrawOn(map.target);c3dStateDirty=true;
            C3D_Mtx transform=matrix(pass.projection);writeProjDirect(transform);
            setTint(1,1,1,pass.alpha);
            auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_RGB,GPU_CONSTANT);C3D_TexEnvColor(env,0xffffffff);
            C3D_TexEnvFunc(env,C3D_RGB,GPU_REPLACE);
            C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(true,GPU_GREATER,0);
            C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
            C3D_TexBind(0,&white);c3dStateDirty=true;
            for(unsigned s=0;s<pass.subCount;++s) {
                const auto& cs=casterSubs[pass.firstSub+s];
                setBufOffset(cs.bufOffset);
                C3D_DrawElements(GPU_TRIANGLES,cs.indexCount,C3D_UNSIGNED_SHORT,cs.indices);
                bufInfoFlushed=true;c3dStateDirty=false;
            }
            env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
            c3dStateDirty=true;
        }
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
        C3D_AlphaTest(true,GPU_GREATER,24);c3dStateDirty=true;
        const u32 skyClear=(u32(packByte(std::clamp(rgbaSky.r,0.0f,1.0f)))<<24)|
                           (u32(packByte(std::clamp(rgbaSky.g,0.0f,1.0f)))<<16)|
                           (u32(packByte(std::clamp(rgbaSky.b,0.0f,1.0f)))<<8)|0xffu;
        unsigned drawCalls=0,mergedCallCount=0;
        const float slider=(displayMode==2 || displayMode==6)?std::clamp(osGet3DSliderState(),0.f,1.f):0.f;
        const bool stereoActive=(displayMode==2 || displayMode==6) && slider>.001f;
        if(stereoActive!=stereoWasActive) {std::fprintf(stderr,"Stereo eyes: %d slider=%.2f\n",stereoActive?2:1,slider);stereoWasActive=stereoActive;}
        // As in Eyes of Gold, keep LCD mode fixed for the lifetime of these
        // targets. The physical slider selects visible stereo; it must not
        // reconfigure gfx while a Citro3D frame is being submitted.
        const float stereoReference=CaneStereoOption3ds(2) && g_pjt?glm::distance(g_pcm->pos,g_pjt->xf.posWorld):g_pcm->sNearClip*12;
        packetMvps.resize(canePackets.size());
        packetMvpValid.assign(canePackets.size(),0);
        struct StereoSlice {u32 start=0,end=0;};
        u32* replayCmdBuf=nullptr;
        StereoSlice replayOpaque{},replaySky{},replayRest{};
        unsigned replayEye0Draws=0,replayEye0Merged=0;
        bool replayValid=false;
        for(int eyeIndex=0;eyeIndex<(stereoActive?2:1);++eyeIndex) {
        C3D_RenderTarget* eyeTarget=postTarget?postTarget:(eyeIndex?rightTarget:target);
        C3D_RenderTargetClear(eyeTarget,C3D_CLEAR_ALL,skyClear,0);C3D_FrameDrawOn(eyeTarget);c3dStateDirty=true;
        if(postTarget) {C3D_SetViewport(0,0,displayMode==3?480:240,displayMode==3?800:400);c3dStateDirty=true;}
        unlitState();c3dStateDirty=true;
        bool lastAlphaEnable=true;GPU_TESTFUNC lastAlphaFunc=GPU_GREATER;int lastAlphaRef=0;
        BlendMode lastBlend=BlendMode::Alpha;
        auto setAlphaTest=[&](bool en,GPU_TESTFUNC fn,int ref) {
            if(en!=lastAlphaEnable || (en && (fn!=lastAlphaFunc || ref!=lastAlphaRef))) {
                C3D_AlphaTest(en,fn,ref);c3dStateDirty=true;
                lastAlphaEnable=en;lastAlphaFunc=fn;lastAlphaRef=ref;
            }
        };
        auto setBlend=[&](BlendMode mode) {
            if(mode!=lastBlend) {
                if(mode==BlendMode::Opaque) {
                    C3D_ColorLogicOp(GPU_LOGICOP_COPY);
                } else {
                    const bool add=(mode==BlendMode::Additive);
                    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,add?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
                }
                c3dStateDirty=true;
                lastBlend=mode;
            }
        };
        setAlphaTest(true,GPU_GREATER,24);
        CaneStereoWarp eyeWarp{};
        float skyStereoW=0;
        if(stereoActive) {
            auto& warp=eyeWarp;warp=CaneMakeStereoWarp(g_pcm->sNearClip,g_pcm->sFarClip,stereoReference,slider*CaneStereoStrength3ds()*.01f,eyeIndex);
            // Derive disparity from the actual projection depth row rather
            // than relying on an assumed handedness/depth convention.
            const float b=projectionMatrix.r[2].w;
            const float a=-projectionMatrix.r[2].z;
            const float convergence=std::max(g_pcm->sNearClip*3,stereoReference*1.5f*CaneStereoOption3ds(3)*.01f);
            const float k=(eyeIndex?1.f:-1.f)*stereoReference*.025f*slider*CaneStereoStrength3ds()*.01f;
            skyStereoW=-k/convergence;
            if(std::abs(b)>.000001f) {warp.z=k/b;warp.w=-k*(a/b+1/convergence);}
            warp.limit*=CaneStereoOption3ds(4)*.01f;
            bindSceneProgram(false,true);
            writeStereoDirect(warp.z,warp.w,warp.limit);
            c3dStateDirty=true;
        }
        const CanePacket* lastMaterial=nullptr;
        const C3D_Mtx* lastCamera=nullptr;
        bool lastWorldBaked=false;
        C3D_Tex* lastTex0=nullptr;
        C3D_Tex* lastTex1=nullptr;
        C3D_Tex* lastTex2=nullptr;
        int lastDepthWrite=-1;
        int currentTexMode=0;
        int lastCull=int(GPU_CULL_NONE);
        auto setCull=[&](GPU_CULLMODE mode) {
            if(int(mode)!=lastCull) {C3D_CullFace(mode);c3dStateDirty=true;lastCull=int(mode);}
        };
        auto setTexMode=[&](int mode) {
            if(mode==currentTexMode)return;
            c3dStateDirty=true;
            if(mode==0) {
                bindSceneProgram(false,sceneUseStereo);
                auto* env0=C3D_GetTexEnv(0);C3D_TexEnvInit(env0);
                C3D_TexEnvSrc(env0,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env0,C3D_Both,GPU_MODULATE);
                if(currentTexMode<0 || currentTexMode==2)C3D_TexEnvInit(C3D_GetTexEnv(2));
                if(currentTexMode<0 || currentTexMode>=1)C3D_TexEnvInit(C3D_GetTexEnv(1));
                if(lastTex1!=&white) {C3D_TexBind(1,&white);lastTex1=&white;}
                if(lastTex2!=&white) {C3D_TexBind(2,&white);lastTex2=&white;}
                if(currentTexMode<0)lastTex0=nullptr;
                currentTexMode=0;
                return;
            }
            bindSceneProgram(true,sceneUseStereo);
            if(currentTexMode!=1 && currentTexMode!=2) {
                auto* env0=C3D_GetTexEnv(0);C3D_TexEnvInit(env0);
                C3D_TexEnvSrc(env0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                C3D_TexEnvOpRgb(env0,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_R);
                C3D_TexEnvFunc(env0,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvSrc(env0,C3D_Alpha,GPU_TEXTURE1,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env0,C3D_Alpha,GPU_MODULATE);
                auto* env1=C3D_GetTexEnv(1);C3D_TexEnvInit(env1);
                C3D_TexEnvSrc(env1,C3D_RGB,GPU_TEXTURE1,GPU_PRIMARY_COLOR,GPU_PREVIOUS);
                C3D_TexEnvOpRgb(env1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_G,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(env1,C3D_RGB,GPU_MULTIPLY_ADD);
                C3D_TexEnvSrc(env1,C3D_Alpha,GPU_PREVIOUS);
                C3D_TexEnvFunc(env1,C3D_Alpha,GPU_REPLACE);
            }
            if(mode==1) {
                if(currentTexMode<0 || currentTexMode==2) {
                    C3D_TexEnvInit(C3D_GetTexEnv(2));
                    if(lastTex2!=&white) {C3D_TexBind(2,&white);lastTex2=&white;}
                }
                currentTexMode=1;
            } else {
                auto* env2=C3D_GetTexEnv(2);C3D_TexEnvInit(env2);
                C3D_TexEnvSrc(env2,C3D_RGB,GPU_TEXTURE2,GPU_PRIMARY_COLOR,GPU_PREVIOUS);
                C3D_TexEnvOpRgb(env2,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_B,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(env2,C3D_RGB,GPU_MULTIPLY_ADD);
                C3D_TexEnvSrc(env2,C3D_Alpha,GPU_PREVIOUS);
                C3D_TexEnvFunc(env2,C3D_Alpha,GPU_REPLACE);
                currentTexMode=2;
            }
        };
        auto drawMesh=[&](const Draw& draw,const C3D_Mtx& camera) {
            if(draw.material!=lastMaterial || &camera!=lastCamera || draw.worldBaked!=lastWorldBaked) {
                if(draw.worldBaked) {
                    if(!lastWorldBaked || &camera!=lastCamera)writeProjDirect(camera);
                } else {
                    if(&camera==&combined && draw.mvpIdx<packetMvps.size()) {
                        if(!packetMvpValid[draw.mvpIdx]) {
                            C3D_Mtx model=matrix(draw.material->model);
                            Mtx_Multiply(&packetMvps[draw.mvpIdx],&combined,&model);
                            packetMvpValid[draw.mvpIdx]=1;
                        }
                        writeProjDirect(packetMvps[draw.mvpIdx]);
                    } else {
                        C3D_Mtx model=matrix(draw.material->model),mvp;Mtx_Multiply(&mvp,&camera,&model);
                        writeProjDirect(mvp);
                    }
                }
                setTint(draw.tintDarken,draw.tintDarken,draw.tintDarken,draw.tintAlpha);
                lastMaterial=draw.material;lastCamera=&camera;lastWorldBaked=draw.worldBaked;
            }
            setBufOffset(draw.bufOffset);
            if(draw.phase==RP_ProjVolume) {
                setTexMode(0);lastTex0=nullptr;lastDepthWrite=-1;
                if(!draw.shader)return;
                bool additive=(draw.shader->grfshd&1)!=0;
                setAlphaTest(false,GPU_ALWAYS,0);
                C3D_DepthTest(true,GPU_GREATER,static_cast<GPU_WRITEMASK>(0));
                C3D_StencilTest(true,GPU_ALWAYS,128,128,128);
                C3D_StencilOp(GPU_STENCIL_ZERO,GPU_STENCIL_REPLACE,GPU_STENCIL_ZERO);
                C3D_CullFace(GPU_CULL_FRONT_CCW);C3D_DrawElements(GPU_TRIANGLES,draw.count,C3D_UNSIGNED_SHORT,draw.indices);
                C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_ZERO,GPU_STENCIL_KEEP);
                if(additive && (draw.shader->grfshd&2)) {
                    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);
                    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE,GPU_ONE,GPU_ZERO);
                    C3D_TexBind(0,draw.tex0?draw.tex0:&white);
                }
                C3D_CullFace(GPU_CULL_BACK_CCW);C3D_DrawElements(GPU_TRIANGLES,draw.count,C3D_UNSIGNED_SHORT,draw.indices);
                C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
                C3D_StencilTest(true,GPU_EQUAL,128,192,128);
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,additive?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
                C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
                auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
                glm::vec4 color=glm::clamp(draw.shader->rgbaVolume,glm::vec4(0),glm::vec4(1));color.r*=.5f;color.g*=.5f;color.b*=.5f;color.a*=draw.tintAlpha;
                C3D_TexEnvColor(env,(u32(color.r*255)|(u32(color.g*255)<<8)|(u32(color.b*255)<<16)|(u32(color.a*255)<<24)));
                C3D_CullFace(GPU_CULL_FRONT_CCW);C3D_DrawElements(GPU_TRIANGLES,draw.count,C3D_UNSIGNED_SHORT,draw.indices);
                C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
                // The draw consumed the dirty flag; restoring the CPU struct
                // alone would leave subsequent geometry using the volume tint.
                C3D_DirtyTexEnv(env);
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ZERO);
                lastBlend=BlendMode::Alpha;
                C3D_StencilTest(false,GPU_ALWAYS,0,255,255);C3D_CullFace(GPU_CULL_NONE);lastCull=int(GPU_CULL_NONE);currentTexMode=-1;
                setAlphaTest(true,GPU_GREATER,24);
                c3dStateDirty=true;bufInfoFlushed=true;
                drawCalls+=3;
                return;
            }
            if(draw.phase==RP_Background) {
                if(lastDepthWrite!=-3) {
                    C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);c3dStateDirty=true;
                    lastDepthWrite=-3;
                }
            } else if(draw.phase==RP_Foreground || draw.phase==RP_WorldMap) {
                if(lastDepthWrite!=-2) {
                    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);c3dStateDirty=true;
                    lastDepthWrite=-2;
                }
            } else if(draw.phase==RP_MurkClear || draw.phase==RP_MurkFill) {
                C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);c3dStateDirty=true;
                lastDepthWrite=-1;
            } else {
                int wantDepthWrite=draw.phase==RP_Translucent?0:1;
                if(wantDepthWrite!=lastDepthWrite) {
                    C3D_DepthTest(true,GPU_GREATER,wantDepthWrite?GPU_WRITE_ALL:GPU_WRITE_COLOR);c3dStateDirty=true;
                    lastDepthWrite=wantDepthWrite;
                }
            }
            setCull(draw.flipped?GPU_CULL_FRONT_CCW:GPU_CULL_BACK_CCW);
            setTexMode(draw.texMode);
            C3D_Tex* bt0=draw.tex0?draw.tex0:&white;
            if(bt0!=lastTex0) {C3D_TexBind(0,bt0);c3dStateDirty=true;lastTex0=bt0;}
            if(draw.texMode>=1) {
                C3D_Tex* bt1=draw.tex1?draw.tex1:&white;
                if(bt1!=lastTex1) {C3D_TexBind(1,bt1);c3dStateDirty=true;lastTex1=bt1;}
            }
            if(draw.texMode==2) {
                C3D_Tex* bt2=draw.tex2?draw.tex2:&white;
                if(bt2!=lastTex2) {C3D_TexBind(2,bt2);c3dStateDirty=true;lastTex2=bt2;}
            }
            switch(draw.kind) {
                case DrawKind::Opaque:
                    setBlend(BlendMode::Opaque);
                    setAlphaTest(false,GPU_ALWAYS,0);
                    drawElementsFast(draw.indices,draw.idxPhysOffset,draw.count);++drawCalls;
                    break;
                case DrawKind::BinaryCutout:
                    setBlend(BlendMode::Opaque);
                    setAlphaTest(true,GPU_GEQUAL,128);
                    drawElementsFast(draw.indices,draw.idxPhysOffset,draw.count);++drawCalls;
                    break;
                case DrawKind::TwoPassAlpha:
                case DrawKind::TwoPassAdditive:
                    // Match DrawGlob/DrawTranslucent: solid alpha writes depth;
                    // the soft fringe never writes it.
                    setBlend(draw.kind==DrawKind::TwoPassAdditive?BlendMode::Additive:BlendMode::Alpha);
                    if(lastDepthWrite!=1) {C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);c3dStateDirty=true;lastDepthWrite=1;}
                    setAlphaTest(true,GPU_GEQUAL,230);
                    drawElementsFast(draw.indices,draw.idxPhysOffset,draw.count);++drawCalls;
                    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);c3dStateDirty=true;lastDepthWrite=0;
                    setAlphaTest(true,GPU_GREATER,0);
                    drawElementsFast(draw.indices,draw.idxPhysOffset,draw.count);++drawCalls;
                    break;
                case DrawKind::OnePassAlpha:
                case DrawKind::OnePassAdditive:
                    setBlend(draw.kind==DrawKind::OnePassAdditive?BlendMode::Additive:BlendMode::Alpha);
                    if(lastDepthWrite!=0) {C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);c3dStateDirty=true;lastDepthWrite=0;}
                    setAlphaTest(true,GPU_GREATER,0);
                    drawElementsFast(draw.indices,draw.idxPhysOffset,draw.count);++drawCalls;
                    break;
                case DrawKind::StandardAlpha:
                default:
                    setBlend(BlendMode::Alpha);
                    setAlphaTest(true,GPU_GREATER,24);
                    drawElementsFast(draw.indices,draw.idxPhysOffset,draw.count);++drawCalls;
                    break;
            }
        };
        bool replayedEye=false;
        const bool recordEye0=(stereoActive && !postTarget && eyeIndex==0);
        if(stereoActive && !postTarget) {
            setBufOffset(dynamicBufOffset[slot]);
            C3D_TexBind(0,&white);lastTex0=&white;
            C3D_DepthTest(false,GPU_NEVER,static_cast<GPU_WRITEMASK>(0));
            C3D_DrawArrays(GPU_TRIANGLES,0,3);
            bufInfoFlushed=true;c3dStateDirty=false;lastDepthWrite=-1;
        }
        if(eyeIndex==1 && stereoActive && !postTarget && replayValid) {
            u32* curBuf=nullptr;u32 curSize=0,curOff=0;
            GPUCMD_GetBuffer(&curBuf,&curSize,&curOff);
            const u32 totalReplay=(replayOpaque.end-replayOpaque.start)+
                                  (replaySky.end-replaySky.start)+
                                  (replayRest.end-replayRest.start);
            if(curBuf==replayCmdBuf && curOff+totalReplay+2048u<=curSize) {
                if(replayOpaque.end>replayOpaque.start)
                    GPUCMD_AddRawCommands(replayCmdBuf+replayOpaque.start,replayOpaque.end-replayOpaque.start);
                if(replaySky.end>replaySky.start) {
                    writeStereoDirect(0.0f,skyStereoW,eyeWarp.limit);
                    GPUCMD_AddRawCommands(replayCmdBuf+replaySky.start,replaySky.end-replaySky.start);
                }
                if(replayRest.end>replayRest.start) {
                    writeStereoDirect(eyeWarp.z,eyeWarp.w,eyeWarp.limit);
                    GPUCMD_AddRawCommands(replayCmdBuf+replayRest.start,replayRest.end-replayRest.start);
                }
                drawCalls+=replayEye0Draws;mergedCallCount+=replayEye0Merged;
                boundProgram=nullptr;
                unlitState();
                C3D_TexBind(0,&white);
                C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);
                lastBufOffset=~0u;setBufOffset(dynamicBufOffset[slot]);
                tintValid=false;lastMaterial=nullptr;lastCamera=nullptr;lastWorldBaked=false;
                lastTex0=&white;lastTex1=&white;lastTex2=&white;lastDepthWrite=0;currentTexMode=0;
                lastCull=int(GPU_CULL_NONE);lastBlend=BlendMode::Alpha;
                lastAlphaEnable=true;lastAlphaFunc=GPU_GREATER;lastAlphaRef=0;
                c3dStateDirty=true;replayedEye=true;
            }
        }
        if(!replayedEye) {
        u32* recBase=nullptr;u32 recSize=0,recOff=0;
        const unsigned eye0DrawStart=drawCalls,eye0MergedStart=mergedCallCount;
        if(recordEye0) {
            GPUCMD_GetBuffer(&recBase,&recSize,&recOff);
            replayCmdBuf=recBase;replayOpaque.start=recOff;
        }
        static constexpr RP kRenderOrder[]={
            RP_DynamicTexture,RP_BlotContext,
            RP_Opaque,RP_Background,RP_Cutout,RP_CelBorder,
            RP_ProjVolume,RP_OpaqueAfterProjVolume,RP_CutoutAfterProjVolume,
            RP_CelBorderAfterProjVolume,RP_MurkClear,RP_MurkOpaque,RP_MurkFill,
            RP_Translucent,RP_TranslucentCelBorder,RP_Blip,RP_Foreground
        };
        for(RP rp:kRenderOrder) {
            if(recordEye0 && rp==RP_Background) {
                replayOpaque.end=gpuCmdBufOffset;
                writeStereoDirect(0.0f,skyStereoW,eyeWarp.limit);
                replaySky.start=gpuCmdBufOffset;
            } else if(stereoActive && !recordEye0 && rp==RP_Background) {
                writeStereoDirect(0.0f,skyStereoW,eyeWarp.limit);
            }
            if(!mergedDraws[rp].empty()) {
                if(lastDepthWrite!=1) {C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);c3dStateDirty=true;lastDepthWrite=1;}
                const bool cutoutBatch=(rp==RP_Cutout || rp==RP_CutoutAfterProjVolume);
                setBlend(BlendMode::Opaque);
                setAlphaTest(cutoutBatch,cutoutBatch?GPU_GEQUAL:GPU_ALWAYS,cutoutBatch?128:0);
                writeProjDirect(combined);
                setTint(1,1,1,1);
                lastMaterial=nullptr;lastCamera=&combined;lastWorldBaked=true;
                setCull(GPU_CULL_BACK_CCW);
                for(const auto& md:mergedDraws[rp]) {
                    setBufOffset(worldChunks[md.chunkIdx].bufOffset);
                    setTexMode(md.texMode);
                    C3D_Tex* bt0=md.tex0?md.tex0:&white;
                    if(bt0!=lastTex0) {C3D_TexBind(0,bt0);c3dStateDirty=true;lastTex0=bt0;}
                    if(md.texMode>=1) {
                        C3D_Tex* bt1=md.tex1?md.tex1:&white;
                        if(bt1!=lastTex1) {C3D_TexBind(1,bt1);c3dStateDirty=true;lastTex1=bt1;}
                    }
                    if(md.texMode==2) {
                        C3D_Tex* bt2=md.tex2?md.tex2:&white;
                        if(bt2!=lastTex2) {C3D_TexBind(2,bt2);c3dStateDirty=true;lastTex2=bt2;}
                    }
                    const u32 idxPhys=batchIndicesPhys[slot]!=~0u?(batchIndicesPhys[slot]+u32(md.indexOffset*sizeof(uint16_t))):~0u;
                    drawElementsFast(batchIndices[slot]+md.indexOffset,idxPhys,md.indexCount);
                    ++drawCalls;++mergedCallCount;
                }
            }
            if(!phaseBuckets[rp].empty()) {
                const C3D_Mtx& phaseCam=(rp==RP_Background)?skyCombined:combined;
                for(const auto& draw:phaseBuckets[rp])drawMesh(draw,phaseCam);
            }
            if(recordEye0 && rp==RP_Background) {
                replaySky.end=gpuCmdBufOffset;
                writeStereoDirect(eyeWarp.z,eyeWarp.w,eyeWarp.limit);
                replayRest.start=gpuCmdBufOffset;
            } else if(stereoActive && !recordEye0 && rp==RP_Background) {
                writeStereoDirect(eyeWarp.z,eyeWarp.w,eyeWarp.limit);
            }
        }
        setTexMode(0);setCull(GPU_CULL_NONE);setBlend(BlendMode::Alpha);
        writeProjDirect(combined);
        setTint(1,1,1,1);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);c3dStateDirty=true;lastDepthWrite=0;
        setAlphaTest(true,GPU_GREATER,0);
        setBufOffset(dynamicBufOffset[slot]);
        for(size_t i=0;i<shadowPasses.size();++i) {
            const auto& pass=shadowPasses[i];if(!pass.overlayCount)continue;
            C3D_TexBind(0,&shadowMaps[i].texture);C3D_DrawArrays(GPU_TRIANGLES,pass.overlayFirst,pass.overlayCount);
            c3dStateDirty=false;bufInfoFlushed=true;
        }
        setTint(1,1,1,1);
        if(outlineVertCount) {
            C3D_Mtx identity;Mtx_Identity(&identity);
            writeProjDirect(identity);
            C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);C3D_TexBind(0,&white);
            setBufOffset(dynamicBufOffset[slot]);
            C3D_DrawArrays(GPU_TRIANGLES,outlineFirst,outlineVertCount);
            c3dStateDirty=false;bufInfoFlushed=true;
            writeProjDirect(combined);
        }
        C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);c3dStateDirty=true;lastDepthWrite=0;
        setBufOffset(dynamicBufOffset[slot]);
        for(const auto& batch:particleDraws) {
            setBlend(batch.additive?BlendMode::Additive:BlendMode::Alpha);
            C3D_TexBind(0,batch.tex?batch.tex:&white);c3dStateDirty=true;
            if(batch.additive) {
                if(lastDepthWrite!=0) {C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);lastDepthWrite=0;}
                setAlphaTest(true,GPU_GREATER,0);
                C3D_DrawArrays(GPU_TRIANGLES,batch.first,batch.count);
                c3dStateDirty=false;bufInfoFlushed=true;
            } else if(batch.maxAlpha>=128) {
                // BLIP alpha passes use 128/255, unlike mesh cutouts (0.9).
                // The complementary tests avoid drawing smoke twice.
                C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
                setAlphaTest(true,GPU_GEQUAL,128);
                C3D_DrawArrays(GPU_TRIANGLES,batch.first,batch.count);
                C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);lastDepthWrite=0;
                setAlphaTest(true,GPU_LESS,128);
                C3D_DrawArrays(GPU_TRIANGLES,batch.first,batch.count);
                c3dStateDirty=false;bufInfoFlushed=true;
            } else {
                if(lastDepthWrite!=0) {C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);lastDepthWrite=0;}
                setAlphaTest(true,GPU_GREATER,0);
                C3D_DrawArrays(GPU_TRIANGLES,batch.first,batch.count);
                c3dStateDirty=false;bufInfoFlushed=true;
            }
        }
        setAlphaTest(true,GPU_GREATER,0);
        if(lastDepthWrite!=0) {C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);c3dStateDirty=true;lastDepthWrite=0;}
        setBlend(BlendMode::Alpha);
        if(recordEye0) {
            replayRest.end=gpuCmdBufOffset;
            u32* recEndBuf=nullptr;GPUCMD_GetBuffer(&recEndBuf,nullptr,nullptr);
            replayEye0Draws=drawCalls-eye0DrawStart;
            replayEye0Merged=mergedCallCount-eye0MergedStart;
            replayValid=(recEndBuf==replayCmdBuf &&
                         replayOpaque.end>=replayOpaque.start && ((replayOpaque.end-replayOpaque.start)&1u)==0 &&
                         replaySky.end>=replaySky.start && ((replaySky.end-replaySky.start)&1u)==0 &&
                         replayRest.end>=replayRest.start && ((replayRest.end-replayRest.start)&1u)==0);
        }
        } // !replayedEye
        if(!phaseBuckets[RP_WorldMap].empty()) {
            unlitState();c3dStateDirty=true;
            lastBlend=BlendMode::Alpha;lastAlphaEnable=true;lastAlphaFunc=GPU_GREATER;lastAlphaRef=0;
            C3D_Mtx ortho;Mtx_OrthoTilt(&ortho,0,400,240,0,0,1,true);
            ortho.r[2]=FVec4_New(0,0,0,-.5f);
            writeProjDirect(ortho);
            setTint(1,1,1,1);
            C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);lastDepthWrite=-2;c3dStateDirty=true;
            setBufOffset(dynamicBufOffset[slot]);
            C3D_TexBind(0,&white);C3D_DrawArrays(GPU_TRIANGLES,wmOverlayFirst,12);
            c3dStateDirty=false;bufInfoFlushed=true;
            C3D_Mtx wmProj,wmCombined;
            Mtx_PerspTilt(&wmProj,1.0f,C3D_AspectRatioTop,std::max(.01f,g_pcm->sNearClip),std::max(g_pcm->sNearClip+1.0f,g_pcm->sFarClip),false);
            Mtx_Multiply(&wmCombined,&wmProj,&view);
            lastMaterial=nullptr;lastCamera=nullptr;lastWorldBaked=false;
            lastTex0=nullptr;lastTex1=nullptr;lastTex2=nullptr;lastDepthWrite=-1;
            currentTexMode=0;lastCull=int(GPU_CULL_NONE);c3dStateDirty=true;
            for(const auto& draw:phaseBuckets[RP_WorldMap])drawMesh(draw,wmCombined);
            setTexMode(0);setCull(GPU_CULL_NONE);setBlend(BlendMode::Alpha);
        }
        setAlphaTest(true,GPU_GREATER,0);
        auto drawHudLayer=[&](bool bottom) {
            unlitState();c3dStateDirty=true;
            lastBlend=BlendMode::Alpha;lastAlphaEnable=true;lastAlphaFunc=GPU_GREATER;lastAlphaRef=0;
            if(stereoActive && !bottom) {
                bindSceneProgram(false,true);
                writeStereoDirect(0,(eyeIndex?1.f:-1.f)*(CaneStereoOption3ds(5)-50)*.0004f*slider,.025f);
            }
            C3D_Mtx ortho;Mtx_OrthoTilt(&ortho,0,bottom?320:400,240,0,0,1,true);
            // PICA's visible clip-depth interval is [-w,0]. Keep 2D
            // glyphs inside it rather than exactly on the near clip plane.
            ortho.r[2]=FVec4_New(0,0,0,-.5f);
            writeProjDirect(ortho);
            setTint(1,1,1,1);
            C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);lastDepthWrite=-2;c3dStateDirty=true;
            setBufOffset(dynamicBufOffset[slot]);
            unsigned tvStencil=0;
            for(const auto& hud:hudDraws) {
                if((hud.layer!=CaneHudLayer::Top)!=bottom)continue;
                setBlend(hud.additive?BlendMode::Additive:BlendMode::Alpha);
                if(hud.tv) {
                    // Stamp the original animated curved aperture and clear
                    // depth only there, leaving the level outside it intact.
                    ++tvStencil;unlitState();c3dStateDirty=true;
                    lastBlend=BlendMode::Alpha;lastAlphaEnable=true;lastAlphaFunc=GPU_GREATER;lastAlphaRef=0;
                    C3D_Mtx farOrtho=ortho;farOrtho.r[2]=FVec4_New(0,0,0,-.0001f);
                    writeProjDirect(farOrtho);
                    setTint(1,1,1,1);
                    C3D_StencilTest(true,GPU_ALWAYS,tvStencil,255,255);
                    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE);
                    C3D_DepthTest(true,GPU_ALWAYS,GPU_WRITE_ALL);
                    C3D_TexBind(0,&white);C3D_DrawArrays(GPU_TRIANGLES,hud.first,hud.count);
                    C3D_StencilTest(true,GPU_EQUAL,tvStencil,255,0);
                    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
                    C3D_Mtx tvProjection,tvCamera;
                    Mtx_PerspTilt(&tvProjection,hud.tv->pspeakerDraw->radFov,1280.0f/720.0f,std::max(.01f,g_pcm->sNearClip),g_pcm->sFarClip,false);
                    Mtx_Multiply(&tvCamera,&tvProjection,&view);
                    if(bottom) {
                        // Apply the same screen-space placement to the 3D
                        // speaker and its original animated stencil aperture.
                        C3D_Mtx placement,placed;Mtx_Identity(&placement);
                        placement.r[0].x=hud.scale;
                        placement.r[0].w=1-hud.scale-2*hud.dy/240.f;
                        placement.r[1].y=hud.scale*400.f/320.f;
                        placement.r[1].w=1-hud.scale*400.f/320.f-2*hud.dx/320.f;
                        Mtx_Multiply(&placed,&placement,&tvCamera);tvCamera=placed;
                    }
                    lastMaterial=nullptr;lastCamera=nullptr;lastWorldBaked=false;lastTex0=nullptr;lastTex1=nullptr;lastTex2=nullptr;lastDepthWrite=-1;
                    currentTexMode=0;lastCull=int(GPU_CULL_NONE);c3dStateDirty=true;
                    for(const auto& draw:tvDraws)if(draw.material->tv==hud.tv)drawMesh(draw,tvCamera);
                    currentTexMode=0;lastCull=int(GPU_CULL_NONE);
                    unlitState();c3dStateDirty=true;
                    lastBlend=BlendMode::Alpha;lastAlphaEnable=true;lastAlphaFunc=GPU_GREATER;lastAlphaRef=0;
                    if(stereoActive && !bottom) {
                        bindSceneProgram(false,true);
                        writeStereoDirect(0,(eyeIndex?1.f:-1.f)*(CaneStereoOption3ds(5)-50)*.0004f*slider,.025f);
                    }
                    writeProjDirect(ortho);
                    setTint(1,1,1,1);
                    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);lastDepthWrite=-2;c3dStateDirty=true;
                    setBufOffset(dynamicBufOffset[slot]);
                } else {C3D_TexBind(0,hud.tex?hud.tex:&white);C3D_DrawArrays(GPU_TRIANGLES,hud.first,hud.count);c3dStateDirty=false;bufInfoFlushed=true;}
            }
            setBlend(BlendMode::Alpha);
        };
        if(!hudDraws.empty())drawHudLayer(false);
        if(postTarget) {
            C3D_FrameSplit(0);
            C3D_SetViewport(0,0,240,400);
            C3D_FrameDrawOn(target);
            unlitState();c3dStateDirty=true;tintValid=false;
            bindProgram(&program);
            C3D_Mtx postOrtho;Mtx_OrthoTilt(&postOrtho,0,400,240,0,0,1,true);
            postOrtho.r[2]=FVec4_New(0,0,0,-.5f);
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projection,&postOrtho);
            setTint(1,1,1,1);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
            C3D_AlphaTest(false,GPU_ALWAYS,0);
            C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
            C3D_TexSetFilter(&postTexture,GPU_LINEAR,GPU_LINEAR);
            C3D_TexBind(0,&postTexture);C3D_TexBind(1,&postTexture);C3D_TexBind(2,&postTexture);
            const float maxU=displayMode==3?480.0f/512.0f:240.0f/256.0f;
            const float maxV=displayMode==3?800.0f/1024.0f:400.0f/512.0f;
            const float du=1.0f/(displayMode==3?512.0f:256.0f);
            // Never overwrite scene vertices while the GPU is using them.
            auto* q=postQuad;
            const float xy[6][2]={{0,0},{400,0},{0,240},{0,240},{400,0},{400,240}};
            // The GPU framebuffer is portrait/tilted (Mtx_OrthoTilt maps x=0->+1/maxV,
            // x=400->-1/0, y=0->+1/maxU, y=240->-1/0). Undo both rotation and V polarity
            // consistently for every postTarget mode (SSAA and PICA-SR).
            const float uv[6][2]={{maxU,maxV},{maxU,0},{0,maxV},{0,maxV},{maxU,0},{0,0}};
            for(unsigned i=0;i<6;++i) {
                q[i].position[0]=xy[i][0];
                q[i].position[1]=xy[i][1];q[i].position[2]=0;
                std::memset(q[i].color,255,4);q[i].uv[0]=uv[i][0];q[i].uv[1]=uv[i][1];
            }
            GSPGPU_FlushDataCache(q,sizeof(Vertex)*6);
            frameBuf->buffers[0].offset=postQuadBufOffset;
            lastBufOffset=postQuadBufOffset;
            C3D_SetBufInfo(frameBuf);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,filterOffset,du,0,0,0);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,filterOffsetNegative,-du,0,0,0);
            if(displayMode==4)fsr3dsTevKernel(FSR3DS_KERNEL_AA_EDGE_APPLY,160);
            else if(displayMode==5)fsr3dsTevKernel(FSR3DS_KERNEL_LITE,0);
            else fsr3dsTevModulate();
            C3D_DrawArrays(GPU_TRIANGLES,0,6);bufInfoFlushed=true;
            unlitState();c3dStateDirty=true;
            lastBlend=BlendMode::Alpha;lastAlphaEnable=true;lastAlphaFunc=GPU_GREATER;lastAlphaRef=0;
            C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_COLOR);
        }
        if(bottomOutput && eyeIndex==(stereoActive?1:0)) {
            C3D_RenderTargetClear(bottomTarget,C3D_CLEAR_ALL,0x03070eff,0);
            C3D_FrameDrawOn(bottomTarget);c3dStateDirty=true;
            drawHudLayer(true);
        }
        } // eye pass; simulation, animation, culling and shadows ran once.
        setStereo();
        C3D_AlphaTest(true,GPU_GREATER,24);
        inFrameCmd=false;
        u64 tDrawEnd=osGetTime();
        C3D_FrameEnd(0);u64 tEnd=osGetTime();frameMs=float(tEnd-begin);gpuMs=C3D_GetDrawingTime();drawCmdMs=C3D_GetProcessingTime();drawCallCount=drawCalls;
        if(frame%120==0)std::fprintf(stderr,"Texture memory: vram=%lu/%luKiB linear=%luKiB ETC1=%u saved=%luKiB targets=%dKiB shadows=%uKiB\n",
            (unsigned long)(vramTextureBytes/1024),(unsigned long)(vramTextureBudget(displayMode)/1024),(unsigned long)(fcramTextureBytes/1024),etcTexCount,(unsigned long)(textureSavedBytes/1024),displayMode==0?750:displayMode==3?3822:displayMode==6?3000:1500,unsigned(std::count_if(std::begin(shadowMaps),std::end(shadowMaps),[](const ShadowMap& map){return map.texture.data!=nullptr;}))*shadowSize*shadowSize*6/1024);
        if(frame%120==0)std::fprintf(stderr,"Profile ms: upd=%.1f sw=%lu prep=%lu begin=%lu draw=%lu end=%lu dynVerts=%lu draws=%u merged=%u calls=%u flat=%u/%u/%u | ui=%.1f blots=%.1f setup=%.1f w1=%.1f shset=%.1f w2=%.1f fin=%.1f wk=[%u:%.1f,%u:%.1f] vram=%u/%u\n",double(updateMs),(unsigned long)(tSw-t0),(unsigned long)(tMesh-tSw),(unsigned long)(tBegin-tMesh),(unsigned long)(tDrawEnd-tBegin),(unsigned long)(tEnd-tDrawEnd),(unsigned long)used,totalDraws,mergedCallCount,drawCalls,animFlattens,movedFlattens,newFlattens,double(pk[1]-pk[0])/268111.856,double(pk[2]-pk[1])/268111.856,double(pk[3]-pk[2])/268111.856,double(pk[4]-pk[3])/268111.856,double(pk[5]-pk[4])/268111.856,double(pk[6]-pk[5])/268111.856,double(pk[7]-pk[6])/268111.856,workerCount>0?workers[0].jobsDone:0u,workerCount>0?double(workers[0].busyTicks)/268111.856:0.0,workerCount>1?workers[1].jobsDone:0u,workerCount>1?double(workers[1].busyTicks)/268111.856:0.0,vramTexCount,vramTexCount+fcramTexCount);
        return packetCount;
    }
    void showLoading(const char* text="Loading...") {
        if(!g_psw || !g_pcm)return;
        caneLoadingText=text;caneLoading=true;draw();C3D_FrameSync();caneLoading=false;
    }
    void resetWorld() {
        C3D_FrameSync();
        for(auto& wc:worldChunks)if(wc.vertices)linearFree(wc.vertices);
        worldChunks.clear();worldChunks.shrink_to_fit();
        for(auto& lc:localChunks)if(lc.vertices)linearFree(lc.vertices);
        localChunks.clear();localChunks.shrink_to_fit();
        localFreeList.clear();localFreeList.shrink_to_fit();
        for(auto& gc:globCachePool)for(auto& sc:gc.subs)if(sc.localOwned && sc.localVerts)linearFree(sc.localVerts);
        liveLightCache.forEach([](const LiveKey&,LiveEntry& le) {
            for(int b=0;b<2;++b)if(le.owned[b] && le.verts[b])linearFree(le.verts[b]);
        });
        liveLightCache.clear(true);
        liveDecisions.clear();ownedLiveFlush.clear();
        batchKeyMap.clear(true);batchKeyTable.clear();bucketHead.clear();entryNext.clear();touchedKeys.clear();
        indexBuffers.forEach([](const SUBGLOB*,IndexBuf& buf) {if(buf.data)linearFree(buf.data);});
        globCachePool.clear();globCacheFree.clear();std::vector<uint32_t>().swap(globCacheFree);
        globCacheMap.clear(true);
        indexBuffers.clear(true);
        celCachePool.clear();celCacheMap.clear(true);
        meshBytes=0;vramTexCount=0;fcramTexCount=0;etcTexCount=0;vramTextureBytes=fcramTextureBytes=textureSavedBytes=0;
        C3D_TexBind(0,&white);C3D_TexBind(1,&white);C3D_TexBind(2,&white);
        for(auto& item:textures)if(item.loaded)C3D_TexDelete(&item.tex);
        if(newButtonTexture.data)C3D_TexDelete(&newButtonTexture);newButtonTexture={};buttonImage=-1;
        for(auto& map:shadowMaps) {if(map.target)C3D_RenderTargetDelete(map.target);if(map.texture.data)C3D_TexDelete(&map.texture);map={};}
        textures.clear();textures.shrink_to_fit();
        preparedImages.clear();preparedImages.shrink_to_fit();
        glTexToImage.clear(true);
        for(int i=0;i<RP_Max;++i) {phaseBuckets[i].clear();staticBatches[i].clear();mergedDraws[i].clear();}
        for(auto& cv:casterDynVerts)cv.clear();
        tvDraws.clear();fadedWorldDraws.clear();shadowReceivers.clear();
        shadowPasses.clear();casterSubs.clear();particleDraws.clear();hudDraws.clear();
        celJobs.clear();celJobVerts.clear();animJobs.clear();animDrawSlots.clear();allBoneNormMats.clear();hudJobs.clear();casterJobs.clear();
        particleJobs.clear();particleJobVerts.clear();particleJobDraws.clear();
        shadowJobOut.clear();
        canePackets.clear();caneCelPackets.clear();caneParticles.clear();caneCasters.clear();caneDynamicLights.clear();caneTvContext=nullptr;
        CaneClearHudPool();frame=0;
    }
    ~CaneRuntimeGraphics() {
        if(started)C3D_FrameSync();
        caneBottomAvailable=caneBottomActive=false;
        if(bottomConsole && !bottomConsoleShadow.empty()) {
            bottomConsole->frameBuffer=reinterpret_cast<u16*>(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,nullptr,nullptr));
            std::memcpy(bottomConsole->frameBuffer,bottomConsoleShadow.data(),bottomConsoleShadow.size()*sizeof(u16));
            GSPGPU_FlushDataCache(bottomConsole->frameBuffer,bottomConsoleShadow.size()*sizeof(u16));
        }
        if(bottomTarget) {C3D_FrameSync();C3D_RenderTargetDetachOutput(bottomTarget);C3D_RenderTargetDelete(bottomTarget);bottomTarget=nullptr;}
        shutdownWorkers();
        if(!started)return;resetWorld();
        if(postTarget) {C3D_RenderTargetDelete(postTarget);postTarget=nullptr;}
        if(postTextureReady) {C3D_TexDelete(&postTexture);postTextureReady=false;}
        if(rightTarget) {C3D_RenderTargetDetachOutput(rightTarget);C3D_RenderTargetDelete(rightTarget);rightTarget=nullptr;}
        if(target) {C3D_RenderTargetDetachOutput(target);C3D_RenderTargetDelete(target);target=nullptr;}
        for(auto* pointer:dynamic)if(pointer)linearFree(pointer);
        for(auto* pointer:batchIndices)if(pointer)linearFree(pointer);
        if(texStage)linearFree(texStage);
        if(postQuad)linearFree(postQuad);
        if(flat1ProgramStarted)shaderProgramFree(&flat1Program);
        if(flat1Shader)DVLB_Free(flat1Shader);
        if(stereo1ProgramStarted)shaderProgramFree(&stereo1Program);
        if(stereo1Shader)DVLB_Free(stereo1Shader);
        if(movieProgramStarted)shaderProgramFree(&movieProgram);
        if(movieShader)DVLB_Free(movieShader);
        C3D_TexDelete(&white);
        if(programStarted)shaderProgramFree(&program);if(shader)DVLB_Free(shader);C3D_Fini();
    }
};
