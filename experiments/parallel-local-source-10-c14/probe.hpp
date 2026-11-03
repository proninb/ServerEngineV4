#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace pls14 {
using clock=std::chrono::steady_clock;
inline const bool enabled=std::getenv("PLS10_C14_PROFILE")!=nullptr;
inline const unsigned shift=[] {auto p=std::getenv("PLS10_C14_SHIFT");return p?static_cast<unsigned>((std::min)(12ul,(std::max)(6ul,std::strtoul(p,nullptr,10)))):10u;}();
inline const std::uint64_t seed=[] {auto p=std::getenv("PLS10_C14_SEED");return p?std::strtoull(p,nullptr,10):114ull;}();
inline bool observing(){
#ifdef PLS10_C9_AUDIT
 return true;
#else
 return enabled;
#endif
}
enum part:unsigned {outside,event,declare_event,endpoint,name,symbol,type,temporal,follow,encode,path,slot,link,object,origin,provenance,warning,part_count};
inline constexpr const char* names[]={"outside","event","declare","endpoint","name","symbol","type","temporal","follow","encode","path","slot","link","object","origin","provenance","warning"};
struct metric {std::uint64_t calls=0,samples=0,ns=0,rng=0;};
inline std::array<metric,part_count> metrics{};
inline std::array<std::uint64_t,part_count> allocations{},requested{};
inline std::uint64_t string_events=0,nonstring_events=0,string_allocations=0,string_requested=0;
#ifdef PLS10_C9_AUDIT
inline std::uint64_t name_hits=0,name_misses=0,type_hits=0,type_misses=0,symbol_components=0,member_selectors=0,index_selectors=0;
#endif
inline thread_local bool active=false,is_string=false;
inline thread_local part allocation_part=outside;
struct event_context {
 bool previous_active=active,previous_string=is_string;
 explicit event_context(bool string){active=observing();is_string=string;if(active){if(string)++string_events;else ++nonstring_events;}}
 ~event_context(){active=previous_active;is_string=previous_string;}
};
inline void allocated(std::size_t n){if(!active)return;if(is_string){++string_allocations;string_requested+=n;}else{++allocations[allocation_part];requested[allocation_part]+=n;}}
struct sample {
 metric& m;bool on=false;clock::time_point at{};
#ifdef PLS10_C9_AUDIT
 part previous=allocation_part;
#endif
 explicit sample(part p):m(metrics[p]){
#ifdef PLS10_C9_AUDIT
 if(active&&!is_string){allocation_part=p;++m.calls;}
#endif
 if(!enabled||!active||is_string)return;
#ifndef PLS10_C9_AUDIT
 ++m.calls;
#endif
 if(!m.rng)m.rng=(seed+0x9e3779b97f4a7c15ull*(p+1))|1ull;
 m.rng^=m.rng>>12;m.rng^=m.rng<<25;m.rng^=m.rng>>27;
 on=((m.rng*2685821657736338717ull)&((1ull<<shift)-1))==0;
 if(on){++m.samples;at=clock::now();}}
 ~sample(){if(on)m.ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-at).count();
#ifdef PLS10_C9_AUDIT
 allocation_part=previous;
#endif
 }
};
template<class F>decltype(auto) observe(part p,F&& action){sample timer(p);return action();}
enum coarse_part:unsigned {source,wait,apply,cleanup,roots,coarse_count};
inline constexpr const char* coarse_names[]={"source","wait","apply","cleanup","roots"};
inline std::array<std::uint64_t,coarse_count> coarse_ns{},coarse_calls{};
struct coarse_scope {coarse_part p;clock::time_point at{};explicit coarse_scope(coarse_part v):p(v){if(enabled)at=clock::now();}~coarse_scope(){if(enabled){coarse_ns[p]+=std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-at).count();++coarse_calls[p];}}};
inline std::uint64_t ticks(FILETIME f){return(std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
inline std::uint64_t cpu(bool process){FILETIME a{},b{},k{},u{};bool ok=process?GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u):GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u);return ok?ticks(k)+ticks(u):0;}
inline std::uint64_t thread_cpu=0,process_cpu=0;
struct source_scope {coarse_scope timer{source};std::uint64_t t=0,p=0;source_scope(){if(enabled){t=cpu(false);p=cpu(true);}}~source_scope(){if(enabled){thread_cpu=cpu(false)-t;process_cpu=cpu(true)-p;}}};
inline void print(std::ostream& o){o<<"\npls14_enabled="<<enabled<<",pls14_shift="<<shift<<",pls14_seed="<<seed<<",pls14_string_events="<<string_events<<",pls14_nonstring_events="<<nonstring_events<<",pls14_string_allocations="<<string_allocations<<",pls14_string_requested="<<string_requested<<",pls14_thread_cpu_ms="<<thread_cpu/1e4<<",pls14_process_cpu_ms="<<process_cpu/1e4;
#ifdef PLS10_C9_AUDIT
 o<<",pls14_name_hits="<<name_hits<<",pls14_name_misses="<<name_misses<<",pls14_type_hits="<<type_hits<<",pls14_type_misses="<<type_misses<<",pls14_symbol_components="<<symbol_components<<",pls14_member_selectors="<<member_selectors<<",pls14_index_selectors="<<index_selectors;
#endif
 for(unsigned i=0;i<part_count;++i)o<<",pls14_"<<names[i]<<"_calls="<<metrics[i].calls<<",pls14_"<<names[i]<<"_samples="<<metrics[i].samples<<",pls14_"<<names[i]<<"_sample_ns="<<metrics[i].ns<<",pls14_"<<names[i]<<"_estimated_ms="<<metrics[i].ns*(1ull<<shift)/1e6<<",pls14_"<<names[i]<<"_allocations="<<allocations[i]<<",pls14_"<<names[i]<<"_requested="<<requested[i];
 for(unsigned i=0;i<coarse_count;++i)o<<",pls14_coarse_"<<coarse_names[i]<<"_ms="<<coarse_ns[i]/1e6<<",pls14_coarse_"<<coarse_names[i]<<"_calls="<<coarse_calls[i];
}
}
