#pragma once
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <fstream>
#include <memory>

namespace pls05 {
enum tag:unsigned {other,bound_paths,bound_endpoints,bound_writes,bound_bindings,provenance,
    flat_index,canonical_cells,final_values,final_bindings,final_warnings,binding_lookup,reference_cache,header_lookup,
    cycle_set,direct_and_convergence,binding_heads,write_heads,binding_keys,binding_next,binding_history,
    write_next,write_history,traversal_temporaries,tag_count};
inline constexpr const char* names[]={"other","bound.paths","bound.endpoints","bound.writes","bound.bindings","bound.provenance",
    "scratch.flat_index","output.cells","output.values","output.bindings","output.warnings","scratch.binding_lookup",
    "scratch.reference_cache","scratch.header_lookup","scratch.cycle_set","scratch.direct_convergence","scratch.binding_heads",
    "scratch.write_heads","scratch.binding_keys","scratch.binding_next","scratch.binding_history","scratch.write_next",
    "scratch.write_history","scratch.traversal_temporaries"};
inline thread_local tag active=other;
struct scope {tag previous=active;explicit scope(tag t){active=t;}~scope(){active=previous;}};
struct stat {std::uint64_t calls=0,requested=0,live=0,peak=0,frees=0;};
inline std::array<stat,tag_count> stats;
inline std::array<std::uint64_t,tag_count> at_alias_peak{};
inline std::uint64_t live=0,alias_peak=0;
// Called only for allocations whose original PLS04 phase is inside lifecycle.
inline void allocated(tag t,std::size_t n,bool alias){auto& s=stats[t];++s.calls;s.requested+=n;s.live+=n;s.peak=(std::max)(s.peak,s.live);live+=n;
    if(alias&&live>alias_peak){alias_peak=live;for(unsigned i=0;i<tag_count;++i)at_alias_peak[i]=stats[i].live;}}
inline void freed(tag t,std::size_t n){auto& s=stats[t];++s.frees;s.live-=n;live-=n;}
template<class T> struct alloc {
    using value_type=T;tag owner=other;
    using propagate_on_container_move_assignment=std::true_type;
    using propagate_on_container_copy_assignment=std::false_type;
    using propagate_on_container_swap=std::true_type;
    alloc()=default;explicit alloc(tag t):owner(t){};template<class U>alloc(const alloc<U>& a):owner(a.owner){}
    T* allocate(std::size_t n){scope s(owner);return std::allocator<T>{}.allocate(n);}
    void deallocate(T* p,std::size_t n){std::allocator<T>{}.deallocate(p,n);}
    template<class U>bool operator==(const alloc<U>& b)const{return owner==b.owner;}
};
template<class T>struct vector:std::vector<T,alloc<T>> {
    using base=std::vector<T,alloc<T>>;using base::base;
    template<class... A>explicit vector(tag t,A&&... a):base(std::forward<A>(a)...,alloc<T>{t}){}
};
template<class K,class V>struct map:std::unordered_map<K,V,std::hash<K>,std::equal_to<K>,alloc<std::pair<const K,V>>> {
    using base=std::unordered_map<K,V,std::hash<K>,std::equal_to<K>,alloc<std::pair<const K,V>>>;
    explicit map(tag t):base(0,std::hash<K>{},std::equal_to<K>{},alloc<std::pair<const K,V>>{t}){}
};
template<class K>struct set:std::unordered_set<K,std::hash<K>,std::equal_to<K>,alloc<K>> {
    using base=std::unordered_set<K,std::hash<K>,std::equal_to<K>,alloc<K>>;
    explicit set(tag t):base(0,std::hash<K>{},std::equal_to<K>{},alloc<K>{t}){}
};
struct checkpoint {
    std::string label;std::array<std::uint64_t,tag_count> bytes{};
    std::uint64_t working_set=0,os_peak=0,cells=0,values=0,bindings=0,warnings=0,path_count=0;
    std::array<std::uint64_t,5> bound_size{},bound_capacity{};
};
inline std::vector<checkpoint> checkpoints;
// Observer analysis/log allocations are explicitly outside counted lifecycle.
struct observe {pls04::phase previous=pls04::active;observe(){pls04::active=pls04::off;}~observe(){pls04::active=previous;}};
template<class B,class R>void snapshot(const char* label,const B& b,const R& r){observe o;checkpoint c;c.label=label;
    for(unsigned i=0;i<tag_count;++i)c.bytes[i]=stats[i].live;
    PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));
    c.working_set=m.WorkingSetSize;c.os_peak=m.PeakWorkingSetSize;c.cells=r.cells.size();c.values=r.values.size();c.bindings=r.bindings.size();c.warnings=r.warnings.size();c.path_count=b.paths.size();
    c.bound_size={b.paths.size()*sizeof(typename B::path_value_type),b.endpoints.size()*sizeof(typename B::endpoint_value_type),b.writes.size()*sizeof(typename B::write_value_type),b.bindings.size()*sizeof(typename B::binding_value_type),b.declaration_origins.size()*sizeof(typename B::origin_value_type)};
    c.bound_capacity={b.paths.capacity()*sizeof(typename B::path_value_type),b.endpoints.capacity()*sizeof(typename B::endpoint_value_type),b.writes.capacity()*sizeof(typename B::write_value_type),b.bindings.capacity()*sizeof(typename B::binding_value_type),b.declaration_origins.capacity()*sizeof(typename B::origin_value_type)};
    checkpoints.push_back(std::move(c));}
inline std::array<std::uint64_t,16> roles{};
inline std::uint64_t output_roots=0,unrooted_cells=0,output_path_steps=0,all_cell_path_steps=0,path_steps=0,live_writes=0,live_binding_records=0;
template<class B,class R>void analyze(const B& b,const R& r){observe o;std::vector<unsigned char> marks(r.cells.size()),paths(b.paths.size()),writes(b.writes.size()),bindings(b.bindings.size());
    for(auto v:r.values){marks.at(v.cell)|=1;writes.at(v.write)=1;}
    for(auto v:r.bindings){marks.at(v.target)|=2;marks.at(v.source)|=4;bindings.at(v.binding)=1;}
    for(auto v:r.warnings)marks.at(v.cell)|=8;
    for(std::size_t i=0;i<r.cells.size();++i){++roles[marks[i]];if(marks[i])++output_roots;else ++unrooted_cells;
        const auto p=r.cells[i].steps;for(std::uint32_t j=0;j<p.count;++j){auto& value=paths.at(p.begin+j);value|=1;if(marks[i])value|=2;}}
    for(auto p:paths){all_cell_path_steps+=bool(p&1);output_path_steps+=bool(p&2);}path_steps=paths.size();
    for(auto w:writes)live_writes+=w;for(auto v:bindings)live_binding_records+=v;
}
inline void write(const char* path){if(!path)return;observe o;std::ofstream f(path);f<<"{\"alias_peak_live_requested\":"<<alias_peak<<",\"tags\":{";
    for(unsigned i=0;i<tag_count;++i){if(i)f<<',';const auto& s=stats[i];f<<'"'<<names[i]<<"\":{\"allocations\":"<<s.calls<<",\"requested_bytes\":"<<s.requested<<",\"peak_live\":"<<s.peak<<",\"live_end\":"<<s.live<<",\"frees\":"<<s.frees<<",\"at_alias_peak\":"<<at_alias_peak[i]<<'}';}
    f<<"},\"checkpoints\":[";for(std::size_t j=0;j<checkpoints.size();++j){if(j)f<<',';const auto& c=checkpoints[j];f<<"{\"stage\":\""<<c.label<<"\",\"working_set\":"<<c.working_set<<",\"os_peak\":"<<c.os_peak<<",\"cells\":"<<c.cells<<",\"values\":"<<c.values<<",\"bindings\":"<<c.bindings<<",\"warnings\":"<<c.warnings<<",\"path_count\":"<<c.path_count<<",\"live_by_tag\":[";
      for(unsigned i=0;i<tag_count;++i){if(i)f<<',';f<<c.bytes[i];}f<<"],\"bound_size\":[";for(unsigned i=0;i<5;++i){if(i)f<<',';f<<c.bound_size[i];}f<<"],\"bound_capacity\":[";for(unsigned i=0;i<5;++i){if(i)f<<',';f<<c.bound_capacity[i];}f<<"]}";}
    f<<"],\"cell_role_masks\":[";for(unsigned i=0;i<16;++i){if(i)f<<',';f<<roles[i];}
    f<<"],\"output_roots\":"<<output_roots<<",\"unrooted_cells\":"<<unrooted_cells<<",\"path_steps\":"<<path_steps<<",\"all_cell_path_steps\":"<<all_cell_path_steps
     <<",\"output_path_steps\":"<<output_path_steps<<",\"live_write_records\":"<<live_writes<<",\"live_binding_records\":"<<live_binding_records<<"}\n";
}
}
