#pragma once
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>

namespace pls07 {
using namespace pls02;
using clock=std::chrono::steady_clock;
inline bool enabled(){static const bool value=[] {const auto* p=std::getenv("PLS07_OWN");return p&&p[0]=='1';}();return value;}
inline bool verify(){const auto* p=std::getenv("PLS07_VERIFY");return p&&p[0]=='1';}
inline double endpoint_release_ms=0,transfer_ms=0;
inline std::size_t endpoint_released_bytes=0;
inline bool buffers_moved=false,bound_empty=false,verified=false;
inline std::uint64_t history_hash=0;
struct write_view {const construction_value& value;const order& event;const origin& at;const origin& value_at;bool declaration;};
struct binding_view {const order& event;const origin& at;const origin& value_at;};
class consumer_view {
    std::span<const endpoint_path_step> path_data;
    std::span<const write> write_data;
    std::span<const binding> binding_data;
    std::span<const declaration_origin> declaration_data;
public:
    consumer_view(std::span<const endpoint_path_step> p,std::span<const write> w,std::span<const binding> b,std::span<const declaration_origin> d):path_data(p),write_data(w),binding_data(b),declaration_data(d){}
    explicit consumer_view(const bound_model& b):consumer_view(b.paths,b.writes,b.bindings,b.declaration_origins){}
    auto paths()const{return path_data;}
    auto declarations()const{return declaration_data;}
    std::size_t write_count()const{return write_data.size();}
    std::size_t binding_count()const{return binding_data.size();}
    write_view write_at(id i)const{const auto& w=write_data[i];return {w.value,w.event,w.at,w.value_at,w.declaration};}
    binding_view binding_at(id i)const{const auto& b=binding_data[i];return {b.event,b.at,b.value_at};}
    // No endpoint-index access: archived transport ordinals are not live handles.
};
struct report_counts {
    std::uint64_t declarations,events,requests,writes,bindings,origins,input_bytes;
    explicit report_counts(const bound_model& b):declarations(b.declarations),events(b.semantic_events),requests(b.endpoint_requests),writes(b.writes.size()),bindings(b.bindings.size()),origins(b.declaration_origins.size()),input_bytes(b.bytes()){}
};
inline void release_endpoints(bound_model& b){if(!enabled())return;const auto t=clock::now();endpoint_released_bytes=b.endpoints.capacity()*sizeof(typed_endpoint);
    std::vector<typed_endpoint>{}.swap(b.endpoints);endpoint_release_ms=std::chrono::duration<double,std::milli>(clock::now()-t).count();}
// Owns all semantic output storage. WHO/type/file IDs stay in their existing
// catalog domains; no dependency on Parser requests or bound_model remains.
class owned_result {
    result state_data;
    std::vector<endpoint_path_step> paths;
    // Full rows are moved, including losing events. Obsolete endpoint ordinals
    // are private archival fields, never exported as valid references.
    std::vector<write> writes;
    std::vector<binding> bindings;
    std::vector<declaration_origin> origins;
public:
    owned_result(result&& r,bound_model&& b):state_data(std::move(r)),paths(std::move(b.paths)),writes(std::move(b.writes)),bindings(std::move(b.bindings)),origins(std::move(b.declaration_origins)){}
    owned_result(const owned_result&)=delete;owned_result& operator=(const owned_result&)=delete;
    owned_result(owned_result&&)=default;owned_result& operator=(owned_result&&)=default;
    const result& state()const{return state_data;}
    consumer_view view()const{return {paths,writes,bindings,origins};}
    std::array<const void*,8> addresses()const{return {paths.data(),writes.data(),bindings.data(),origins.data(),state_data.cells.data(),state_data.values.data(),state_data.bindings.data(),state_data.warnings.data()};}
    std::size_t bytes()const{return paths.capacity()*sizeof(endpoint_path_step)+writes.capacity()*sizeof(write)+bindings.capacity()*sizeof(binding)+origins.capacity()*sizeof(declaration_origin)+state_data.cells.capacity()*sizeof(cell)+state_data.values.capacity()*sizeof(winner)+state_data.bindings.capacity()*sizeof(final_binding)+state_data.warnings.capacity()*sizeof(warning);}
};
inline std::uint64_t history_fingerprint(consumer_view data){std::uint64_t h=14695981039346656037ULL;
    const auto add=[&](std::uint64_t x){for(unsigned i=0;i<8;++i){h=(h^static_cast<unsigned char>(x))*1099511628211ULL;x>>=8;}};
    const auto at=[&](origin o){add(o.file.value());add(o.source.offset);add(o.source.length);};
    add(data.write_count());for(id i=0;i<data.write_count();++i){const auto w=data.write_at(i);add(w.value.bits());add(w.value.operand);add(static_cast<unsigned>(w.value.kind));add(w.event.source);add(w.event.local);at(w.at);at(w.value_at);add(w.declaration);}
    add(data.binding_count());for(id i=0;i<data.binding_count();++i){const auto b=data.binding_at(i);add(b.event.source);add(b.event.local);at(b.at);at(b.value_at);}
    add(data.declarations().size());for(const auto& d:data.declarations()){add(d.who.value());at(d.name);at(d.statement);add(d.event.source);add(d.event.local);add(d.is_extern);}return h;
}
inline void validate(const result& r,consumer_view data){
    for(const auto& c:r.cells)if(c.steps.begin>data.paths().size()||c.steps.count>data.paths().size()-c.steps.begin)throw failure("Owned path range invalid");
    for(const auto& w:r.values)if(w.cell>=r.cells.size()||w.write>=data.write_count())throw failure("Owned winner reference invalid");
    for(const auto& b:r.bindings)if(b.target>=r.cells.size()||b.source>=r.cells.size()||b.binding>=data.binding_count())throw failure("Owned binding reference invalid");
    for(const auto& w:r.warnings)if(w.cell>=r.cells.size())throw failure("Owned warning reference invalid");
}
inline void report(const char* path,std::size_t retained){if(!path)return;std::ofstream f(path);f<<"{\"owned\":"<<(enabled()?"true":"false")<<",\"buffers_moved\":"<<(buffers_moved?"true":"false")
 <<",\"bound_empty\":"<<(bound_empty?"true":"false")<<",\"verified\":"<<(verified?"true":"false")<<",\"history_hash\":\""<<history_hash<<"\",\"endpoint_released_bytes\":"<<endpoint_released_bytes
 <<",\"endpoint_release_ms\":"<<endpoint_release_ms<<",\"transfer_ms\":"<<transfer_ms<<",\"retained_bytes\":"<<retained<<"}\n";}
}
