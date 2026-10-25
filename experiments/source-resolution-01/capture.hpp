#pragma once
#include "../../server_engine/project/frontend/semantic_input.hpp"
#include "../../server_engine/project/source/source_map.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cw::server::sres01 {
// 0 inactive, 1 record, 2 loaded control, 3 tokens, 4 identity reads,
// 5 endpoint formation, 6 provenance. No mode changes Graph.
struct token_record { semantic_token token{}; server_status status{}; };
struct name_record { std::uint32_t begin=0,length=0; };
struct find_record { identity_ref scope{},result{}; string_id name{}; identity_kind kind{}; };
struct resolve_record { identity_ref scope{},result{}; string_id name{}; identity_kind kind{}; server_status status{}; bool created=false; };
enum class prov_kind:std::uint32_t { begin,end,dependency,initialization,contribution };
struct prov_record { prov_kind kind{}; file_id file{}; identity_ref identity{}; object_endpoint target{}; source_data_ref data{}; server_status status{}; };
struct endpoint_record {
    identity_ref scope{},prebound{};
    semantic_token before{},buffer_before{},after{},buffer_after{};
    bool has_before=false,has_after=false;
    type_ref type{},object_type{};
    object_endpoint ref{};
    file_id unit_file{}; identity_ref unit_who{}; source_range location{};
    std::uint64_t path_begin=0,path_count=0,path_capacity=0,token_reads=0,prov_begin=0,prov_count=0;
    server_status status{};
};
struct metadata { std::uint64_t magic=0x5352455330315631ULL,version=2,roots=0,strings=0,identities=0,corpus=0; };
struct state_type {
    int mode=0; bool failed=false,verify=true;
    metadata meta{};
    std::vector<token_record> tokens;
    std::vector<name_record> names;
    std::vector<char> text;
    std::vector<find_record> finds;
    std::vector<resolve_record> resolves;
    std::vector<prov_record> provenance;
    std::vector<endpoint_record> endpoints;
    std::vector<endpoint_path_step> steps;
    std::size_t token_at=0,find_at=0,resolve_at=0,prov_at=0,endpoint_at=0;
    std::uint64_t retained_new=0,bypassed_existing=0;
    std::chrono::steady_clock::time_point started{};
};
inline state_type state;
inline server_status bad(const char* detail) noexcept {
    if(!state.failed) std::fprintf(stderr,"SOURCE_RESOLUTION_01: %s\n",detail);
    state.failed=true;return server_status::project_artifact_invalid;
}
inline bool same_token(const semantic_token& a,const semantic_token& b) noexcept {
    return a.file==b.file && a.kind==b.kind && a.source_offset==b.source_offset &&
        a.source_length==b.source_length && a.identifier==b.identifier;
}
template<class T> void save_vector(std::ofstream& f,const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const std::uint64_t count=v.size(); f.write(reinterpret_cast<const char*>(&count),sizeof count);
    f.write(reinterpret_cast<const char*>(v.data()),static_cast<std::streamsize>(v.size()*sizeof(T)));
}
template<class T> void load_vector(std::ifstream& f,std::vector<T>& v) {
    std::uint64_t count=0; f.read(reinterpret_cast<char*>(&count),sizeof count);
    if(!f || count>4000000000ULL/sizeof(T)) throw std::runtime_error("invalid trace count");
    v.resize(static_cast<std::size_t>(count));
    f.read(reinterpret_cast<char*>(v.data()),static_cast<std::streamsize>(v.size()*sizeof(T)));
    if(!f) throw std::runtime_error("truncated trace");
}
inline const char* tape_path() {
    const auto* p=std::getenv("CW_SOURCE_RESOLUTION_TAPE");
    if(!p || !*p) throw std::runtime_error("CW_SOURCE_RESOLUTION_TAPE is required"); return p;
}
inline std::uint64_t fingerprint(std::size_t roots,const file_context& files,
    const string_table& strings,const identity_space& ids,const graph& G) noexcept {
    std::uint64_t hash=1469598103934665603ULL;
    const auto bytes=[&](const void* data,std::size_t size) {
        const auto* p=static_cast<const unsigned char*>(data);
        for(std::size_t i=0;i<size;++i) {hash^=p[i];hash*=1099511628211ULL;}
    };
    for(std::size_t i=0;i<roots;++i) {
        const file_id file{static_cast<std::uint32_t>(i+1)};
        if(files.kind(file)!=file_kind::source) continue;
        const auto text=files.content(file);const auto size=text.size();
        bytes(&size,sizeof size);bytes(text.data(),text.size());
    }
    for(std::size_t i=1;i<=strings.size();++i) {
        const auto text=strings.spelling(static_cast<std::uint32_t>(i));
        const auto size=text.size();bytes(&size,sizeof size);bytes(text.data(),text.size());
    }
    for(std::size_t i=1;i<=ids.size();++i) {
        const auto id=ids.at_slot(static_cast<std::uint32_t>(i));identity_record r;
        if(!ids.record(id,r)) return 0;
        bytes(&id,sizeof id);bytes(&r,sizeof r);
    }
    const auto span=[&](auto values){ bytes(values.data(),values.size_bytes()); };
    span(G.type_entries());span(G.type_identity_entries());span(G.member_entries());
    span(G.member_construction_entries());span(G.base_entries());span(G.derived_type_entries());
    span(G.object_entries());span(G.object_identity_entries());span(G.object_construction_entries());
    span(G.constructor_defaults.entries());
    return hash;
}
inline bool begin(std::size_t roots,const file_context& files,const string_table& strings,
    const identity_space& ids,const graph& G) noexcept {
    try {
        state=state_type{};
        if(const auto* m=std::getenv("CW_SOURCE_RESOLUTION_MODE")) state.mode=std::atoi(m);
        if(state.mode<0 || state.mode>6) return false;
        if(const auto* v=std::getenv("CW_SOURCE_RESOLUTION_VERIFY")) state.verify=std::strcmp(v,"0")!=0;
        state.meta.roots=roots;state.meta.strings=strings.size();state.meta.identities=ids.size();
        if(state.mode!=0) state.meta.corpus=fingerprint(roots,files,strings,ids,G);
        if(state.mode>=2) {
            std::ifstream f(tape_path(),std::ios::binary);
            metadata meta; f.read(reinterpret_cast<char*>(&meta),sizeof meta);
            if(!f || std::memcmp(&meta,&state.meta,sizeof meta)) throw std::runtime_error("Header/corpus prefix mismatch");
            load_vector(f,state.tokens);load_vector(f,state.names);load_vector(f,state.text);
            load_vector(f,state.finds);load_vector(f,state.resolves);load_vector(f,state.provenance);
            load_vector(f,state.endpoints);load_vector(f,state.steps);
            if(f.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("trailing trace bytes");
        }
        return true;
    } catch(const std::exception& e) { bad(e.what());return false; }
}
inline void start() noexcept { state.started=std::chrono::steady_clock::now(); }
inline server_status next(semantic_input& input,semantic_token& out,string_table& strings) noexcept {
    if(state.mode==3) {
        if(state.token_at>=state.tokens.size()) return bad("token trace exhausted");
        const auto& r=state.tokens[state.token_at++]; out=r.token;
        if(out.kind==token_kind::identifier && !strings.contains(out.identifier)) {
            const auto id=out.identifier.value();
            if(id>=state.names.size()) return bad("missing identifier spelling");
            const auto& n=state.names[id];
            if(n.begin>state.text.size() || n.length>state.text.size()-n.begin) return bad("invalid spelling range");
            string_id actual;
            const auto status=strings.intern({state.text.data()+n.begin,n.length},actual);
            if(!succeeded(status) || actual!=out.identifier) return bad("identifier order changed");
        }
        return r.status;
    }
    const auto status=input.next(out);
    if(state.mode==1) try {
        state.tokens.push_back({out,status});
        if(out.kind==token_kind::identifier) {
            const auto id=out.identifier.value();
            if(state.names.size()<=id) state.names.resize(id+1);
            auto& n=state.names[id];
            if(n.length==0) {
                const auto value=strings.get(out.identifier);
                if(value.empty() || state.text.size()+value.size()>0xffffffffULL) return bad("invalid recorded identifier");
                n={static_cast<std::uint32_t>(state.text.size()),static_cast<std::uint32_t>(value.size())};
                state.text.insert(state.text.end(),value.begin(),value.end());
            }
        }
    } catch(...) { bad("token capture allocation failed"); }
    return status;
}
template<class F> identity_ref find(identity_ref scope,string_id name,identity_kind kind,F original) noexcept {
    if(state.mode==4) {
        if(state.find_at>=state.finds.size()) {bad("identity trace exhausted");return {};}
        const auto& r=state.finds[state.find_at++];
        if(state.verify && (r.scope!=scope || r.name!=name || r.kind!=kind)) {bad("identity input/order mismatch");return {};}
        return r.result;
    }
    const auto result=original();
    if(state.mode==1) try {state.finds.push_back({scope,result,name,kind});} catch(...) {bad("find capture failed");}
    return result;
}
inline server_status resolve(identity_space& ids,identity_ref parent,string_id name,identity_kind kind,identity_ref& out) noexcept {
    if(state.mode==4) {
        if(state.resolve_at>=state.resolves.size()) return bad("resolve trace exhausted");
        const auto& r=state.resolves[state.resolve_at++];
        if(state.verify && (r.scope!=parent || r.name!=name || r.kind!=kind)) return bad("resolve input mismatch");
        if(!r.created) {
            if(state.verify && !ids.contains(r.result)) return bad("existing WHO is not live");
            out=r.result;++state.bypassed_existing;return r.status;
        }
        const auto status=ids.resolve(parent,name,kind,out);++state.retained_new;
        return !state.verify || (status==r.status && out==r.result) ? status:bad("new WHO creation changed");
    }
    const auto before=ids.size();
    const auto status=ids.resolve(parent,name,kind,out);
    if(state.mode==1) try {state.resolves.push_back({parent,out,name,kind,status,ids.size()!=before});} catch(...) {bad("resolve capture failed");}
    return status;
}
inline bool same_prov(const prov_record& a,const prov_record& b) noexcept {
    return a.kind==b.kind && a.file==b.file && a.identity==b.identity && a.target==b.target && a.data.raw()==b.data.raw();
}
inline server_status apply_prov(source_map& sources,const prov_record& r) noexcept {
    switch(r.kind) {
    case prov_kind::begin:return sources.begin_root(r.file);
    case prov_kind::end:return sources.end_root();
    case prov_kind::dependency:return sources.add_dependency(r.identity);
    case prov_kind::initialization:return sources.add_initialization(r.target);
    case prov_kind::contribution:return sources.add(r.file,r.data);
    }
    return server_status::project_artifact_invalid;
}
inline server_status provenance(source_map& sources,prov_record r) noexcept {
    if(state.mode==6) {
        if(state.prov_at>=state.provenance.size()) return bad("provenance trace exhausted");
        const auto index=state.prov_at++;
        if(!state.verify) return server_status::success;
        const auto& expected=state.provenance[index];
        return same_prov(r,expected)?expected.status:bad("provenance input/order mismatch");
    }
    r.status=apply_prov(sources,r);
    if(state.mode==1) try {state.provenance.push_back(r);} catch(...) {bad("provenance capture failed");}
    return r.status;
}
inline server_status prov_begin(source_map& s,file_id f) noexcept {prov_record r;r.kind=prov_kind::begin;r.file=f;return provenance(s,r);}
inline server_status prov_end(source_map& s) noexcept {prov_record r;r.kind=prov_kind::end;return provenance(s,r);}
inline server_status prov_dependency(source_map& s,identity_ref id) noexcept {prov_record r;r.kind=prov_kind::dependency;r.identity=id;return provenance(s,r);}
inline server_status prov_init(source_map& s,object_endpoint t) noexcept {prov_record r;r.kind=prov_kind::initialization;r.target=t;return provenance(s,r);}
inline server_status prov_add(source_map& s,file_id f,source_data_ref d) noexcept {prov_record r;r.kind=prov_kind::contribution;r.file=f;r.data=d;return provenance(s,r);}
inline bool finish(source_map& sources) noexcept {
    const auto stopped=std::chrono::steady_clock::now();
    try {
        if(state.failed) return false;
        if((state.mode==3 && state.token_at!=state.tokens.size()) ||
           (state.mode==4 && (state.find_at!=state.finds.size() || state.resolve_at!=state.resolves.size())) ||
           (state.mode==5 && state.endpoint_at!=state.endpoints.size()) ||
           (state.mode==6 && state.prov_at!=state.provenance.size())) return bad("unused trace records"),false;
        const auto restore_begin=std::chrono::steady_clock::now();
        if(state.mode==6) for(const auto& r:state.provenance)
            if(apply_prov(sources,r)!=r.status) return bad("provenance restoration mismatch"),false;
        const auto restored=std::chrono::steady_clock::now();
        if(state.mode==1) {
            std::ofstream f(tape_path(),std::ios::binary|std::ios::trunc);
            f.write(reinterpret_cast<const char*>(&state.meta),sizeof state.meta);
            save_vector(f,state.tokens);save_vector(f,state.names);save_vector(f,state.text);
            save_vector(f,state.finds);save_vector(f,state.resolves);save_vector(f,state.provenance);
            save_vector(f,state.endpoints);save_vector(f,state.steps);
            if(!f) throw std::runtime_error("trace write failed");
        }
        std::printf("source_resolution01,mode=%d,verify=%d,stage_ms=%.6f,restore_ms=%.6f,tokens=%zu,finds=%zu,resolves=%zu,endpoints=%zu,provenance=%zu,retained_new_resolve=%llu,bypassed_existing_resolve=%llu\n",
            state.mode,state.verify,std::chrono::duration<double,std::milli>(stopped-state.started).count(),
            std::chrono::duration<double,std::milli>(restored-restore_begin).count(),state.tokens.size(),state.finds.size(),
            state.resolves.size(),state.endpoints.size(),state.provenance.size(),state.retained_new,state.bypassed_existing);
        return true;
    } catch(const std::exception& e) {bad(e.what());return false;}
}
} // namespace cw::server::sres01
