#pragma once
#include "probe.hpp"

namespace pls03 {
// Sole candidate: replace only canonical-cell indexing. IDs, payload order,
// reference caches, LWW heads, diagnostics and model lifetimes stay unchanged.
struct flat_slot {std::uint64_t hash=0;std::uint32_t cell_plus_one=0,reserved=0;};
class flat_index {
    std::vector<flat_slot> slots;
    std::size_t count=0;
    static std::uint64_t mix(std::uint64_t x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
    template<class Paths> static std::uint64_t hash(std::uint32_t who,pls02::range r,const Paths& pp){
        count_metric(hash_calls);std::uint64_t h=who;
        for(std::uint32_t i=0;i<r.count;++i){const auto& s=pp[r.begin+i];count_metric(hash_steps);h=(h^static_cast<unsigned>(s.kind))*1099511628211ULL;h=(h^s.value)*1099511628211ULL;}
        return mix(h);
    }
    static void count_metric(metric m,std::uint64_t n=1){pls03::count(m,n);}
    void grow(){
        if(!slots.empty() && count+1<=slots.size()/2)return;
        std::vector<flat_slot> next(slots.empty()?8:slots.size()*2);
        for(const auto& s:slots)if(s.cell_plus_one){auto p=static_cast<std::size_t>(s.hash)&(next.size()-1);
            while(next[p].cell_plus_one)p=(p+1)&(next.size()-1);next[p]=s;}
        slots.swap(next);
    }
public:
    const bool enabled;
    flat_index():enabled([]{const auto* p=std::getenv("PLS03_MODE");return p&&p[0]=='1';}()){}
    template<class Cells,class Paths,class Type> std::uint32_t intern(cw::server::identity_ref who,Type type,pls02::range r,Cells& cells,const Paths& pp){
        count_metric(cell_find);const auto h=hash(who.value(),r,pp);
        std::size_t p=0;
        if(!slots.empty()){
            p=static_cast<std::size_t>(h)&(slots.size()-1);
            while(slots[p].cell_plus_one){const auto& s=slots[p];
                if(s.hash==h){const auto& old=cells[s.cell_plus_one-1];count_metric(equal_calls);
                    bool same=old.who==who&&old.steps.count==r.count;
                    if(same)for(std::uint32_t i=0;i<r.count;++i){count_metric(equal_steps);if(pp[old.steps.begin+i]!=pp[r.begin+i]){same=false;break;}}
                    if(same)return s.cell_plus_one-1;
                }
                p=(p+1)&(slots.size()-1);
            }
        }
        const auto previous_size=slots.size();grow();
        if(slots.size()!=previous_size){p=static_cast<std::size_t>(h)&(slots.size()-1);while(slots[p].cell_plus_one)p=(p+1)&(slots.size()-1);}
        const auto id=static_cast<std::uint32_t>(cells.size());cells.push_back({who,type,r});slots[p]={h,id+1,0};++count;count_metric(cell_insert);return id;
    }
    std::size_t bytes() const {return slots.capacity()*sizeof(flat_slot);}
};
}
