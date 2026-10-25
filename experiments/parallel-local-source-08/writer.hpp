#pragma once
#include "../parallel-local-source-07-b1/owned_reader.hpp"
#include "../../server_engine/project/source/source_map.hpp"
#include <algorithm>
#include <numeric>

namespace pls08 {
using namespace pls07b1;
inline void require(bool good,const char* text){if(!good)throw pls02::failure(text);}
inline void status(server_status s,const char* text){if(!succeeded(s))throw pls02::failure(std::string(text)+" status="+std::to_string(static_cast<unsigned>(s)));}
struct catalog {
    const graph& G;const file_context& files;std::size_t header_objects;domain_id stamp{1,1};
    domain_id domain()const{return stamp;}
    std::size_t object_count()const{return G.object_count();}
    object_info object_at(std::size_t i)const{const auto h=G.object_at(i);const auto* p=G.find(h);construction_value v{};const bool cold=p->construction_slot()!=0||p->non_default_initializer();if(cold)require(G.construction(h,v),"Cold object value unavailable");
        return {G.identity(h),p->type,p->state&graph_object_flag_mask,cold?(i<header_objects?construction_origin::header_default:construction_origin::explicit_source):construction_origin::none,v};}
    std::optional<object_info> object(identity_ref who)const{const auto h=G.find_object(who);if(!h)return {};return object_at(h.value()-1);}
    bool contains_type(type_ref t)const{return G.contains(t);}
    bool contains_file(file_id f)const{return files.contains(f);}
};
struct output {std::vector<object_endpoint> endpoints;std::vector<link_handle> links;std::vector<bool> cold_values;
    std::size_t source_values=0,source_bindings=0,provenance_events=0;};
struct emission {event_key event;file_id physical;source_data_ref data;object_endpoint initialization{};bool is_value=false;};

// Explicit feasibility backend: does not promote projection_only to the B1
// closed-supported token. Seed and supported-input checks are local to PLS08.
template<FinalStateReader R>output write_supported_projection(const R& reader,const catalog& c,graph& G,source_map& sources,
    std::size_t seed_links,std::size_t seed_initializations,bool reverse){
    require(&c.G==&G,"Writer catalog/seed mismatch");
    require(inspect(reader,c,false)==issue::ok,"Writer contract inspection failed");
    require(G.link_count()==seed_links&&G.initialization_count()==seed_initializations,"Seed contains a Source overlay");
    output out;out.endpoints.resize(reader.cell_count());out.links.resize(reader.cell_count());out.cold_values.resize(reader.cell_count());
    const auto ordinal=[&](std::size_t i,std::size_t n){return reverse?n-1-i:i;};
    for(std::size_t j=0;j<reader.cell_count();++j){const auto i=ordinal(j,reader.cell_count());const auto cell=reader.cell_at(i);endpoint_ref endpoint;
        // Match the established direct-member encoding; a path wrapper for the
        // same member is a different raw Graph index key, not a second identity.
        if(cell.steps.size()==1&&cell.steps[0].kind==endpoint_path_step_kind::member&&cell.steps[0].value<=endpoint_path_handle::maximum_slot)
            endpoint=endpoint_ref::from_raw(static_cast<std::uint32_t>(cell.steps[0].value));
        else {endpoint_path_handle path;status(G.intern_resolved_endpoint_path(cell.root_type,cell.steps,cell.value_type,path),"intern_resolved_endpoint_path");endpoint=endpoint_ref::from_path(path);}
        require(bool(endpoint),"Writer cannot encode canonical endpoint");out.endpoints[i]={cell.who,endpoint};
    }
    std::vector<std::vector<emission>> roots(reader.tasks().size());
    const auto emit=[&](provenance p,source_data_ref data,object_endpoint ep={},bool value=false){require(p.event.source_order<roots.size(),"SourceMap root rank missing");
        roots[p.event.source_order].push_back({p.event,p.target.file,data,ep,value});++out.provenance_events;};
    for(std::size_t j=0;j<reader.binding_count();++j){const auto b=reader.binding_at(ordinal(j,reader.binding_count()));link_handle link;bool replaced=false;
        status(G.rebind_resolved_link(out.endpoints[b.source.value],out.endpoints[b.target.value],link,replaced),"rebind_resolved_link");
        out.links[b.target.value]=link;emit(b.declaration,source_data_ref::link(link));++out.source_bindings;
    }
    for(std::size_t j=0;j<reader.value_count();++j){const auto v=reader.value_at(ordinal(j,reader.value_count()));bool replaced=false;
        const auto cell=reader.cell_at(v.target.value);auto type=cell.value_type;derived_type_record qualifier;bool immutable=false;
        while(G.derived(type,qualifier)&&(qualifier.kind==derived_type_kind::const_qualified||qualifier.kind==derived_type_kind::volatile_qualified)){
            immutable|=qualifier.kind==derived_type_kind::const_qualified;type=qualifier.child;}
        if(immutable){const auto object=c.object(cell.who);construction_value cold;object_initialization_record prior;
            require(v.declaration_initializer&&cell.steps.empty()&&object&&object->cold_origin==construction_origin::explicit_source&&
                (object->semantic_flags&graph_object_non_default_initializer)&&G.construction(G.find_object(cell.who),cold)&&cold==v.value&&
                !G.initialization(out.endpoints[v.target.value],prior),"Const declaration requires matching native cold object construction; no Graph init API for const writes");
            out.cold_values[v.target.value]=true;
        }else status(G.add_resolved_initialization(out.endpoints[v.target.value],cell.value_type,v.value,replaced),"add_resolved_initialization");
        emit(v.source,source_data_ref::object(out.endpoints[v.target.value].object),out.endpoints[v.target.value],!out.cold_values[v.target.value]);++out.source_values;
    }
    for(std::size_t i=0;i<reader.declaration_count();++i){const auto d=reader.declaration_at(i);emit({d.event,d.producer_root,d.name,d.statement},source_data_ref::object(d.who));}
    // n-1 transitions carry every duplicate occurrence. Retain contributor roots
    // in native SourceMap without feeding full operation history to the writer.
    for(std::size_t i=0;i<reader.warning_count();++i){const auto w=reader.warning_at(i);const auto ep=out.endpoints[w.target.value];
        const auto data=w.rebind?source_data_ref::link(out.links[w.target.value]):source_data_ref::object(ep.object);
        require(bool(data),"Warning provenance has no final Graph datum");
        emit({w.previous_event,w.previous_root,w.previous,w.previous},data,ep,!w.rebind&&!out.cold_values[w.target.value]);
        emit({w.current_event,w.current_root,w.current,w.current},data,ep,!w.rebind&&!out.cold_values[w.target.value]);
    }
    for(std::size_t root=0;root<roots.size();++root){auto& events=roots[root];
        std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.event<b.event;});
        status(sources.begin_root(reader.tasks()[root].file),"SourceMap begin_root");
        for(const auto& e:events){status(sources.add(e.physical,e.data),"SourceMap add");if(e.is_value)status(sources.add_initialization(e.initialization),"SourceMap add_initialization");}
        status(sources.end_root(),"SourceMap end_root");
    }
    return out;
}
}
