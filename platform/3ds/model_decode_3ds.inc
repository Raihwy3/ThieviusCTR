// PCBRX uses a fixed little-endian wire layout, not the GLM/native layout.
// Batch the records while expanding 16-bit bone indices explicitly.
#include <chrono>
#include <cstdio>
namespace cane_model_decode {
struct Stats {uint64_t vertices=0,edges=0,poses=0;long long micros=0;};
static Stats stats;
struct LoadScope {
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    ~LoadScope() {stats.micros+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();}
};
struct WireVertex { float attributes[12];uint16_t bones[4];float weights[4]; };
struct WireCel { float position[4];uint16_t bones[4];float weights[4]; };
struct WirePose { glm::vec4 position,normal; };
static_assert(sizeof(WireVertex)==72 && offsetof(WireVertex,weights)==56);
static_assert(sizeof(WireCel)==40 && offsetof(WireCel,weights)==24);
static_assert(sizeof(glm::vec4)==16 && sizeof(WirePose)==32);
static_assert(offsetof(VERTICE,boneIndices)==48 && offsetof(VERTICE,boneWeights)==64);

static void vertices(CBinaryInputStream* input,std::vector<VERTICE>& output,size_t count) {
    stats.vertices+=count;
    if(count>input->Remaining()/sizeof(WireVertex))throw std::runtime_error("Truncated PCBRX vertices");
    output.resize(count);
    WireVertex batch[128];
    for(size_t start=0;start<count;) {
        const auto n=std::min<size_t>(128,count-start);
        input->ReadBytes(batch,n*sizeof(WireVertex));
        for(size_t i=0;i<n;++i) {
            auto& vertex=output[start+i];const auto& wire=batch[i];
            std::memcpy(&vertex.pos,wire.attributes,48);
            vertex.boneIndices=glm::uvec4(wire.bones[0],wire.bones[1],wire.bones[2],wire.bones[3]);
            std::memcpy(&vertex.boneWeights,wire.weights,16);
        }
        start+=n;
    }
}
static void poses(CBinaryInputStream* input,std::vector<glm::vec4>& positions,std::vector<glm::vec4>& normals,size_t count) {
    stats.poses+=count;
    if(count>input->Remaining()/sizeof(WirePose))throw std::runtime_error("Truncated PCBRX poses");
    const auto start=positions.size();positions.resize(start+count);normals.resize(start+count);
    WirePose batch[128];
    for(size_t done=0;done<count;) {
        const auto n=std::min<size_t>(128,count-done);input->ReadBytes(batch,n*sizeof(WirePose));
        for(size_t i=0;i<n;++i) {positions[start+done+i]=batch[i].position;normals[start+done+i]=batch[i].normal;}
        done+=n;
    }
}
static void edges(CBinaryInputStream* input,std::vector<CELPOINT>& output,size_t count) {
    stats.edges+=count;
    if(count>input->Remaining()/sizeof(WireCel))throw std::runtime_error("Truncated PCBRX cel edges");
    const auto start=output.size();output.resize(start+count);
    WireCel batch[128];
    for(size_t done=0;done<count;) {
        const auto n=std::min<size_t>(128,count-done);input->ReadBytes(batch,n*sizeof(WireCel));
        for(size_t i=0;i<n;++i) {
            auto& point=output[start+done+i];const auto& wire=batch[i];
            std::memcpy(&point.position,wire.position,16);
            point.boneIndices=glm::uvec4(wire.bones[0],wire.bones[1],wire.bones[2],wire.bones[3]);
            std::memcpy(&point.boneWeights,wire.weights,16);
        }
        done+=n;
    }
}
}
void CaneResetModelLoadStats3ds() {cane_model_decode::stats={};}
void CaneReportModelLoadStats3ds() {
    const auto& s=cane_model_decode::stats;
    std::fprintf(stderr,"Model loading: models=%lld ms vertices=%llu cel-points=%llu pose-pairs=%llu\n",
        s.micros/1000,(unsigned long long)s.vertices,(unsigned long long)s.edges,(unsigned long long)s.poses);
}
