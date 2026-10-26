#pragma once
// Feasibility kernel only: input is already typed and final-target resolved.
// No Source parsing, binding database, value journal or Graph ABI changes.
#include "project/graph/graph.hpp"
#include "project/source/source_map.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace pls10c1 {
using namespace cw::server;
inline void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
inline void status(server_status s){require(succeeded(s),"Graph/SourceMap API failed");}
struct order {
    std::uint32_t source=0,operation=0;
    auto operator<=>(const order&)const=default;
};
struct provenance {
    order event;
    file_id file;
    std::uint32_t offset=0,length=0;
    bool operator==(const provenance&)const=default;
};
// Metadata only: losing values are never retained.
struct occurrence {object_endpoint target;provenance at;};
struct warning {object_endpoint target;provenance previous,current;};
struct operation {construction_value value;type_ref type;provenance at;};
inline std::uint64_t key(object_endpoint e){return (std::uint64_t(e.object.value())<<32)|e.member.value();}

class direct_commit {
    graph& G;
    // Public Graph APIs expose neither initialization slot nor Source order.
    // This duplicate endpoint lookup is explicit experimental scratch, not a
    // claimed optimal implementation. It holds metadata, never write values.
    std::unordered_map<std::uint64_t,occurrence> latest;
    std::vector<occurrence> losers;
public:
    std::uint64_t submissions=0,graph_calls=0,skipped_older=0;
    explicit direct_commit(graph& g):G(g){}
    void apply(object_endpoint target,const operation& op){
        ++submissions;const auto k=key(target);require(k!=0,"Missing final target");
        const auto found=latest.find(k);
        if(found!=latest.end()&&!(found->second.at.event<op.at.event)){
            require(found->second.at.event!=op.at.event,"Duplicate event for same final cell");
            losers.push_back({target,op.at});++skipped_older;return;
        }
        bool replaced=false;
        status(G.add_resolved_initialization(target,op.type,op.value,replaced));++graph_calls;
        if(found==latest.end())latest.emplace(k,occurrence{target,op.at});
        else {losers.push_back(found->second);found->second={target,op.at};}
    }
    std::size_t cells()const{return latest.size();}
    std::size_t losing_events()const{return losers.size();}
    // Cold diagnostic/provenance projection, after all targets are resolved.
    // One occurrence per operation, containing no value or syntax.
    std::vector<occurrence> occurrences()const{
        auto all=losers;all.reserve(all.size()+latest.size());
        for(const auto& [k,v]:latest)all.push_back(v);
        std::sort(all.begin(),all.end(),[](const auto& a,const auto& b){return std::tuple{key(a.target),a.at.event}<std::tuple{key(b.target),b.at.event};});return all;
    }
    std::vector<warning> diagnostics()const{
        const auto all=occurrences();std::vector<warning> result;
        for(std::size_t i=1;i<all.size();++i)if(all[i-1].target==all[i].target)
            result.push_back({all[i].target,all[i-1].at,all[i].at});
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return std::tuple{a.current.event,key(a.target)}<std::tuple{b.current.event,key(b.target)};});return result;
    }
    void provenance_to(source_map& map,const std::vector<file_id>& roots)const{
        auto all=occurrences();
        std::stable_sort(all.begin(),all.end(),[](const auto& a,const auto& b){return a.at.event<b.at.event;});
        std::size_t next=0;
        for(std::size_t root=0;root<roots.size();++root){status(map.begin_root(roots[root]));
            while(next<all.size()&&all[next].at.event.source==root){const auto& e=all[next++];
                status(map.add(e.at.file,source_data_ref::object(e.target.object)));status(map.add_initialization(e.target));}
            status(map.end_root());
        }
        require(next==all.size(),"Source order root absent");
    }
};

// The ordinary operation is held only while target resolution reports pending.
// A2 remains responsible for final bindings; no alternative alias algorithm.
struct pending_operation {std::uint32_t request;operation write;};
template<class Resolve>void finish(std::vector<pending_operation>& pending,direct_commit& sink,Resolve&& resolve){
    for(const auto& p:pending)sink.apply(resolve(p.request),p.write);
    std::vector<pending_operation>{}.swap(pending);
}
}
