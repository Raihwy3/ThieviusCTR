#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

struct CanePtrHash {
    size_t operator()(const void* p) const {
        uintptr_t x=uintptr_t(p);
        x^=x>>16;x*=0x45d9f3bu;x^=x>>16;
        return size_t(x);
    }
};

// Contiguous linear-probing map with tombstone-free backward-shift erasure.
// Keeps per-frame hot cache lookups inside compact linear memory instead of
// chasing per-node std::unordered_map allocations.
template<class Key,class Value,class Hash=std::hash<Key>>
class CaneFlatMap {
    struct Slot {Key key{};Value value{};bool used=false;};
    std::vector<Slot> slots;
    size_t count=0;

    static size_t hashOf(const Key& k) {return Hash{}(k);}

    void insertUnique(const Key& k,Value&& v) {
        const size_t mask=slots.size()-1u;
        size_t i=hashOf(k)&mask;
        while(slots[i].used)i=(i+1u)&mask;
        slots[i]={k,std::move(v),true};
        ++count;
    }
    void eraseSlot(size_t i) {
        const size_t mask=slots.size()-1u;
        size_t j=(i+1u)&mask;
        while(slots[j].used) {
            const size_t home=hashOf(slots[j].key)&mask;
            if(((j-home)&mask)>=((j-i)&mask)) {
                slots[i]=std::move(slots[j]);
                i=j;
            }
            j=(j+1u)&mask;
        }
        slots[i]=Slot{};
        --count;
    }

public:
    void rehash(size_t cap) {
        size_t n=64u;
        while(n<cap)n*=2u;
        std::vector<Slot> old=std::move(slots);
        slots.assign(n,Slot{});
        count=0;
        for(auto& s:old)if(s.used)insertUnique(s.key,std::move(s.value));
    }
    Value* find(const Key& k) {
        if(slots.empty())return nullptr;
        const size_t mask=slots.size()-1u;
        for(size_t i=hashOf(k)&mask;slots[i].used;i=(i+1u)&mask) {
            if(slots[i].key==k)return &slots[i].value;
        }
        return nullptr;
    }
    const Value* find(const Key& k) const {
        return const_cast<CaneFlatMap*>(this)->find(k);
    }
    Value& operator[](const Key& k) {
        if((count+1u)*2u>slots.size())rehash(slots.empty()?64u:slots.size()*2u);
        const size_t mask=slots.size()-1u;
        size_t i=hashOf(k)&mask;
        while(slots[i].used) {
            if(slots[i].key==k)return slots[i].value;
            i=(i+1u)&mask;
        }
        slots[i].key=k;
        slots[i].value=Value{};
        slots[i].used=true;
        ++count;
        return slots[i].value;
    }
    bool erase(const Key& k) {
        if(slots.empty())return false;
        const size_t mask=slots.size()-1u;
        for(size_t i=hashOf(k)&mask;slots[i].used;i=(i+1u)&mask) {
            if(slots[i].key==k) {eraseSlot(i);return true;}
        }
        return false;
    }
    template<class Pred> void eraseIf(Pred&& pred) {
        for(size_t i=0;i<slots.size();) {
            if(slots[i].used && pred(slots[i].key,slots[i].value)) {
                eraseSlot(i);
            } else {
                ++i;
            }
        }
    }
    template<class Fn> void forEach(Fn&& fn) {
        for(auto& s:slots)if(s.used)fn(s.key,s.value);
    }
    void clear(bool release=false) {
        count=0;
        if(release)std::vector<Slot>().swap(slots);
        else slots.assign(slots.size(),Slot{});
    }
    size_t size() const {return count;}
};
