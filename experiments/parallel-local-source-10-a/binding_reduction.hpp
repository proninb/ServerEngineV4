#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace pls10 {
inline bool enabled(){const auto* p=std::getenv("PLS10_FINALIZE");return p&&(p[0]=='1'||p[0]=='2');}
inline bool sparse_diagnostics(){const auto* p=std::getenv("PLS10_FINALIZE");return p&&p[0]=='2';}
inline constexpr std::uint32_t absent=UINT32_MAX;
struct measurements {std::size_t direct_operations=0,indirect_operations=0,target_evaluations=0,rounds=0,final_bindings=0,capacity_bytes=0;bool diagnostic_pass_skipped=false;};
inline measurements stats;

// Temporary mathematical B(cell), not a Graph/WHO/type database. Direct targets
// have one winning operation ID each. Nested targets use a sparse overlay on the
// previous context; no complete catalog is copied for a round.
class binding_reduction {
    struct delta {std::uint32_t current=absent,next=absent,generation=0;bool listed=false;};
    std::vector<std::uint32_t> direct,direct_cells,indirect_cells;
    std::vector<delta> overlay;
    std::uint32_t generation=0;
public:
    std::uint32_t direct_at(std::uint32_t cell)const{return cell<direct.size()?direct[cell]:absent;}
    std::uint32_t find(std::uint32_t cell)const{
        return cell<overlay.size()&&overlay[cell].current!=absent?overlay[cell].current:direct_at(cell);
    }
    template<class Later>void select_direct(std::uint32_t cell,std::uint32_t operation,Later later){
        if(cell>=direct.size())direct.resize(static_cast<std::size_t>(cell)+1,absent);
        const auto previous=direct[cell];
        if(previous==absent){direct_cells.push_back(cell);direct[cell]=operation;}
        else if(later(operation,previous))direct[cell]=operation;
    }
    void begin_round(){
        if(generation==UINT32_MAX){for(auto& item:overlay)item.generation=0;generation=1;}
        else ++generation;
    }
    template<class Later>void select_next(std::uint32_t cell,std::uint32_t operation,Later later){
        if(cell>=overlay.size())overlay.resize(static_cast<std::size_t>(cell)+1);
        // No reference survives a caller's recursive target resolution. This
        // method runs only after resolution returns and never recurses itself.
        auto& item=overlay[cell];
        if(!item.listed){indirect_cells.push_back(cell);item.listed=true;}
        if(item.generation!=generation){item.generation=generation;item.next=direct_at(cell);}
        if(item.next==absent||later(operation,item.next))item.next=operation;
    }
    bool commit_round(){
        bool stable=true;
        for(const auto cell:indirect_cells){auto& item=overlay[cell];const auto next=item.generation==generation?item.next:direct_at(cell);
            stable&=find(cell)==next;item.current=next==direct_at(cell)?absent:next;}
        return stable;
    }
    std::vector<std::uint32_t> keys()const{
        auto result=direct_cells;
        for(const auto cell:indirect_cells)if(direct_at(cell)==absent&&find(cell)!=absent)result.push_back(cell);
        std::sort(result.begin(),result.end());return result;
    }
    std::size_t bytes()const{return (direct.capacity()+direct_cells.capacity()+indirect_cells.capacity())*sizeof(std::uint32_t)+overlay.capacity()*sizeof(delta);}
};
}
