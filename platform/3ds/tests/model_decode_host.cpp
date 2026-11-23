#include "glob.h"
#include "../model_decode_3ds.inc"
#include <cassert>
#include <iostream>

int main() {
    using namespace cane_model_decode;
    std::vector<WireVertex> source(257);
    for(size_t i=0;i<source.size();++i) {
        for(unsigned j=0;j<12;++j)source[i].attributes[j]=float(i*13+j)*.125f;
        for(unsigned j=0;j<4;++j) {source[i].bones[j]=uint16_t(i+j*20000);source[i].weights[j]=float(j)*.25f;}
    }
    CBinaryInputStream fast,reference;
    fast.OpenMemory(int(source.size()*sizeof(WireVertex)),source.data());
    reference.OpenMemory(int(source.size()*sizeof(WireVertex)),source.data());
    std::vector<VERTICE> decoded;vertices(&fast,decoded,source.size());
    for(const auto& vertex:decoded) {
        assert(vertex.pos==reference.ReadVector());assert(vertex.normal==reference.ReadVector());
        assert(vertex.color==reference.ReadVector4());assert(vertex.uv==reference.ReadVector2());
        for(unsigned j=0;j<4;++j)assert(vertex.boneIndices[j]==reference.U16Read());
        assert(vertex.boneWeights==reference.ReadVector4());
    }
    assert(fast.GetOffset()==reference.GetOffset());
    std::vector<WireCel> cel(129);
    for(size_t i=0;i<cel.size();++i)for(unsigned j=0;j<4;++j) {
        cel[i].position[j]=float(i+j);cel[i].bones[j]=uint16_t(i+j*17000);cel[i].weights[j]=float(j)*.25f;
    }
    fast.OpenMemory(int(cel.size()*sizeof(WireCel)),cel.data());
    reference.OpenMemory(int(cel.size()*sizeof(WireCel)),cel.data());
    std::vector<CELPOINT> points;edges(&fast,points,cel.size());
    for(const auto& point:points) {
        assert(point.position==reference.ReadVector4());
        for(unsigned j=0;j<4;++j)assert(point.boneIndices[j]==reference.U16Read());
        assert(point.boneWeights==reference.ReadVector4());
    }
    std::vector<WirePose> pose(257);
    for(size_t i=0;i<pose.size();++i) {pose[i].position=glm::vec4(float(i));pose[i].normal=glm::vec4(float(i)+.5f);}
    fast.OpenMemory(int(pose.size()*sizeof(WirePose)),pose.data());
    std::vector<glm::vec4> positions(1,glm::vec4(-1)),normals(1,glm::vec4(-2));
    poses(&fast,positions,normals,pose.size());
    for(size_t i=0;i<pose.size();++i) {assert(positions[i+1]==pose[i].position);assert(normals[i+1]==pose[i].normal);}
    assert(positions[0]==glm::vec4(-1));
    fast.OpenMemory(sizeof(WireVertex)-1,source.data());
    bool rejected=false;try {vertices(&fast,decoded,1);}catch(const std::runtime_error&) {rejected=true;}
    assert(rejected);
    std::cout<<"PASS batched vertex/cel/pose decoding matches scalar wire reader, bone expansion and truncation checks\n";
}
