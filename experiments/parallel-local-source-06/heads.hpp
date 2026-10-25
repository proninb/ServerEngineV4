#pragma once
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace pls06 {
inline constexpr std::uint32_t invalid=UINT32_MAX;
struct head_stats {bool dense=false;std::size_t initial_cells=0,slots=0,capacity_bytes=0,populated=0,buckets=0;};
inline head_stats stats;
// Only the write-head index varies. Cell numbering and next-chain storage do not.
class write_heads {
    const bool dense;
    std::unordered_map<std::uint32_t,std::uint32_t> map;
    std::vector<std::uint32_t> values;
public:
    explicit write_heads(std::size_t initial_cells):dense([] {const char* p=std::getenv("PLS06_DENSE");return p&&p[0]=='1';}()){
        stats={};stats.dense=dense;stats.initial_cells=initial_cells;
    }
    std::uint32_t exchange(std::uint32_t cell,std::uint32_t write){
        if(dense){
            if(cell>=values.size())values.resize(static_cast<std::size_t>(cell)+1,invalid);
            auto& head=values[cell];const auto previous=head;head=write;return previous;
        }
        const auto [it,inserted]=map.emplace(cell,write);
        if(inserted)return invalid;
        const auto previous=it->second;it->second=write;return previous;
    }
    template<class F>void each(F&& visit){
        stats.slots=dense?values.size():map.size();stats.capacity_bytes=values.capacity()*sizeof(std::uint32_t);stats.buckets=map.bucket_count();
        if(dense){for(std::size_t cell=0;cell<values.size();++cell)if(values[cell]!=invalid)visit(static_cast<std::uint32_t>(cell),values[cell]);}
        else {for(const auto& [cell,head]:map)visit(cell,head);}
    }
};
inline void report(const char* path){if(!path)return;std::ofstream f(path);f<<"{\"dense\":"<<(stats.dense?"true":"false")
    <<",\"initial_cells\":"<<stats.initial_cells<<",\"slots\":"<<stats.slots<<",\"capacity_bytes\":"<<stats.capacity_bytes
    <<",\"populated\":"<<stats.populated<<",\"control_bucket_count\":"<<stats.buckets<<"}\n";}
}
