#pragma once
#include "../state-lazy-01/state.hpp"
#include <array>

namespace ep01 {
using namespace sl01;
inline bool enabled=true;
inline unsigned workers=1; // coordinator configuration, fixed before dispatch
inline measurements last;
struct alignas(64) lane_work {
    std::size_t records=0,physical=ca01::none;
    double ms=0;
};
struct execution {
    std::array<lane_work,16> lanes{};
    std::size_t records=0,physical_workers=0,peak_active=0;
    double critical_lane_ms=0;
};
inline execution last_execution;

inline std::vector<record_result> evaluate_all(const batch& b,const bound_storage& types,
    const prepared& p,const ca01::result& canonical,unsigned order,ca01::pool& pool,unsigned count){
    ca01::require(count>=1&&count<=pool.size,"invalid evaluator worker count");
    std::vector<record_result> outputs(b.declarations.size());std::vector<id> tasks;
    for(id di=0;di<b.declarations.size();++di)if(b.declarations[di].kind==declaration_kind::record)tasks.push_back(di);
    if(order==1)std::reverse(tasks.begin(),tasks.end());
    if(order==2)std::sort(tasks.begin(),tasks.end(),[](id a,id z){return ca01::mix(a)<ca01::mix(z);});
    std::atomic<std::size_t> next{0},active{0},peak{0};
    execution observed;
    pool.run(count,[&](std::size_t lane,std::size_t physical){
        auto& local=observed.lanes[lane];local.physical=physical;const auto start=ei01::clock::now();
        const auto concurrency=active.fetch_add(1,std::memory_order_relaxed)+1;
        auto old=peak.load(std::memory_order_relaxed);while(old<concurrency&&!peak.compare_exchange_weak(old,concurrency,std::memory_order_relaxed)){}
        for(auto i=next.fetch_add(1,std::memory_order_relaxed);i<tasks.size();i=next.fetch_add(1,std::memory_order_relaxed)){
            const auto di=tasks[i];outputs[di]=sl01::evaluate_one(b,types,p,canonical,di);++local.records;
        }
        active.fetch_sub(1,std::memory_order_relaxed);local.ms=milliseconds(start);
    });
    // The pool barrier precedes all output reads, commit, and input destruction.
    std::array<bool,16> used{};
    for(unsigned lane=0;lane<count;++lane){const auto& local=observed.lanes[lane];observed.records+=local.records;
        if(local.records)used[local.physical]=true;observed.critical_lane_ms=(std::max)(observed.critical_lane_ms,local.ms);}
    for(bool value:used)observed.physical_workers+=value;observed.peak_active=peak.load(std::memory_order_relaxed);
    ca01::require(observed.records==tasks.size()&&observed.peak_active<=count,"invalid evaluator dispatch");
    last_execution=observed;return outputs;
}
inline void resolve(const batch& b,const ca01::result& canonical,binding_result& result,ca01::pool& pool){
    last={};last_execution={};
    if(!enabled){sl01::resolve(b,canonical,result);last=sl01::last;return;}
    const auto p=ei01::prepare(b,canonical,result.types,last);auto start=ei01::clock::now();
    auto outputs=ep01::evaluate_all(b,result.types,p,canonical,task_order,pool,workers);last.evaluate=milliseconds(start);
    if(audit_sizes){last.local_bytes=ei01::local_bytes(outputs);for(id di=0;di<b.declarations.size();++di)
        if(b.declarations[di].kind==declaration_kind::record)ca01::require(outputs[di].definition==di,"record output slot missing");}
    start=ei01::clock::now();ei01::commit(outputs,result);last.commit=milliseconds(start);
}
}
