#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
namespace pls10c6 {
using clock=std::chrono::steady_clock;
enum tag {queue_wait,apply_file,root_provenance,model_cleanup,final_cleanup,body,
 who_bind,typed_endpoint,object_lookup,type_request,scalar_value,write_request,
 typed_resolve,endpoint_encode,reference_follow,origin,warning,provenance,
 value_api,value_lookup,value_mutation,link_api,link_insert,link_lookup,
 value_rehash,link_rehash,value_reserve,link_reserve,origin_reserve,link_origin,
 declaration_event,assignment_event,namespace_event,binding_request,
 count_tags};
inline constexpr const char* names[]={"queue_wait","apply_file","root_provenance","model_cleanup","final_cleanup","body",
 "who_bind","typed_endpoint","object_lookup","type_request","scalar_value","write_request",
 "typed_resolve","endpoint_encode","reference_follow","origin","warning","provenance",
 "value_api","value_lookup","value_mutation","link_api","link_insert","link_lookup",
 "value_rehash","link_rehash","value_reserve","link_reserve","origin_reserve","link_origin",
 "declaration_event","assignment_event","namespace_event","binding_request"};
inline const int level=[](){auto p=std::getenv("PLS10_C6");return p?std::atoi(p):0;}();
inline const std::uint64_t seed=[](){auto p=std::getenv("PLS10_C6_SEED");return p?std::strtoull(p,nullptr,10):198;}();
struct record {
 std::uint64_t calls=0,hits=0,misses=0,samples=0,inclusive=0,exclusive=0,exact_calls=0,exact_ns=0;
 std::uint64_t hit_samples=0,miss_samples=0,hit_ns=0,miss_ns=0;
 std::array<std::uint64_t,4096> durations{};
};
inline std::array<record,count_tags> records;
struct edge {std::uint64_t calls=0,samples=0,regular_ns=0,exact_calls=0,exact_ns=0;};
inline std::array<std::array<edge,count_tags>,count_tags+1> edges;
inline thread_local bool active=false;
inline std::uint64_t clock_reads=0,path_steps=0,target_instances=0;
inline auto now(){++clock_reads;return clock::now();}
inline std::uint64_t ns(clock::time_point a,clock::time_point b){return std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();}
inline std::uint64_t mix(std::uint64_t x){x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31);}
inline void hit(tag t,bool yes){if(active&&level){if(yes)++records[t].hits;else ++records[t].misses;}}
inline void step(){if(active&&level)++path_steps;}
inline void target(){if(active&&level)++target_instances;}
struct observation {observation(){active=level!=0;}~observation(){active=false;}};
struct frame;
inline thread_local frame* top=nullptr;
struct frame {
 tag kind;frame* parent=nullptr;clock::time_point start{};std::uint64_t child=0,rare=0;
 bool enabled=false,timed=false,sampled=false,exact=false;
 bool has_result=false,success=false;
 void outcome(bool value){has_result=true;success=value;}
 explicit frame(tag t,bool all=false,bool condition=true):kind(t),exact(all){
  if(!condition||!active||!level)return;enabled=true;auto& r=records[t];++r.calls;
  if(level<2)return;
  sampled=!all&&((mix(r.calls+seed+std::uint64_t(t)*997)&1023)==0);
  if(level==3){parent=top;top=this;++edges[parent?parent->kind:count_tags][kind].calls;timed=all||sampled;if(timed)start=now();return;}
  timed=all||sampled||top!=nullptr;if(!timed)return;
  parent=top;top=this;start=now();
 }
 ~frame(){if(!enabled||level<2)return;
  if(level==3){
   const auto elapsed=timed?ns(start,now()):0;top=parent;
   auto& e=edges[parent?parent->kind:count_tags][kind];auto& r=records[kind];
   if(exact){++e.exact_calls;e.exact_ns+=elapsed;++r.exact_calls;r.exact_ns+=elapsed;
    for(auto* p=parent;p;p=p->parent)p->rare+=elapsed;}
   else if(sampled){const auto regular=elapsed>rare?elapsed-rare:0;++e.samples;e.regular_ns+=regular;
    if(r.samples<r.durations.size())r.durations[r.samples]=regular;++r.samples;r.inclusive+=regular;
    if(has_result){if(success){++r.hit_samples;r.hit_ns+=regular;}else {++r.miss_samples;r.miss_ns+=regular;}}}
   return;
  }
  if(!timed)return;
  std::uint64_t elapsed=ns(start,now());top=parent;
  if(parent&&parent->timed)parent->child+=elapsed;
  auto& r=records[kind];
  if(exact){++r.exact_calls;r.exact_ns+=elapsed;}
  else if(sampled){if(r.samples<r.durations.size())r.durations[r.samples]=elapsed;++r.samples;r.inclusive+=elapsed;r.exclusive+=elapsed>(child)?elapsed-child:0;}
 }
};
// Coarse per-file timers do not force timing of their fine-grained descendants.
struct coarse {
 tag kind;clock::time_point start{};bool enabled=false;
 explicit coarse(tag t):kind(t){if(active&&level){enabled=true;++records[t].calls;if(level>=2)start=now();}}
 ~coarse(){if(enabled&&level>=2){auto& r=records[kind];++r.exact_calls;r.exact_ns+=ns(start,now());}}
};
template<class F>decltype(auto) measured(tag t,F&& f){frame timing(t);return f();}
template<class F>decltype(auto) measured_exact(tag t,F&& f){coarse timing(t);return f();}
struct destruction_timer {
 clock::time_point start{};bool enabled=false;
 void begin(){if(active&&level){enabled=true;++records[final_cleanup].calls;if(level>=2)start=now();}}
 ~destruction_timer(){if(enabled&&level>=2){++records[final_cleanup].exact_calls;records[final_cleanup].exact_ns+=ns(start,now());}}
};
template<class T>struct measured_owner:destruction_timer,T {
 using T::T;
 ~measured_owner(){destruction_timer::begin();}
};
inline void dump(){const auto* path=std::getenv("PLS10_C6_REPORT");if(!path)return;
 std::vector<std::uint64_t> empty;for(int i=0;i<2048;++i){auto a=clock::now();auto b=clock::now();empty.push_back(ns(a,b));}
 std::sort(empty.begin(),empty.end());std::ofstream o(path);
 std::uint64_t positive=0,total=0;for(auto n:empty){total+=n;if(n&&!positive)positive=n;}
 o<<"{\"level\":"<<level<<",\"seed\":"<<seed<<",\"sampling_denominator\":1024,\"clock_pair_floor_ns\":"<<empty[empty.size()/2]<<",\"clock_pair_mean_ns\":"<<double(total)/empty.size()<<",\"clock_positive_step_ns\":"<<positive<<",\"clock_reads\":"<<clock_reads<<",\"typed_path_steps\":"<<path_steps<<",\"temporary_target_instances\":"<<target_instances<<",\"tags\":{";
 for(int t=0;t<count_tags;++t){const auto& r=records[t];if(t)o<<',';
  std::vector<std::uint64_t> v(r.durations.begin(),r.durations.begin()+(std::min)(r.samples,std::uint64_t(r.durations.size())));std::sort(v.begin(),v.end());
  const auto q=[&](double p){return v.empty()?0:v[(std::min)(v.size()-1,std::size_t(p*v.size()))];};
  o<<'"'<<names[t]<<"\":{\"calls\":"<<r.calls<<",\"hits\":"<<r.hits<<",\"misses\":"<<r.misses<<",\"samples\":"<<r.samples<<",\"inclusive_sample_ns\":"<<r.inclusive<<",\"exclusive_sample_ns\":"<<r.exclusive<<",\"inclusive_estimate_ms\":"<<(r.samples?double(r.inclusive)/r.samples*r.calls/1e6:0)<<",\"exclusive_estimate_ms\":"<<(r.samples?double(r.exclusive)/r.samples*r.calls/1e6:0)<<",\"sample_p50_ns\":"<<q(.5)<<",\"sample_p95_ns\":"<<q(.95)<<",\"exact_calls\":"<<r.exact_calls<<",\"exact_ms\":"<<r.exact_ns/1e6<<",\"hit_samples\":"<<r.hit_samples<<",\"miss_samples\":"<<r.miss_samples<<",\"hit_mean_ns\":"<<(r.hit_samples?double(r.hit_ns)/r.hit_samples:0)<<",\"miss_mean_ns\":"<<(r.miss_samples?double(r.miss_ns)/r.miss_samples:0)<<'}';
 }o<<"},\"edges\":[";bool first=true;
 for(int p=0;p<=count_tags;++p)for(int c=0;c<count_tags;++c){const auto& e=edges[p][c];if(!e.calls)continue;if(!first)o<<',';first=false;
  o<<"{\"parent\":\""<<(p==count_tags?"ROOT":names[p])<<"\",\"child\":\""<<names[c]<<"\",\"calls\":"<<e.calls<<",\"samples\":"<<e.samples<<",\"regular_ns\":"<<e.regular_ns<<",\"exact_calls\":"<<e.exact_calls<<",\"exact_ns\":"<<e.exact_ns<<'}';}
 o<<"]}";
}
}
