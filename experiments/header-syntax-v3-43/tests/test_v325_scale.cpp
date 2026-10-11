// Real Graph storage scale test; no mock, benchmark-only overlay required.
#include "server_engine/project/graph/graph.hpp"
#include <bit>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace cw::server;
int main(){
    constexpr std::uint32_t types=23333,fields=708668;
    constexpr std::uint32_t extra=fields-types*30;
    static_assert(extra<types);
    graph G;
    const auto intrinsic=G.intrinsic(intrinsic_type::signed_int);
    assert(intrinsic);
    for(std::uint32_t i=0;i<types;++i){
        const auto id=identity_ref::from_raw((2u<<30)|(i+2));
        type_handle t;
        assert(succeeded(G.declare_record(id,graph_record_kind::struct_type,t)));
        assert(t.value()==i+1);
        const std::uint32_t count=30+(i<extra?1:0);
        assert(succeeded(G.begin_direct_record(t,count)));
        for(std::uint32_t m=0;m<count;++m){
            const auto name=std::bit_cast<string_id>(m+1);
            const auto value=construction_value::constant(construction_kind::unsigned_integer,m+1);
            assert(succeeded(G.append_direct_member(t,{name,intrinsic,graph_member_access::public_access,{}},value)));
        }
        assert(succeeded(G.commit_prevalidated_record(t,graph_record_kind::struct_type,{},false)));
    }
    assert(G.type_count()==types);
    assert(G.member_count()==fields);
    assert(succeeded(G.finish_prevalidated_member_index()));
    for(std::uint32_t i=0;i<types;i+=103){
        const auto handle=type_handle{}; // graph::find_type resolves existing identities.
        const auto id=identity_ref::from_raw((2u<<30)|(i+2));
        const auto actual=G.find_type(id);
        assert(actual && actual.value()==i+1);
        const std::uint32_t count=30+(i<extra?1:0);
        for(std::uint32_t m=0;m<count;++m){
            const auto name=std::bit_cast<string_id>(m+1);
            const auto index=G.find_member(actual,name);
            assert(index && index.value()==m);
            const auto* val=G.construction(actual,index);
            assert(val && val->bits()==m+1);
        }
    }
    std::cout<<"V3-25 NATIVE SCALE PASS types="<<G.type_count()
             <<" members="<<G.member_count()<<" lookup=PASS\n";
}
