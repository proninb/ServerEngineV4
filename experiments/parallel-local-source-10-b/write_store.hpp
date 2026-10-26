#pragma once
// Generated binder includes this after write/typed_endpoint have been declared.
#include "registry.hpp"
#include "selector.hpp"
#include <optional>
#include <utility>
namespace pls10b {
struct losing_origin {id cell;origin at;order event;};

class write_store {
    bool compact=enabled();
    std::vector<write> rows;
    const std::vector<typed_endpoint>* endpoints=nullptr;
    const std::vector<endpoint_path_step>* paths=nullptr;
    std::uint64_t operations=0;
public:
    using value_type=write;
    std::optional<registry> index;
    std::vector<losing_origin> losers;
    write_store(){if(compact)index.emplace();}
    void attach(const std::vector<typed_endpoint>& e,const std::vector<endpoint_path_step>& p){endpoints=&e;paths=&p;if(compact)stats={};}
    void push_back(write value){
        if(operations>=none)throw failure("Too many Source writes");
        value.sequence=static_cast<id>(operations++);
        if(!compact){rows.push_back(value);return;}
        const auto endpoint=(*endpoints)[value.target];bool indirect=false;
        for(id i=0;i<endpoint.steps.count;++i){pls03::count(pls03::deref_scan_steps);if((*paths)[endpoint.steps.begin+i].kind==endpoint_path_step_kind::dereference){indirect=true;break;}}
        value.indirect=indirect;
        if(indirect){rows.push_back(value);++stats.indirect;return;}
        ++stats.direct;const auto [entry,row]=index->direct(endpoint.who,endpoint.type,endpoint.steps,static_cast<id>(rows.size()),*paths);
        value.target=entry;
        if(row==rows.size()){rows.push_back(value);++stats.direct_winners;}
        else {const auto previous=rows[row];losers.push_back({entry,previous.at,previous.event});value.sequence=previous.sequence;rows[row]=value;}
    }
    std::uint64_t operation_count()const{return operations;}
    bool is_compact()const{return compact;}
    registry take_index(){if(!compact||!index)throw failure("Sparse writes/index strategy mismatch");return std::move(*index);}
    auto begin(){return rows.begin();}auto end(){return rows.end();}
    auto begin()const{return rows.begin();}auto end()const{return rows.end();}
    auto data(){return rows.data();}auto data()const{return rows.data();}
    std::size_t size()const{return rows.size();}std::size_t capacity()const{return rows.capacity();}
    bool empty()const{return rows.empty();}
    write& operator[](std::size_t i){return rows[i];}const write& operator[](std::size_t i)const{return rows[i];}
    void resize(std::size_t n){rows.resize(n);}
    std::size_t extra_bytes()const{return (index?index->bytes()+index->extra_bytes():0)+losers.capacity()*sizeof(losing_origin);}
    operator std::vector<write>() && {return std::move(rows);}
};
}
