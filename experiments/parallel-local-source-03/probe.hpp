#pragma once
#include "../parallel-local-source-02/model.hpp"
#include "../../server_engine/project/graph/graph.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <new>
#include <numeric>

namespace pls03 {
using clock=std::chrono::steady_clock;
enum phase:unsigned {off,setup,header,catalog,references,binding_targets,binding_diagnostics,binding_cleanup,
    write_targets,reduction,value_order,write_cleanup,diagnostic_order,release,phase_count};
inline constexpr const char* phases[]={"off","alias_setup","header_seed","binding_catalog","reference_resolution","binding_targets",
    "binding_diagnostics","binding_cleanup","write_targets_and_index","lww_reduction","value_order","write_cleanup","diagnostic_order","alias_release"};
enum metric:unsigned {canonical_calls,canonical_direct,canonical_deref,intern_range,intern_vector,cell_find,cell_insert,
    hash_calls,hash_steps,equal_calls,equal_steps,follow_calls,follow_hits,follow_misses,path_walk_steps,deref_scan_steps,
    binding_lookups,cache_lookups,heads_insert,select_find,select_store,cache_store,visiting_insert,visiting_erase,binding_at,metric_count};
inline constexpr const char* metrics[]={"canonical_calls","canonical_direct","canonical_deref","intern_range","intern_vector",
    "cell_find","cell_insert","cell_hash_calls","cell_hash_steps","cell_equal_calls","cell_equal_steps",
    "follow_calls","follow_hits","follow_misses","path_walk_steps","deref_scan_steps","binding_lookups","cache_lookups","heads_emplace",
    "binding_select_find","binding_select_store","cache_store","visiting_insert","visiting_erase","binding_at"};
enum method:unsigned {canonical_method,intern_method,vector_method,follow_method,step_method,method_count};
inline constexpr const char* methods[]={"canonical","intern_range","intern_vector","follow","typed_step"};
struct stats {
    std::uint64_t elapsed_ns=0,allocations=0,requested_bytes=0,live=0,peak_live=0,frees=0;
    std::array<std::uint64_t,metric_count> counts{};
};
inline std::array<stats,phase_count> stages;
inline thread_local phase active=off;
inline clock::time_point changed;
inline bool running=false;
inline std::uint64_t total_live=0,total_peak=0,total_calls=0,total_bytes=0,flat_capacity_bytes=0;
inline void boundary(phase next){
    const auto now=clock::now();if(running&&active!=off)stages[active].elapsed_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(now-changed).count();
    active=next;changed=now;
}
inline void count(metric m,std::uint64_t n=1){
#ifdef PLS03_AUDIT
    if(active!=off)stages[active].counts[m]+=n;
#else
    (void)m;(void)n;
#endif
}
struct pause {phase previous=active;pause(){active=off;}~pause(){active=previous;}};
struct method_stats {std::uint64_t calls=0,samples=0,ns=0;std::uint32_t random=0x9e3779b9u;std::vector<std::uint64_t> durations;};
inline std::array<method_stats,method_count> samples;
struct sample {
#ifdef PLS03_AUDIT
    method kind;bool selected=false;clock::time_point start;
    explicit sample(method m):kind(m){auto& s=samples[m];++s.calls;auto r=s.random;r^=r<<13;r^=r>>17;r^=r<<5;s.random=r;selected=((r&2047u)==0);if(selected)start=clock::now();}
    ~sample(){if(selected){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-start).count();
        auto& s=samples[kind];++s.samples;s.ns+=ns;pause p;s.durations.push_back(ns);}}
#else
    explicit sample(method){}
#endif
};
struct path_request {std::uint32_t who=0;pls02::range path{};bool final=false;};
inline std::vector<path_request> requests;
template<class E> void canonical_request(const E& e,bool final){
#ifdef PLS03_AUDIT
    pause p;requests.push_back({e.who.value(),e.steps,final});
#else
    (void)e;(void)final;
#endif
}
struct lifetime_record {std::uint64_t remaining=0,bound=0,current_file=0;};
inline std::vector<lifetime_record> lifetime;
inline std::uint64_t local_total=0,local_remaining=0;
inline std::array<std::uint64_t,11> local_columns{};
inline constexpr const char* column_names[]={"names_text","name_ranges","qualified_components","scopes","symbols","selectors","endpoint_requests","type_requests","extents","values_and_bytes","events"};
template<class V> std::uint64_t capacity_bytes(const V& v){return v.capacity()*sizeof(typename V::value_type);}
template<class Models> void lifetime_begin(const Models& inputs){
#ifdef PLS03_AUDIT
    local_total=0;local_columns={};for(const auto& m:inputs){local_total+=m.retained_bytes();
        local_columns[0]+=capacity_bytes(m.text);local_columns[1]+=capacity_bytes(m.names);local_columns[2]+=capacity_bytes(m.components);
        local_columns[3]+=capacity_bytes(m.scopes);local_columns[4]+=capacity_bytes(m.symbols);local_columns[5]+=capacity_bytes(m.selectors);
        local_columns[6]+=capacity_bytes(m.endpoints);local_columns[7]+=capacity_bytes(m.types);local_columns[8]+=capacity_bytes(m.extents);
        local_columns[9]+=capacity_bytes(m.values)+capacity_bytes(m.bytes);local_columns[10]+=capacity_bytes(m.events);}
    local_remaining=local_total;lifetime.clear();lifetime.reserve(inputs.size());
#else
    (void)inputs;
#endif
}
template<class M> void lifetime_file(const M& local,std::size_t bound){
#ifdef PLS03_AUDIT
    local_remaining-=local.retained_bytes();lifetime.push_back({local_remaining,bound,local.retained_bytes()});
#else
    (void)local;(void)bound;
#endif
}
template<class B> void begin(const B& b){
    stages={};samples={};requests.clear();total_live=total_peak=total_calls=total_bytes=flat_capacity_bytes=0;
#ifdef PLS03_AUDIT
    requests.reserve(b.bindings.size()*4+b.writes.size()*2+1024);
#endif
    running=true;active=setup;changed=clock::now();
}
template<class B> void finish(const B& bound,double total_ms){
    boundary(off);running=false;
    const auto* file=std::getenv("PLS03_PROFILE_PATH");if(!file)return;
    std::ofstream out(file);out<<"{\n\"audit\":"
#ifdef PLS03_AUDIT
      <<"true"
#else
      <<"false"
#endif
      <<",\n\"alias_ms\":"<<total_ms<<",\n\"allocation_totals\":{\"calls\":"<<total_calls<<",\"requested_bytes\":"<<total_bytes<<",\"peak_live_requested\":"<<total_peak<<",\"live_requested\":"<<total_live
      <<",\"flat_index_capacity_bytes\":"<<flat_capacity_bytes<<"},\n\"stages\":{";
    for(unsigned s=1;s<phase_count;++s){if(s>1)out<<',';const auto& st=stages[s];out<<'"'<<phases[s]<<"\":{\"ms\":"<<st.elapsed_ns/1e6
        <<",\"allocations\":"<<st.allocations<<",\"requested_bytes\":"<<st.requested_bytes<<",\"peak_live_requested\":"<<st.peak_live<<",\"live_requested\":"<<st.live<<",\"frees\":"<<st.frees<<",\"counts\":{";
        for(unsigned m=0;m<metric_count;++m){if(m)out<<',';out<<'"'<<metrics[m]<<"\":"<<st.counts[m];}out<<"}}";}
    out<<"},\n\"method_samples_inclusive\":{";
    for(unsigned i=0;i<method_count;++i){if(i)out<<',';auto& s=samples[i];std::sort(s.durations.begin(),s.durations.end());
        out<<'"'<<methods[i]<<"\":{\"calls\":"<<s.calls<<",\"samples\":"<<s.samples<<",\"sample_ns\":"<<s.ns
           <<",\"median_sample_ns\":"<<(s.samples?s.durations[s.samples/2]:0)<<",\"p95_sample_ns\":"<<(s.samples?s.durations[(s.samples-1)*95/100]:0)
           <<",\"inclusive_sample_estimate_ms\":"<<(s.samples?double(s.ns)*s.calls/s.samples/1e6:0)<<'}';}
    std::uint64_t distinct=0,groups=0,maximum=0,distinct_deref=0;
#ifdef PLS03_AUDIT
    const auto less=[&](const path_request& a,const path_request& b){
        if(a.who!=b.who)return a.who<b.who;if(a.final!=b.final)return a.final<b.final;
        const auto aa=std::span<const cw::server::endpoint_path_step>{bound.paths}.subspan(a.path.begin,a.path.count);
        const auto bb=std::span<const cw::server::endpoint_path_step>{bound.paths}.subspan(b.path.begin,b.path.count);
        return std::lexicographical_compare(aa.begin(),aa.end(),bb.begin(),bb.end(),[](auto x,auto y){return std::tie(x.kind,x.value)<std::tie(y.kind,y.value);});};
    std::sort(requests.begin(),requests.end(),less);
    for(std::size_t first=0;first<requests.size();){auto last=first+1;while(last<requests.size()&&!less(requests[first],requests[last])&&!less(requests[last],requests[first]))++last;
        ++distinct;if(last-first>1)++groups;maximum=(std::max)(maximum,static_cast<std::uint64_t>(last-first));
        for(const auto& step:std::span<const cw::server::endpoint_path_step>{bound.paths}.subspan(requests[first].path.begin,requests[first].path.count))
            if(step.kind==cw::server::endpoint_path_step_kind::dereference){++distinct_deref;break;}
        first=last;
    }
#endif
    std::uint64_t actual_overlap=local_total,hypothetical=local_total;
    for(const auto& r:lifetime){actual_overlap=(std::max)(actual_overlap,local_total+r.bound);hypothetical=(std::max)(hypothetical,r.remaining+r.current_file+r.bound);}
    out<<"},\n\"path_reuse\":{\"calls\":"<<requests.size()<<",\"distinct_who_path_follow_mode\":"<<distinct<<",\"repeated_groups\":"<<groups<<",\"max_calls_per_path\":"<<maximum<<",\"distinct_deref_paths\":"<<distinct_deref<<"},\n"
       <<"\"lifetime_capacity\":{\"local_bytes\":"<<local_total<<",\"actual_local_plus_bound_peak\":"<<actual_overlap<<",\"hypothetical_release_after_file_peak\":"<<hypothetical<<",\"files\":"<<lifetime.size()<<"},\n\"local_columns\":{";
    for(unsigned i=0;i<local_columns.size();++i){if(i)out<<',';out<<'"'<<column_names[i]<<"\":"<<local_columns[i];}
    out<<"},\n\"bound_columns\":{\"paths\":"<<capacity_bytes(bound.paths)<<",\"endpoints\":"<<capacity_bytes(bound.endpoints)<<",\"writes\":"<<capacity_bytes(bound.writes)
       <<",\"bindings\":"<<capacity_bytes(bound.bindings)<<",\"declaration_origins\":"<<capacity_bytes(bound.declaration_origins)<<"}\n}\n";
}
}

#ifdef PLS03_ALLOCATIONS
namespace pls03 {
struct allocation_header {void* base;std::size_t bytes;phase owner;};
inline void* allocate(std::size_t requested,std::size_t alignment){
    alignment=(std::max)(alignment,alignof(allocation_header));const auto n=requested?requested:1;
    if(n>static_cast<std::size_t>(-1)-sizeof(allocation_header)-alignment)throw std::bad_alloc();
    void* raw=std::malloc(n+sizeof(allocation_header)+alignment-1);if(!raw)throw std::bad_alloc();
    const auto address=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(allocation_header)+alignment-1)&~(alignment-1);
    auto* h=reinterpret_cast<allocation_header*>(address)-1;h->base=raw;h->bytes=requested;h->owner=active;
    if(active!=off){auto& st=stages[active];++st.allocations;st.requested_bytes+=requested;st.live+=requested;st.peak_live=(std::max)(st.peak_live,st.live);
        ++total_calls;total_bytes+=requested;total_live+=requested;total_peak=(std::max)(total_peak,total_live);}
    return reinterpret_cast<void*>(address);
}
inline void deallocate(void* p) noexcept {if(!p)return;auto* h=static_cast<allocation_header*>(p)-1;
    if(h->owner!=off){auto& s=stages[h->owner];s.live-=h->bytes;++s.frees;total_live-=h->bytes;}std::free(h->base);}
}
void* operator new(std::size_t n){return pls03::allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,std::align_val_t a){return pls03::allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t& t)noexcept{return ::operator new(n,t);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t& t)noexcept{return ::operator new(n,a,t);}
void operator delete(void* p)noexcept{pls03::deallocate(p);}
void operator delete[](void* p)noexcept{pls03::deallocate(p);}
void operator delete(void* p,std::size_t)noexcept{pls03::deallocate(p);}
void operator delete[](void* p,std::size_t)noexcept{pls03::deallocate(p);}
void operator delete(void* p,std::align_val_t)noexcept{pls03::deallocate(p);}
void operator delete[](void* p,std::align_val_t)noexcept{pls03::deallocate(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{pls03::deallocate(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{pls03::deallocate(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{pls03::deallocate(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{pls03::deallocate(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{pls03::deallocate(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{pls03::deallocate(p);}
#endif
