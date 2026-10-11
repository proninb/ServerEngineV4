// Run against REAL isolated ServerEngineV4 graph.cpp, not mock Graph.
#include "server_engine/project/graph/graph.hpp"
#include "benchmarks/v3_27_derived_cache.hpp"
#include <cassert>
#include <iostream>
using namespace cw::server;
int main() {
    graph G;
    v327_derived::cache cache;
    v327_derived::counters stats;
    const auto base=G.intrinsic(intrinsic_type::signed_int);
    const auto other=G.intrinsic(intrinsic_type::unsigned_int);
    assert(base && other);
    type_ref p1,p2,a1,a2,q1,r1,x;
    assert(succeeded(cache.intern(G,base,derived_type_kind::pointer,0,p1,&stats)));
    assert(G.derived_type_count()==1);
    assert(succeeded(cache.intern(G,base,derived_type_kind::pointer,0,p2,&stats)));
    assert(p1==p2 && G.derived_type_count()==1);
    assert(succeeded(cache.intern(G,base,derived_type_kind::bounded_array,3,a1,&stats)));
    assert(succeeded(cache.intern(G,base,derived_type_kind::bounded_array,7,a2,&stats)));
    assert(a1 != a2 && a1!=p1 && a2!=p1 && G.derived_type_count()==3);
    assert(succeeded(cache.intern(G,other,derived_type_kind::pointer,0,q1,&stats)));
    assert(q1!=p1 && G.derived_type_count()==4);
    assert(succeeded(cache.intern(G,p1,derived_type_kind::const_qualified,0,r1,&stats)));
    assert(G.derived_type_count()==5);
    // Must reject bad bounded-array payload and never cache it.
    assert(!succeeded(cache.intern(G,base,derived_type_kind::bounded_array,0,x,&stats)));
    assert(!x);
    assert(!succeeded(cache.intern(G,base,derived_type_kind::bounded_array,0,x,&stats)));
    assert(!x);
    assert(stats.calls==8 && stats.hits==1 && stats.misses==7);
    assert(stats.graph_created==5 && cache.size()==5);
    // Force several rehashes with distinct 64-bit array-bound payloads.
    for(std::uint64_t n=100;n<4200;++n){
        type_ref created,again;
        assert(succeeded(cache.intern(G,base,derived_type_kind::bounded_array,n,created)));
        assert(succeeded(cache.intern(G,base,derived_type_kind::bounded_array,n,again)));
        assert(created==again);
    }
    assert(cache.size()==4105 && G.derived_type_count()==4105);
    // Cache is per-producer/Graph. Separate Graph generates its own WHERE.
    graph H;
    v327_derived::cache separate;
    type_ref h;
    assert(succeeded(separate.intern(H,H.intrinsic(intrinsic_type::signed_int),
        derived_type_kind::bounded_array,7,h)));
    assert(h!=a2 && H.derived_type_count()==1);
    std::cout<<"V327_REAL_GRAPH_DERIVED_CACHE_PASS\n";
}
