#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#ifdef PLS10_C9_AUDIT
#include "audit.hpp"
#endif
namespace pls11 {
using clock=std::chrono::steady_clock;
inline const bool enabled=std::getenv("PLS10_C11_PROFILE")!=nullptr;
inline const unsigned shift=[] {auto p=std::getenv("PLS10_C11_SHIFT");return p?static_cast<unsigned>((std::min)(12ul,(std::max)(6ul,std::strtoul(p,nullptr,10)))):10u;}();
inline const std::uint64_t seed=[] {auto p=std::getenv("PLS10_C11_SEED");return p?std::strtoull(p,nullptr,10):111ull;}();
inline thread_local bool in_string=false;
inline thread_local unsigned allocation_part=0;
enum part:unsigned {other,element,path,slot,provenance,part_count};
inline constexpr const char* names[]={"other","element","path","slot","provenance"};
struct metric {std::uint64_t calls=0,samples=0,ns=0,rng=0;};
inline std::array<metric,part_count> metrics{};
inline std::array<std::uint64_t,part_count> allocations{},requested{};
inline std::uint64_t statements=0,elements=0,literal_bytes=0,string_ns=0;
// Only the ordered commit thread enters a string context. Extraction threads
// have independent false TLS and do not modify the non-atomic counters.
inline void allocated(std::size_t n){if(in_string){++allocations[allocation_part];requested[allocation_part]+=n;}}
struct sample {
 metric& m;bool on=false;clock::time_point at{};
 explicit sample(part p):m(metrics[p]){if(!enabled||!in_string)return;++m.calls;
  if(!m.rng)m.rng=(seed+0x9e3779b97f4a7c15ull*(p+1))|1ull;
  m.rng^=m.rng>>12;m.rng^=m.rng<<25;m.rng^=m.rng>>27;
  on=((m.rng*2685821657736338717ull)&((1ull<<shift)-1))==0;
  if(on){++m.samples;at=clock::now();}}
 ~sample(){if(on)m.ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-at).count();}
};
struct string_scope {
 bool previous=in_string;clock::time_point at{};
 string_scope(std::uint64_t bytes,std::uint64_t size){in_string=true;
#ifdef PLS10_C9_AUDIT
 ++statements;literal_bytes+=bytes;elements+=size;
#else
 if(enabled){++statements;literal_bytes+=bytes;elements+=size;}
#endif
 if(enabled)at=clock::now();}
 ~string_scope(){if(enabled)string_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-at).count();in_string=previous;}
};
struct allocation_scope {unsigned previous=allocation_part;explicit allocation_scope(part p){allocation_part=p;}~allocation_scope(){allocation_part=previous;}};
template<class F>decltype(auto) observe(part p,F&& action){
#ifdef PLS10_C9_AUDIT
 allocation_scope allocation_context(p);
#endif
 sample timer(p);return action();
}
struct path_counts {std::uint64_t calls=0,hits=0,inserted=0,failed=0,steps=0;};
inline path_counts paths[2]{};
template<class G,class H>struct path_audit {
 const G& g;const H& output;std::size_t before;bool active=false;unsigned context=0;
 path_audit(const G& graph,const H& result,std::size_t count):g(graph),output(result),before(graph.endpoint_path_count()){
#ifdef PLS10_C9_AUDIT
 active=pls10c9audit::active;context=in_string?1:0;if(active){++paths[context].calls;paths[context].steps+=count;}
#endif
 }
 ~path_audit(){if(active){auto& v=paths[context];if(!output)++v.failed;else if(g.endpoint_path_count()>before)++v.inserted;else ++v.hits;}}
};
inline std::uint64_t string_slot_calls=0,string_slot_existing=0,string_slot_applied=0;
template<class R>struct slot_audit {
 const R& output;bool on=false;
 explicit slot_audit(const R& r):output(r){
#ifdef PLS10_C9_AUDIT
 on=pls10c9audit::active&&in_string;
#endif
 }
 ~slot_audit(){if(on){++string_slot_calls;string_slot_existing+=output.existed;string_slot_applied+=output.applied;}}
};
inline void print(std::ostream& o){o<<"\npls11_profile="<<enabled<<",pls11_shift="<<shift<<",pls11_seed="<<seed<<",pls11_statements="<<statements<<",pls11_elements="<<elements<<",pls11_literal_bytes="<<literal_bytes<<",pls11_string_ms="<<string_ns/1e6;
 for(unsigned i=0;i<part_count;++i)o<<",pls11_"<<names[i]<<"_calls="<<metrics[i].calls<<",pls11_"<<names[i]<<"_samples="<<metrics[i].samples<<",pls11_"<<names[i]<<"_sample_ns="<<metrics[i].ns<<",pls11_"<<names[i]<<"_estimated_ms="<<metrics[i].ns*(1ull<<shift)/1e6<<",pls11_"<<names[i]<<"_allocations="<<allocations[i]<<",pls11_"<<names[i]<<"_requested="<<requested[i];
 for(unsigned i=0;i<2;++i)o<<",pls11_path"<<i<<"_calls="<<paths[i].calls<<",pls11_path"<<i<<"_hits="<<paths[i].hits<<",pls11_path"<<i<<"_new="<<paths[i].inserted<<",pls11_path"<<i<<"_failed="<<paths[i].failed<<",pls11_path"<<i<<"_steps="<<paths[i].steps;
 o<<",pls11_string_slot_calls="<<string_slot_calls<<",pls11_string_slot_existing="<<string_slot_existing<<",pls11_string_slot_applied="<<string_slot_applied;
}
}
