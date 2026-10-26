#pragma once
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <iostream>
#ifdef PLS10_C9_AUDIT
#include "audit.hpp"
#endif
namespace pls10c9 {
inline bool enabled(){static const bool v=std::getenv("PLS10_C9_PROFILE")!=nullptr;return v;}
using clock=std::chrono::steady_clock;
inline unsigned long long ticks(FILETIME f){return (static_cast<unsigned long long>(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
inline unsigned long long cpu(bool process=false){FILETIME a{},b{},k{},u{};const auto ok=process?GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u):GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u);return ok?ticks(k)+ticks(u):0;}
inline unsigned long long cycles(){ULONG64 n=0;QueryThreadCycleTime(GetCurrentThread(),&n);return n;}
struct metric {std::atomic<unsigned long long> wall{0},cpu{0},cycles{0},calls{0};};
inline metric source,parse,wait,apply,cleanup;
inline std::atomic<unsigned long long> produced{0},live{0},peak{0},events{0},tokens{0};
inline unsigned long long process_start=0,process_end=0,ws_begin=0,ws_end=0;
inline unsigned long long ws(){PROCESS_MEMORY_COUNTERS p{};return GetProcessMemoryInfo(GetCurrentProcess(),&p,sizeof(p))?p.WorkingSetSize:0;}
struct scope {
#ifdef PLS10_C9_AUDIT
 pls10c9audit::guard allocation_guard;
#endif
 metric& m;bool on;clock::time_point at{};unsigned long long c=0,y=0;
 explicit scope(metric& v):
#ifdef PLS10_C9_AUDIT
 allocation_guard(&v==&apply?3:(&v==&parse&&std::getenv("PLS08_CANDIDATE")&&*std::getenv("PLS08_CANDIDATE")=='1'?2:1)),
#endif
 m(v),on(enabled()){if(on){at=clock::now();c=cpu();y=cycles();}}
 ~scope(){if(on){m.cycles+=cycles()-y;m.cpu+=cpu()-c;m.wall+=std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-at).count();++m.calls;}}
};
struct source_scope {scope timer{source};source_scope(){if(enabled()){process_start=cpu(true);ws_begin=ws();}}~source_scope(){if(enabled()){process_end=cpu(true);ws_end=ws();}}};
inline void model_add(unsigned long long bytes,unsigned long long count,unsigned long long reads){if(!enabled())return;produced+=bytes;events+=count;tokens+=reads;auto n=live.fetch_add(bytes)+bytes;auto old=peak.load();while(n>old&&!peak.compare_exchange_weak(old,n)){};}
inline void print(std::ostream& out){
out<<"\npls10c9=1";
#ifdef PLS10_C9_AUDIT
pls10c9audit::print(out);
#endif
out<<",c9_enabled="<<enabled();if(!enabled())return;for(auto p:{std::pair{"source",&source},{"parse",&parse},{"wait",&wait},{"apply",&apply},{"cleanup",&cleanup}})out<<",c9_"<<p.first<<"_wall_ms="<<p.second->wall.load()/1e6<<",c9_"<<p.first<<"_cpu_ms="<<p.second->cpu.load()/1e4<<",c9_"<<p.first<<"_cycles="<<p.second->cycles.load()<<",c9_"<<p.first<<"_calls="<<p.second->calls.load();out<<",c9_source_process_cpu_ms="<<(process_end-process_start)/1e4<<",c9_ws_begin="<<ws_begin<<",c9_ws_end="<<ws_end<<",c9_model_produced_capacity="<<produced.load()<<",c9_model_peak_capacity="<<peak.load()<<",c9_events="<<events.load()<<",c9_token_reads="<<tokens.load();}
}
