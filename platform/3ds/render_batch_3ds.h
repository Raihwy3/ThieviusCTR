#pragma once
#include <cstddef>
#include <cstdint>

// Merge only compatible runs. Offsets refer to one shared vertex allocation;
// size_t(-1) marks other buffers. The caller reserves the full index budget.
template<class Draws,class Offset,class Compatible>
unsigned CaneMergeDynamicDraws(Draws& draws,uint16_t* indices,size_t& used,Offset offsetOf,Compatible compatible) {
    unsigned saved=0;
    size_t output=0;
    for(size_t first=0;first<draws.size();) {
        size_t end=first+1;
        const size_t base=offsetOf(draws[first]);
        if(base!=size_t(-1)) {
            size_t count=draws[first].count;
            while(end<draws.size() && compatible(draws[first],draws[end]) && count+draws[end].count<=65532) {
                const size_t offset=offsetOf(draws[end]);
                if(offset==size_t(-1) || offset<base)break;
                bool fits=true;
                for(unsigned i=0;i<draws[end].count;++i)
                    if(size_t(draws[end].indices[i])+offset-base>65535) {fits=false;break;}
                if(!fits)break;
                count+=draws[end].count;++end;
            }
        }
        auto merged=draws[first];
        if(end-first>1) {
            const size_t start=used;
            for(size_t j=first;j<end;++j) {
                const size_t offset=offsetOf(draws[j])-base;
                for(unsigned i=0;i<draws[j].count;++i)indices[used++]=uint16_t(offset+draws[j].indices[i]);
            }
            merged.indices=indices+start;merged.count=unsigned(used-start);
            saved+=unsigned(end-first-1);
        }
        draws[output++]=merged;first=end;
    }
    draws.resize(output);
    return saved;
}
