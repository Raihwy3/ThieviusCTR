#include "../render_batch_3ds.h"
#include <vector>
#include <cassert>
#include <iostream>
struct Draw {size_t offset;const uint16_t* indices;unsigned count;int material;};
int main() {
    const uint16_t tri[]={0,1,2};
    uint16_t buffer[64]{};
    size_t used=4; // Existing static indices must stay intact.
    buffer[0]=123;
    auto offset=[](const Draw& d){return d.offset;};
    auto compatible=[](const Draw& a,const Draw& b){return a.material==b.material;};
    std::vector<Draw> draws={{10,tri,3,1},{20,tri,3,1},{30,tri,3,2},{40,tri,3,2}};
    assert(CaneMergeDynamicDraws(draws,buffer,used,offset,compatible)==2);
    assert(draws.size()==2 && used==16 && buffer[0]==123);
    assert(draws[0].offset==10 && draws[0].count==6);
    assert(draws[0].indices[3]==10 && draws[1].indices[3]==10);
    used=0;
    draws={{0,tri,3,1},{65534,tri,3,1},{size_t(-1),tri,3,1},{5,tri,3,1},{3,tri,3,1}};
    assert(CaneMergeDynamicDraws(draws,buffer,used,offset,compatible)==0);
    assert(draws.size()==5 && used==0); // Overflow, other buffers, descending offsets.
    const uint16_t last[]={0,1,1};
    draws={{0,tri,3,1},{65534,last,3,1}};
    assert(CaneMergeDynamicDraws(draws,buffer,used,offset,compatible)==1);
    assert(draws[0].indices[5]==65535); // Last valid 16-bit vertex.
    used=0;
    draws={{0,tri,65532,1},{3,tri,3,1}};
    assert(CaneMergeDynamicDraws(draws,buffer,used,offset,compatible)==0);
    draws.clear();assert(CaneMergeDynamicDraws(draws,buffer,used,offset,compatible)==0);
    std::cout<<"PASS batch rebasing, material boundaries, index limits, buffer boundaries and compaction\n";
}
