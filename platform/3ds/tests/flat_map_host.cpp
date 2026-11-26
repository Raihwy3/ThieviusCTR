#include "../flat_map_3ds.h"
#include <cassert>
#include <cstdio>
#include <unordered_map>

namespace {
struct ColliderKey {
    int id=0;
    size_t bucket=0;
    bool operator==(const ColliderKey& o) const {return id==o.id && bucket==o.bucket;}
};
struct ColliderHash {
    size_t operator()(const ColliderKey& k) const {return k.bucket;}
};
}

int main() {
    CaneFlatMap<int,int> map;
    assert(map.size()==0);
    assert(map.find(1)==nullptr);
    assert(!map.erase(1));

    for(int i=0;i<500;++i)map[i]=i*7+3;
    assert(map.size()==500);
    for(int i=0;i<500;++i) {
        int* v=map.find(i);
        assert(v && *v==i*7+3);
    }
    for(int i=0;i<500;i+=2)assert(map.erase(i));
    assert(!map.erase(0));
    assert(map.size()==250);
    for(int i=0;i<500;++i) {
        int* v=map.find(i);
        if(i%2==0)assert(v==nullptr);
        else assert(v && *v==i*7+3);
    }

    // Wrap-around linear probing & backward-shift erasure near table end.
    CaneFlatMap<ColliderKey,int,ColliderHash> wrap;
    std::unordered_map<int,int> ref;
    for(int i=0;i<24;++i) {
        const size_t bucket=(i%2==0)?62u:63u;
        ColliderKey k{i,bucket};
        wrap[k]=i*10;
        ref[i]=i*10;
    }
    assert(wrap.erase(ColliderKey{0,62u}));
    ref.erase(0);
    assert(wrap.erase(ColliderKey{3,63u}));
    ref.erase(3);
    wrap.eraseIf([&](const ColliderKey& k,int&) {
        const bool drop=(k.id%5)==0 || k.id==2 || k.id==19;
        if(drop)ref.erase(k.id);
        return drop;
    });
    assert(wrap.size()==ref.size());
    size_t visited=0;
    wrap.forEach([&](const ColliderKey& k,int& v) {
        assert(ref.at(k.id)==v);
        ++visited;
    });
    assert(visited==ref.size());
    for(int i=0;i<24;++i) {
        const size_t bucket=(i%2==0)?62u:63u;
        int* v=wrap.find(ColliderKey{i,bucket});
        if(ref.count(i))assert(v && *v==ref[i]);
        else assert(v==nullptr);
    }

    CanePtrHash ptrHash;
    int a=0,b=0;
    assert(ptrHash(&a)!=ptrHash(&b));

    map.clear(true);
    assert(map.size()==0);
    assert(map.find(1)==nullptr);
    std::puts("PASS flat_map: insert, growth, wrap-around backward-shift erase, eraseIf, forEach, clear");
}
