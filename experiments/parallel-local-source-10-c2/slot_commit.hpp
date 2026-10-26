#pragma once
#include "../parallel-local-source-10-c1/direct_commit.hpp"

namespace pls10c2 {
using namespace pls10c1;
// Both ready and deferred targets use the same encoding before Graph lookup.
inline object_endpoint normalize(const graph& G,object_endpoint endpoint){
    if(endpoint.member.is_path()){
        endpoint_path_record record;require(G.endpoint_path(endpoint.member.path(),record),"Invalid endpoint path");
        const auto steps=G.endpoint_path_steps(endpoint.member.path());
        if(steps.size()==1&&steps[0].kind==endpoint_path_step_kind::member&&steps[0].value<=endpoint_path_handle::maximum_slot)
            endpoint.member=endpoint_ref::from_raw(static_cast<std::uint32_t>(steps[0].value));
    }
    return endpoint;
}
class slot_commit {
    struct metadata {provenance at{};bool present=false;};
    struct lost {std::uint32_t slot;provenance at;};
    graph& G;
    std::vector<metadata> metadata_by_slot;
    std::vector<lost> losers;
    std::size_t populated=0;
    struct decision_context {slot_commit& owner;const operation& op;};
    static server_status decide(void* context,std::uint32_t slot,bool,bool& accept)noexcept{
        auto& [self,op]=*static_cast<decision_context*>(context);
        try {
            if(slot>=self.metadata_by_slot.size())self.metadata_by_slot.resize(std::size_t(slot)+1);
            auto& current=self.metadata_by_slot[slot];
            if(current.present){
                if(current.at.event==op.at.event)return server_status::project_configuration_invalid;
                accept=current.at.event<op.at.event;
                // Push before changing current metadata: allocation failure
                // leaves both Graph value and current winner unchanged.
                self.losers.push_back({slot,accept?current.at:op.at});
                if(accept)current.at=op.at;
            }else {accept=true;current={op.at,true};++self.populated;}
            return server_status::success;
        }catch(...){return server_status::io_error;}
    }
public:
    std::uint64_t submissions=0,graph_calls=0,skipped_older=0,decisions=0;
    explicit slot_commit(graph& g):G(g){}
    initialization_apply_result apply(object_endpoint endpoint,const operation& op){
        ++submissions;endpoint=normalize(G,endpoint);
        decision_context context{*this,op};initialization_apply_result result;
        status(G.apply_resolved_initialization(endpoint,op.type,op.value,decide,&context,result));++decisions;
        if(result.applied)++graph_calls;else ++skipped_older;return result;
    }
    std::size_t cells()const{return populated;}
    std::size_t losing_events()const{return losers.size();}
    std::size_t metadata_capacity_bytes()const{return metadata_by_slot.capacity()*sizeof(metadata);}
    std::vector<occurrence> occurrences()const{
        const auto entries=G.initialization_entries();std::vector<occurrence> all;all.reserve(losers.size()+populated);
        for(std::size_t slot=0;slot<metadata_by_slot.size();++slot)if(metadata_by_slot[slot].present){require(slot<entries.size(),"Graph reset during producer lifetime");all.push_back({entries[slot].target,metadata_by_slot[slot].at});}
        for(const auto& l:losers){require(l.slot<entries.size(),"Graph reset during diagnostics");all.push_back({entries[l.slot].target,l.at});}
        std::sort(all.begin(),all.end(),[](const auto& a,const auto& b){return std::tuple{key(a.target),a.at.event}<std::tuple{key(b.target),b.at.event};});return all;
    }
    std::vector<warning> diagnostics()const{
        const auto all=occurrences();std::vector<warning> out;
        for(std::size_t i=1;i<all.size();++i)if(all[i-1].target==all[i].target)out.push_back({all[i].target,all[i-1].at,all[i].at});
        std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return std::tuple{a.current.event,key(a.target)}<std::tuple{b.current.event,key(b.target)};});return out;
    }
    void provenance_to(source_map& map,const std::vector<file_id>& roots)const{
        auto all=occurrences();std::stable_sort(all.begin(),all.end(),[](const auto& a,const auto& b){return a.at.event<b.at.event;});std::size_t next=0;
        for(std::size_t root=0;root<roots.size();++root){status(map.begin_root(roots[root]));
            while(next<all.size()&&all[next].at.event.source==root){const auto& e=all[next++];status(map.add(e.at.file,source_data_ref::object(e.target.object)));status(map.add_initialization(e.target));}
            status(map.end_root());}
        require(next==all.size(),"Source root missing");
    }
};
template<class Resolve>void finish(std::vector<pending_operation>& pending,slot_commit& sink,Resolve&& resolve){
    for(const auto& p:pending)(void)sink.apply(resolve(p.request),p.write);std::vector<pending_operation>{}.swap(pending);
}
}
