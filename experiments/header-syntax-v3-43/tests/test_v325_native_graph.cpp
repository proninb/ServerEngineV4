// Run against an isolated REAL ServerEngineV4 graph.cpp, not mocks.
#include "server_engine/project/graph/graph.hpp"
#include <bit>
#include <cassert>
#include <iostream>
using namespace cw::server;
int main(){
    const auto a=std::bit_cast<string_id>(std::uint32_t{1});
    const auto b=std::bit_cast<string_id>(std::uint32_t{2});
    const auto who=identity_ref::from_raw((2u<<30) | 2u);
    assert(who && who.kind()==identity_kind::type);
    graph G;
    type_handle handle;
    assert(succeeded(G.declare_record(who,graph_record_kind::struct_type,handle)));
    assert(succeeded(G.begin_direct_record(handle,2)));
    const auto itype=G.intrinsic(intrinsic_type::signed_int);
    assert(itype);
    assert(succeeded(G.append_direct_member(handle,{a,itype,graph_member_access::public_access,{}},{})));
    assert(succeeded(G.append_direct_member(handle,{b,itype,graph_member_access::private_access,{}},{})));
    const auto value=construction_value::constant(construction_kind::unsigned_integer,7);
    assert(succeeded(G.set_direct_construction(handle,1,value)));
    assert(G.direct_record_member(handle,0)->name == a);
    assert(succeeded(G.commit_prevalidated_record(handle,graph_record_kind::struct_type,{},false)));
    // No index available yet: producer uses contiguous members for cold nested paths.
    assert(G.members(handle).size()==2);
    assert(succeeded(G.finish_prevalidated_member_index()));
    const auto ia=G.find_member(handle,a),ib=G.find_member(handle,b);
    assert(ia && ib && ia.value()==0 && ib.value()==1);
    assert(G.member(handle,ib)->name==b);
    assert(G.construction(handle,ib)->bits()==7);
    // Cannot finalize twice or mutate a committed record.
    assert(!succeeded(G.finish_prevalidated_member_index()));
    assert(!succeeded(G.append_direct_member(handle,{a,itype,graph_member_access::public_access,{}},{})));
    graph H;
    type_handle h;
    assert(succeeded(H.declare_record(who,graph_record_kind::struct_type,h)));
    assert(succeeded(H.begin_direct_record(h,1)));
    assert(!succeeded(H.append_direct_member(h,{},{})));
    H.abort_direct_record(h);
    assert(H.member_count()==0);
    std::cout << "V3-25 REAL GRAPH COMMIT / INDEX / ABORT PASS\n";
}
