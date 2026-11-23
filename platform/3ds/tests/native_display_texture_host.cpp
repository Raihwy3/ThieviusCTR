#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <unistd.h>
#include "../native_display_math.inc"
#include "../native_texture_etc.inc"
int main() {
    constexpr float n=100,f=20000,ref=1000,convergence=ref*1.5f;
    const float a=n/(n-f),b=n*f/(n-f);
    for(int eye=0;eye<2;++eye)for(float distance:{300.f,1000.f,convergence,5000.f}) {
        const auto s=CaneMakeStereoWarp(n,f,ref,1,eye);
        const float z=-a*distance+b;
        const float actual=s.z*z+s.w*distance;
        // Independent parallel-eye camera translation plus off-axis shift.
        const float expected=(eye?1.f:-1.f)*ref*.025f*(1-distance/convergence);
        assert(std::abs(actual-expected)<.0001f);
        // Clip-space outlines have W=1; disparity must still match the mesh.
        assert(std::abs((s.z*(z/distance)+s.w)-actual/distance)<.000001f);
        const auto other=CaneMakeStereoWarp(n,f,ref,1,1-eye);
        assert(std::abs(actual+other.z*z+other.w*distance)<.0001f);
    }
    const auto off=CaneMakeStereoWarp(n,f,ref,0,0);assert(off.z==0 && off.w==0);
    assert(off.limit==0);
    const auto half=CaneMakeStereoWarp(n,f,ref,.5f,0);assert(std::abs(half.limit-.0125f)<.000001f);
    assert(std::abs(half.z-CaneMakeStereoWarp(n,f,ref,1,0).z*.5f)<.000001f);
    const auto invalid=CaneMakeStereoWarp(0,0,ref,1,0);assert(invalid.z==0 && invalid.w==0);
    // A coloured 4x4 tile in each quadrant catches ETC1 channel and tile order.
    std::vector<uint8_t> rgb(8*8*3);uint8_t encoded[32]{};
    const int colors[4][3]={{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)for(int c=0;c<3;++c)
        rgb[(y*8+x)*3+c]=colors[(y/4)*2+x/4][c];
    cane_texture_etc::encodeFrame(rgb.data(),encoded,8,8);
    for(int tile=0;tile<4;++tile) {
        uint64_t block=0;std::memcpy(&block,encoded+tile*8,8);
        const uint32_t hi=uint32_t(block>>32),lo=uint32_t(block);
        const bool diff=hi&2;
        for(int x=0;x<4;++x)for(int y=0;y<4;++y) {
            const int half=(hi&1)?y/2:x/2;
            const int table=(hi>>(half?2:5))&7;
            const int bit=x*4+y,selector=((lo>>bit)&1)|(((lo>>(bit+16))&1)<<1);
            for(int c=0;c<3;++c) {
                int base;
                if(diff) {
                    const int v=(hi>>(27-c*8))&31;
                    int delta=(hi>>(24-c*8))&7;if(delta&4)delta-=8;
                    const int q=v+(half?delta:0);base=(q<<3)|(q>>2);
                } else base=((hi>>(28-c*8-half*4))&15)*17;
                const int decoded=std::clamp(base+cane_texture_etc::kTable[table][selector],0,255);
                assert(std::abs(decoded-colors[tile][c])<=8);
            }
        }
    }
    const auto originalDir=std::filesystem::current_path();
    const auto testDir=std::filesystem::temp_directory_path()/("thievius-texture-test-"+std::to_string(getpid()));
    std::filesystem::create_directories(testDir/"sdmc:/3ds/ThieviusCTR");
    std::filesystem::current_path(testDir);
    std::vector<uint8_t> prepared,loaded;
    assert(!cane_texture_etc::prepare(rgb,8,8,loaded,false)); // Draw path never encodes a miss.
    assert(cane_texture_etc::prepare(rgb,8,8,prepared));
    assert(cane_texture_etc::prepare(rgb,8,8,loaded,false) && loaded==prepared);
    for(const auto& entry:std::filesystem::directory_iterator("sdmc:/3ds/ThieviusCTR/Cache/Textures")) {
        FILE* file=std::fopen(entry.path().c_str(),"r+b");assert(file);
        const int first=std::fgetc(file);std::rewind(file);std::fputc(first^255,file);std::fclose(file);
    }
    assert(!cane_texture_etc::prepare(rgb,8,8,loaded,false)); // Reject corrupt data instead of uploading it.
    assert(cane_texture_etc::prepare(rgb,8,8,loaded) && loaded==prepared);
    std::vector<cane_texture_etc::PreparedImage> bundle(2),restored(2);
    bundle[0][0]={8,8,prepared};bundle[1][1]={8,8,prepared};
    cane_texture_etc::saveBundle("bundle.bin",1234,bundle);
    assert(cane_texture_etc::loadBundle("bundle.bin",1234,restored));
    assert(restored[0][0].data==prepared && restored[1][1].data==prepared);
    restored.assign(2,{});assert(!cane_texture_etc::loadBundle("bundle.bin",5678,restored));
    FILE* malformed=std::fopen("bundle.bin","r+b");assert(malformed);std::fseek(malformed,32,SEEK_SET);
    const uint32_t invalidIndex=99999;std::fwrite(&invalidIndex,1,4,malformed);std::fclose(malformed);
    assert(!cane_texture_etc::loadBundle("bundle.bin",1234,restored));
    std::filesystem::current_path(originalDir);std::filesystem::remove_all(testDir);
    std::puts("PASS stereo convergence, eye sign, projected outlines, zero slider, ETC1 channels/tile order, cache round-trip and corrupt-cache fallback");
}
