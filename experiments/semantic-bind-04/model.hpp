#pragma once
// Same Parser and same semantic rules; only representation/lifetime policies.
#include "parser.hpp"
#include "request_storage.hpp"
#include "bound_storage.hpp"
#include "diagnostics.hpp"

namespace sb01 {
enum class variant {baseline,sparse,shared_shape,segmented,combined};
inline variant selected_variant=variant::baseline; // fixed before starting workers
struct policy {bool sparse=false,shared=false,segmented=false;};
inline policy options(variant v){return {v==variant::sparse||v==variant::combined,v==variant::shared_shape||v==variant::combined,v==variant::segmented||v==variant::combined};}
inline const char* variant_name(variant v){switch(v){case variant::baseline:return "baseline";case variant::sparse:return "sparse";case variant::shared_shape:return "shared_shape";case variant::segmented:return "segmented";default:return "combined";}}
inline variant parse_variant(std::string_view name){for(auto v:{variant::baseline,variant::sparse,variant::shared_shape,variant::segmented,variant::combined})if(name==variant_name(v))return v;throw std::runtime_error("unknown storage variant");}
struct binding_result {bound_storage types;std::vector<issue> failures;issue first;std::size_t compatibility_checks=0;};
struct batch {
    ca01::input identities;std::vector<declaration> declarations;request_storage requests;
    issue first;std::size_t excluded_callables=0,excluded_initializers=0;policy flags;
};
inline batch combine(std::vector<local_output>& locals){
    batch out;out.flags=options(selected_variant);
    std::size_t symbols=0,declarations=0,requests=0,nonempty=0;
    for(const auto& l:locals){symbols+=l.symbols.size();declarations+=l.declarations.size();requests+=l.requests.size();nonempty+=!l.requests.empty();}
    ca01::require(symbols<none&&declarations<none,"batch local index limit");
    out.identities.symbols.reserve(symbols);out.declarations.reserve(declarations);out.requests.prepare(out.flags.segmented,requests,nonempty);
    for(auto& local:locals){const auto offset=static_cast<id>(out.identities.symbols.size());const auto def_offset=static_cast<id>(out.declarations.size());
        earliest(out.first,local.failure);out.excluded_callables+=local.excluded_callables;out.excluded_initializers+=local.excluded_initializers;
        for(auto s:local.symbols){if(s.parent!=none)s.parent+=offset;out.identities.symbols.push_back(s);}
        for(auto d:local.declarations){d.symbol+=offset;out.declarations.push_back(d);}
        // Resolve local reference offsets before freezing ownership. Segmented
        // mode then moves the vector buffer, constructing no second requests.
        for(auto& r:local.requests){r.scope+=offset;r.owner+=offset;r.definition+=def_offset;}
        out.requests.retain(local.shapes);out.requests.append(local.requests);
    }
    out.requests.freeze();out.identities.prepare();return out;
}
struct catalog_entry {event first=UINT64_MAX,complete=UINT64_MAX,alias_at=UINT64_MAX;I alias=I::none;bool has_graph=false,record=true,union_type=false;};
struct binding_key {id scope;std::string_view name;friend bool operator==(binding_key,binding_key)=default;};
struct binding_hash {std::size_t operator()(binding_key k)const{return static_cast<std::size_t>(ca01::mix(ca01::hash_text(k.name)^ca01::mix(k.scope)));}};

template<bool Sparse,bool Shared>inline binding_result bind_impl(const batch& b,const ca01::result& c,ca01::pool& workers){
    binding_result out;out.first=b.first;const auto tasks=(std::min)(workers.size,b.requests.size());
    out.types.initialize<Shared>(b.requests.size(),b.requests);out.failures.resize(Sparse?tasks:b.requests.size());
    std::vector<catalog_entry> entries(b.identities.symbols.size());
    std::vector<id> ordered(b.declarations.size());std::iota(ordered.begin(),ordered.end(),0);
    std::sort(ordered.begin(),ordered.end(),[&](id x,id y){return b.declarations[x].begin<b.declarations[y].begin;});
    std::unordered_map<binding_key,id,binding_hash> lookup;std::unordered_map<std::uint64_t,bool> root_families;
    for(auto i:ordered){const auto& d=b.declarations[i];if(d.kind==declaration_kind::name_space)continue;
        const auto rep=c.representative[d.symbol];auto& e=entries[rep];const bool record=d.kind==declaration_kind::record;
        e.first=(std::min)(e.first,d.begin);
        if(record){const bool is_union=d.record_kind==K::kw_union;
            const auto family_key=(event(b.identities.symbols[d.symbol].worker)<<32)|rep;
            const auto [family,inserted]=root_families.emplace(family_key,is_union);
            if(!inserted){++out.compatibility_checks;if(family->second!=is_union)earliest(out.first,{error_kind::record_family,d.begin,d.source});}
            if(d.open!=UINT64_MAX){
                if(e.has_graph){++out.compatibility_checks;
                    if(!e.record)earliest(out.first,{error_kind::alias_conflict,d.open,d.open_source});
                    else if(e.union_type!=is_union)earliest(out.first,{error_kind::record_family,d.open,d.open_source});
                }else{e.has_graph=true;e.record=true;e.union_type=is_union;}
                e.complete=(std::min)(e.complete,d.complete);
            }
        }else{if(e.has_graph){++out.compatibility_checks;if(e.record||e.alias!=d.alias)earliest(out.first,{error_kind::alias_conflict,d.begin,d.source});}
            else{e.has_graph=true;e.record=false;e.alias=d.alias;e.alias_at=d.complete;}}
        const auto& s=b.identities.symbols[d.symbol];lookup.emplace(binding_key{c.representative[s.parent],s.name},rep);
    }
    const auto& immutable_lookup=lookup;
    workers.run(tasks,[&](auto task,auto){diagnostics_writer<Sparse> diagnostics(out.failures,task);
        // One segment search per range, then linear access. No binary search
        // or random index lookup per request in the parallel Binder hot loop.
        b.requests.range(task*b.requests.size()/tasks,(task+1)*b.requests.size()/tasks,[&](std::size_t i,const request& r){
            auto& t=out.types.writable<Shared>(i);t.builtin=r.type.builtin;
            if constexpr(!Shared)t.layers.assign(r.type.layers.begin(),r.type.layers.end());
            if(r.type.name.empty())return;
            id scope=c.representative[r.scope],target=none;
            for(;;){const auto found=immutable_lookup.find({scope,r.type.name});
                if(found!=immutable_lookup.end()&&entries[found->second].first<=r.at){target=found->second;break;}
                const auto parent=b.identities.symbols[scope].parent;if(parent==none)break;scope=c.representative[parent];}
            auto failure=[&](error_kind k){diagnostics.fail(i,{k,r.at,k==error_kind::incomplete_type?r.incomplete_source:r.source});};
            if(target==none){failure(error_kind::unknown_type);return;}
            const auto& e=entries[target];
            if(e.alias_at<=r.at){if(r.base)failure(error_kind::incomplete_type);else t.builtin=e.alias;return;}
            t.entity=target;
            const auto& layers=r.type.layers;
            const bool indirect=std::any_of(layers.begin(),layers.end(),[](auto l){return l.kind==D::pointer||l.kind==D::lvalue_reference||l.kind==D::rvalue_reference;});
            if((r.base||!indirect)&&e.complete>r.at)failure(error_kind::incomplete_type);
            if(r.base&&e.union_type)failure(error_kind::incomplete_type);
        });diagnostics.finish();
    });
    for(auto f:out.failures)earliest(out.first,f);
    std::vector<std::vector<std::size_t>> fields(b.declarations.size());
    b.requests.range(0,b.requests.size(),[&](std::size_t i,const request& r){fields[r.definition].push_back(i);});
    std::vector<id> first_definition(entries.size(),none);
    for(auto di:ordered){const auto& d=b.declarations[di];if(d.kind!=declaration_kind::record||d.complete==UINT64_MAX)continue;
        auto& first=first_definition[c.representative[d.symbol]];if(first==none){first=di;continue;}++out.compatibility_checks;
        const auto& a=fields[first];const auto& z=fields[di];bool same=a.size()==z.size();
        for(std::size_t j=0;same&&j<a.size();++j)same=b.requests[a[j]].member==b.requests[z[j]].member&&b.requests[a[j]].base==b.requests[z[j]].base&&equal(out.types[a[j]],out.types[z[j]]);
        if(!same)earliest(out.first,{error_kind::definition_conflict,d.complete,d.complete_source});
    }
    return out;
}
inline binding_result bind(const batch& b,const ca01::result& c,ca01::pool& workers){
    if(b.flags.shared)return b.flags.sparse?bind_impl<true,true>(b,c,workers):bind_impl<false,true>(b,c,workers);
    return b.flags.sparse?bind_impl<true,false>(b,c,workers):bind_impl<false,false>(b,c,workers);
}
inline std::size_t primary_bytes(const batch& b,const binding_result& result){
    std::size_t bytes=b.identities.symbols.capacity()*sizeof(ca01::symbol)+b.declarations.capacity()*sizeof(declaration)+b.requests.capacity()*sizeof(request)+result.types.record_bytes()+result.types.copied_modifier_bytes()+result.failures.capacity()*sizeof(issue);
    for(const auto& arena:b.requests.owner()->shape_owners)bytes+=arena->payload.capacity()*sizeof(modifier);return bytes;
}
}
