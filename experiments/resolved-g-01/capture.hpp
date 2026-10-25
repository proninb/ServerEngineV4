#ifdef CW_PATH_INTERN_02
#include "../resolved-g-02/policy.hpp"
#endif
#pragma once
#include "../../server_engine/project/graph/graph.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace cw::server::resolved_g01 {
struct access {
    template<class V> static void copy_vector(V& out, const V& in) {
        out.reserve(in.capacity());
        out.assign(in.begin(), in.end());
        if (out.capacity() != in.capacity()) throw std::runtime_error("baseline capacity changed");
    }
    template<class A, class B> static bool same(const A& a, const B& b) {
        using T = typename A::value_type;
        static_assert(std::is_trivially_copyable_v<T>);
        return a.size() == b.size() && (a.empty() ||
            std::memcmp(a.data(), b.data(), a.size()*sizeof(T)) == 0);
    }
    static std::unique_ptr<graph> clone(const graph& in);
    static bool equal(const graph& a, const graph& b);
    static std::size_t bytes(const graph& a);
#ifdef CW_PATH_INTERN_02
    static endpoint_path_handle find_slot(const graph&,type_ref,std::span<const endpoint_path_step>,
        std::uint64_t,std::uint32_t,std::size_t&) noexcept;
    static bool path_prepare(graph&,const graph&,unsigned);
    static bool prepare_path_tape(const graph&);
    static bool verify_hashes(const graph&);
#endif
};

enum class code : unsigned { derive, object, complete, path, init, batch, link, count };
struct operation {
    code kind{};
    derived_type_kind modifier{};
    identity_ref who{};
    type_ref type{}, value_type{};
    object_endpoint target{}, source{};
    construction_value value{};
    std::uint64_t payload=0;
    std::size_t begin=0, count=0;
    std::uint32_t flags=0, expected=0;
    bool replaced=false;
};
struct session {
    bool recording=false, failed=false;
    unsigned depth=0;
    std::unique_ptr<graph> baseline;
    std::vector<operation> operations;
    std::vector<endpoint_path_step> steps;
    std::vector<object_initialization_record> records;
};
inline session state;
using clock = std::chrono::steady_clock;
inline double ms(clock::time_point a, clock::time_point b) {
    return std::chrono::duration<double,std::milli>(b-a).count();
}
inline bool begin(const graph& G) noexcept {
    const auto* option=std::getenv("CW_RESOLVED_G_CAPTURE");
    if (!option || std::strcmp(option,"1")) return true;
    try {
        state=session{};
        state.baseline=access::clone(G);
        state.recording=true;
        return true;
    } catch (...) { std::fprintf(stderr,"resolved_g01 baseline capture failed\n"); return false; }
}

template<class Make,class Finish> struct observation {
    bool active=false, outer=false;
    std::size_t slot=0;
    Finish finish;
    observation(Make make, Finish done) noexcept : finish(done) {
        if (!state.recording) return;
        active=true;
        outer=state.depth++ == 0;
        if (outer) {
            try { slot=state.operations.size(); state.operations.push_back(make()); }
            catch (...) { state.failed=true; outer=false; }
        }
    }
    ~observation() noexcept {
        if (!active) return;
        if (outer) finish(state.operations[slot]);
        --state.depth;
    }
};
template<class Make,class Finish> auto observe(Make make,Finish finish) {
    return observation<Make,Finish>{make,finish};
}
inline operation derived(type_ref child,derived_type_kind modifier,std::uint64_t payload) {
    operation o; o.kind=code::derive; o.type=child; o.modifier=modifier; o.payload=payload; return o;
}
inline operation object(identity_ref who,type_ref type,std::uint32_t flags,construction_value value,bool complete) {
    operation o; o.kind=complete?code::complete:code::object; o.who=who; o.type=type; o.flags=flags; o.value=value; return o;
}
inline operation path(type_ref root,std::span<const endpoint_path_step> steps,type_ref value) {
    operation o; o.kind=code::path; o.type=root; o.value_type=value;
    o.begin=state.steps.size(); o.count=steps.size();
    state.steps.insert(state.steps.end(),steps.begin(),steps.end()); return o;
}
inline operation initialization(object_endpoint target,type_ref type,construction_value value) {
    operation o; o.kind=code::init; o.target=target; o.type=type; o.value=value; return o;
}
inline operation batch(type_ref type,std::span<const object_initialization_record> records) {
    operation o; o.kind=code::batch; o.type=type; o.begin=state.records.size(); o.count=records.size();
    state.records.insert(state.records.end(),records.begin(),records.end()); return o;
}
inline operation link(object_endpoint source,object_endpoint target) {
    operation o; o.kind=code::link; o.source=source; o.target=target; return o;
}

template<bool Verify> bool execute(graph& G) noexcept {
    for (const auto& o:state.operations) {
        server_status status=server_status::project_configuration_invalid;
        std::uint32_t result=0;
        bool replaced=false;
        switch(o.kind) {
        case code::derive: {
            type_ref out;
            status=G.derive_resolved(o.type,o.modifier,o.payload,out); result=out.value(); break;
        }
        case code::object:
        case code::complete: {
            object_handle out;
            status=o.kind==code::object ? G.add_resolved_object(o.who,o.type,out,o.flags,o.value) :
                G.complete_resolved_extern_object(o.who,o.type,out,o.flags,o.value);
            result=out.value(); break;
        }
        case code::path: {
#ifdef CW_PATH_INTERN_02
            if (path_intern02::enabled(path_intern02::skip_all) ||
                (path_intern02::enabled(path_intern02::skip_existing) && o.flags==1)) {
                result=o.expected; status=server_status::success; break;
            }
            path_intern02::supplied_hash=path_intern02::enabled(path_intern02::prehashed)?o.payload:0;
#endif
            endpoint_path_handle out;
            status=G.intern_resolved_endpoint_path(o.type,
                std::span<const endpoint_path_step>{state.steps}.subspan(o.begin,o.count),o.value_type,out);
            result=out.value(); break;
        }
        case code::init: status=G.add_resolved_initialization(o.target,o.type,o.value,replaced); break;
        case code::batch: status=G.add_resolved_initializations(o.type,
            std::span<const object_initialization_record>{state.records}.subspan(o.begin,o.count),replaced); break;
        case code::link: {
            link_handle out; status=G.rebind_resolved_link(o.source,o.target,out,replaced); result=out.value(); break;
        }
        case code::count: return false;
        }
        if (!succeeded(status)) {
            std::fprintf(stderr,"resolved_g01 failed API kind=%u index=%zu status=%u\n",unsigned(o.kind),
                std::size_t(&o-state.operations.data()),unsigned(status));
            return false;
        }
        if constexpr(Verify) if (result!=o.expected || replaced!=o.replaced) {
            std::fprintf(stderr,"resolved_g01 output mismatch kind=%u index=%zu result=%u/%u replaced=%u/%u\n",
                unsigned(o.kind),std::size_t(&o-state.operations.data()),result,o.expected,replaced,o.replaced);
            return false;
        }
    }
    return true;
}

#ifdef CW_PATH_INTERN_02
inline bool finish_path02(const graph& expected) noexcept;
#endif
inline bool finish(const graph& expected) noexcept {
    if (!state.baseline) return true;
    state.recording=false;
    if (state.failed || state.depth) return false;
    const auto* corrupt=std::getenv("CW_RESOLVED_G_CORRUPT");
    if (corrupt && !std::strcmp(corrupt,"1")) {
        for(auto i=state.operations.rbegin();i!=state.operations.rend();++i) {
            if(i->kind==code::init) { i->value.low^=1; break; }
            if(i->kind==code::batch && i->count) { state.records[i->begin+i->count-1].value.low^=1; break; }
        }
    }
    int repetitions=11;
    if(const auto* value=std::getenv("CW_RESOLVED_G_REPEATS")) repetitions=std::clamp(std::atoi(value),1,100);
    std::array<std::uint64_t,static_cast<unsigned>(code::count)> counts{};
    std::uint64_t overwritten=0,init_records=0;
    for(const auto& o:state.operations) {
        ++counts[static_cast<unsigned>(o.kind)]; overwritten+=o.replaced;
        init_records+=o.kind==code::init ? 1 : o.kind==code::batch ? o.count : 0;
    }
    std::printf("resolved_g01_summary,operations=%zu,event_bytes=%zu,tape_capacity_bytes=%zu,header_bytes=%zu,derive=%llu,object=%llu,complete=%llu,path=%llu,init=%llu,batch=%llu,link=%llu,init_records=%llu,overwritten_calls=%llu,final_objects=%zu,final_links=%zu,final_inits=%zu,final_paths=%zu\n",
        state.operations.size(),sizeof(operation),state.operations.capacity()*sizeof(operation)+
        state.steps.capacity()*sizeof(endpoint_path_step)+state.records.capacity()*sizeof(object_initialization_record),
        access::bytes(*state.baseline),counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],
        init_records,overwritten,expected.object_count(),expected.link_count(),expected.initialization_count(),expected.endpoint_path_count());
#ifdef CW_PATH_INTERN_02
    return finish_path02(expected);
#else
    try {
        for(int repeat=-2;repeat<repetitions;++repeat) {
            const auto prepare_start=clock::now();
            auto G=access::clone(*state.baseline);
            const auto started=clock::now();
            const bool success=repeat<0 ? execute<true>(*G) : execute<false>(*G);
            const auto stopped=clock::now();
            const bool parity=success && access::equal(*G,expected);
            const auto verified=clock::now();
            const auto final_bytes=access::bytes(*G);
            G.reset();
            const auto released=clock::now();
            std::printf("resolved_g01,repeat=%d,g_ms=%.6f,prepare_ms=%.6f,verify_ms=%.6f,release_ms=%.6f,parity=%d,final_capacity_bytes=%zu\n",
                repeat,ms(started,stopped),ms(prepare_start,started),ms(stopped,verified),ms(verified,released),parity,final_bytes);
            if(!parity) return false;
        }
        state.baseline.reset();
        return true;
    } catch (...) { std::fprintf(stderr,"resolved_g01 replay allocation failed\n"); return false; }
#endif
}
} // namespace cw::server::resolved_g01

namespace cw::server::resolved_g01 {
inline std::unique_ptr<graph> access::clone(const graph& in) {
 auto out=std::make_unique<graph>();
 out->constructor_defaults=in.constructor_defaults;
 copy_vector(out->identity_locations,in.identity_locations);
 copy_vector(out->types,in.types);
 copy_vector(out->type_identities,in.type_identities);
 copy_vector(out->enum_handles,in.enum_handles);
 copy_vector(out->base_records,in.base_records);
 copy_vector(out->member_records,in.member_records);
 copy_vector(out->member_construction,in.member_construction);
 copy_vector(out->member_name_index,in.member_name_index);
 copy_vector(out->objects,in.objects);
 copy_vector(out->object_identities,in.object_identities);
 copy_vector(out->object_construction,in.object_construction);
 copy_vector(out->endpoint_paths,in.endpoint_paths);
 copy_vector(out->endpoint_path_step_values,in.endpoint_path_step_values);
 copy_vector(out->endpoint_path_index,in.endpoint_path_index);
#ifdef CW_PATH_INTERN_02
 copy_vector(out->endpoint_path_hashes,in.endpoint_path_hashes);
#endif
 copy_vector(out->links,in.links);
 copy_vector(out->object_initializations,in.object_initializations);
 copy_vector(out->initialization_target_index,in.initialization_target_index);
 copy_vector(out->link_target_index,in.link_target_index);
 copy_vector(out->derived_types,in.derived_types);
 copy_vector(out->derived_index,in.derived_index);
 out->member_name_index_count=in.member_name_index_count;
 out->initialization_target_index_count=in.initialization_target_index_count;
 out->link_target_index_count=in.link_target_index_count;
 return out;
}
inline bool access::equal(const graph& a,const graph& b) {
 return same(a.constructor_defaults.entries(),b.constructor_defaults.entries()) &&
 same(a.identity_locations,b.identity_locations) &&
 same(a.types,b.types) &&
 same(a.type_identities,b.type_identities) &&
 same(a.enum_handles,b.enum_handles) &&
 same(a.base_records,b.base_records) &&
 same(a.member_records,b.member_records) &&
 same(a.member_construction,b.member_construction) &&
 same(a.member_name_index,b.member_name_index) &&
 same(a.objects,b.objects) &&
 same(a.object_identities,b.object_identities) &&
 same(a.object_construction,b.object_construction) &&
 same(a.endpoint_paths,b.endpoint_paths) &&
 same(a.endpoint_path_step_values,b.endpoint_path_step_values) &&
 same(a.endpoint_path_index,b.endpoint_path_index) &&
 same(a.links,b.links) &&
 same(a.object_initializations,b.object_initializations) &&
 same(a.initialization_target_index,b.initialization_target_index) &&
 (a.link_target_index.size()==b.link_target_index.size() &&
  std::equal(a.link_target_index.begin(),a.link_target_index.end(),b.link_target_index.begin(),
    [](const auto& x,const auto& y){ return x.key==y.key && x.link==y.link; })) &&
 same(a.derived_types,b.derived_types) &&
 same(a.derived_index,b.derived_index) &&
 a.member_name_index_count==b.member_name_index_count &&
 a.initialization_target_index_count==b.initialization_target_index_count &&
 a.link_target_index_count==b.link_target_index_count;
}
inline std::size_t access::bytes(const graph& a) {
 return a.identity_locations.capacity()*sizeof(decltype(a.identity_locations)::value_type) +
a.types.capacity()*sizeof(decltype(a.types)::value_type) +
a.type_identities.capacity()*sizeof(decltype(a.type_identities)::value_type) +
a.enum_handles.capacity()*sizeof(decltype(a.enum_handles)::value_type) +
a.base_records.capacity()*sizeof(decltype(a.base_records)::value_type) +
a.member_records.capacity()*sizeof(decltype(a.member_records)::value_type) +
a.member_construction.capacity()*sizeof(decltype(a.member_construction)::value_type) +
a.member_name_index.capacity()*sizeof(decltype(a.member_name_index)::value_type) +
a.objects.capacity()*sizeof(decltype(a.objects)::value_type) +
a.object_identities.capacity()*sizeof(decltype(a.object_identities)::value_type) +
a.object_construction.capacity()*sizeof(decltype(a.object_construction)::value_type) +
a.endpoint_paths.capacity()*sizeof(decltype(a.endpoint_paths)::value_type) +
a.endpoint_path_step_values.capacity()*sizeof(decltype(a.endpoint_path_step_values)::value_type) +
a.endpoint_path_index.capacity()*sizeof(decltype(a.endpoint_path_index)::value_type) +
a.links.capacity()*sizeof(decltype(a.links)::value_type) +
a.object_initializations.capacity()*sizeof(decltype(a.object_initializations)::value_type) +
a.initialization_target_index.capacity()*sizeof(decltype(a.initialization_target_index)::value_type) +
a.link_target_index.capacity()*sizeof(decltype(a.link_target_index)::value_type) +
a.derived_types.capacity()*sizeof(decltype(a.derived_types)::value_type) +
a.derived_index.capacity()*sizeof(decltype(a.derived_index)::value_type)
#ifdef CW_PATH_INTERN_02
 + a.endpoint_path_hashes.capacity()*sizeof(std::uint64_t)
#endif
 ;
}
}

#ifdef CW_PATH_INTERN_02
#include "../resolved-g-02/replay.hpp"
#endif
