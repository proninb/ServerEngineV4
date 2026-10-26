#pragma once
#ifdef PLS10B_PROBE_HEADER
#include PLS10B_PROBE_HEADER
#else
#include "../parallel-local-source-03/probe.hpp"
#endif
#include <limits>

namespace pls10b {
using namespace pls02;
// One shared index, first for direct winners, then for canonical cells. Direct
// keys receive canonical IDs lazily in the original finalization order. There
// is no second full path index and no change to canonical cell numbering.
class registry {
    struct key {identity_ref who;type_ref type;range steps;};
    struct slot {std::uint64_t hash=0;id index_plus_one=0,direct_row_plus_one=0;};
    std::vector<slot> slots;
    std::vector<key> direct_keys;
    std::vector<id> direct_canonical;
    std::size_t count=0;
    static std::uint64_t mix(std::uint64_t x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
    template<class Paths>static auto hash(identity_ref who,range r,const Paths& paths){
        pls03::count(pls03::hash_calls);std::uint64_t h=who.value();
        for(id i=0;i<r.count;++i){const auto s=paths[r.begin+i];pls03::count(pls03::hash_steps);h=(h^static_cast<unsigned>(s.kind))*1099511628211ULL;h=(h^s.value)*1099511628211ULL;}return mix(h);
    }
    template<class Key,class Paths>static bool equal(const Key& k,identity_ref who,range r,const Paths& paths){
        pls03::count(pls03::equal_calls);if(k.who!=who||k.steps.count!=r.count)return false;
        for(id i=0;i<r.count;++i){pls03::count(pls03::equal_steps);if(paths[k.steps.begin+i]!=paths[r.begin+i])return false;}return true;
    }
    void grow(){
        if(!slots.empty()&&count+1<=slots.size()/2)return;
        std::vector<slot> next(slots.empty()?8:slots.size()*2);
        for(const auto& s:slots)if(s.index_plus_one){auto p=static_cast<std::size_t>(s.hash)&(next.size()-1);while(next[p].index_plus_one)p=(p+1)&(next.size()-1);next[p]=s;}
        slots.swap(next);
    }
public:
    static constexpr bool enabled=true;
    template<class Paths>std::pair<id,id> direct(identity_ref who,type_ref type,range r,id new_row,const Paths& paths){
        const auto h=hash(who,r,paths);
        if(!slots.empty()){auto p=static_cast<std::size_t>(h)&(slots.size()-1);
            while(slots[p].index_plus_one){const auto& s=slots[p];
                if(s.hash==h&&equal(direct_keys[s.index_plus_one-1],who,r,paths))return {s.index_plus_one-1,s.direct_row_plus_one-1};
                p=(p+1)&(slots.size()-1);}}
        grow();auto p=static_cast<std::size_t>(h)&(slots.size()-1);while(slots[p].index_plus_one)p=(p+1)&(slots.size()-1);
        const auto entry=static_cast<id>(direct_keys.size());direct_keys.push_back({who,type,r});direct_canonical.push_back(none);
        slots[p]={h,entry+1,new_row+1};++count;return {entry,new_row};
    }
    template<class Cells>id materialize(id entry,Cells& cells){
        if(direct_canonical[entry]!=none)return direct_canonical[entry];
        const auto k=direct_keys[entry];const auto c=static_cast<id>(cells.size());cells.push_back({k.who,k.type,k.steps});direct_canonical[entry]=c;
        pls03::count(pls03::cell_insert);return c;
    }
    template<class Cells,class Paths>id intern(identity_ref who,type_ref type,range r,Cells& cells,const Paths& paths){
        pls03::count(pls03::cell_find);const auto h=hash(who,r,paths);
        if(!slots.empty()){auto p=static_cast<std::size_t>(h)&(slots.size()-1);
            while(slots[p].index_plus_one){const auto& s=slots[p];bool match=false;
                if(s.hash==h){if(s.direct_row_plus_one){const auto entry=s.index_plus_one-1,canonical=direct_canonical[entry];
                    match=canonical==none?equal(direct_keys[entry],who,r,paths):equal(cells[canonical],who,r,paths);
                }else match=equal(cells[s.index_plus_one-1],who,r,paths);}
                if(match)return s.direct_row_plus_one?materialize(s.index_plus_one-1,cells):s.index_plus_one-1;
                p=(p+1)&(slots.size()-1);}}
        grow();auto p=static_cast<std::size_t>(h)&(slots.size()-1);while(slots[p].index_plus_one)p=(p+1)&(slots.size()-1);
        const auto c=static_cast<id>(cells.size());cells.push_back({who,type,r});slots[p]={h,c+1,0};++count;pls03::count(pls03::cell_insert);return c;
    }
    id canonical(id entry)const{return direct_canonical[entry];}
    void release_keys(){std::vector<key>{}.swap(direct_keys);}
    std::size_t bytes()const{return slots.capacity()*sizeof(slot);}
    std::size_t extra_bytes()const{return direct_keys.capacity()*sizeof(key)+direct_canonical.capacity()*sizeof(id);}
};
}
