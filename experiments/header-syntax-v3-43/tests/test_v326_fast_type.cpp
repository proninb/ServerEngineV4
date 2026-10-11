// Compile with -include tests/mock_project/mock.hpp, -I tests/mock_project -I .
#define CW_V326_TYPE_TEST 1
#include "benchmarks/v3_26_fast_type.hpp"
#include <cassert>
#include <limits>
#include <iostream>
using namespace cw::server;
int main(){
    string_table strings;
    identity_space ids(strings);
    graph G;
    v326_type::base_type_cache cache;
    cache.begin_record();
    std::uint64_t hit=0,miss=0;
    const token_kind t[]={token_kind::kw_const,token_kind::identifier,token_kind::kw_volatile};
    const string_id sym[]={{},{7},{}};
    const std::uint64_t nums[]={0,0,0};
    const v316::source_view view{t,sym,nums,&strings,&strings};
    v316::type_spec first;
    assert(v326_type::try_resolve_simple(view,0,3,identity_ref{1},ids,G,cache,first,&hit,&miss));
    assert(miss==1 && hit==0 && first.length==2);
    assert(first.layers[0].kind==derived_type_kind::const_qualified);
    assert(first.layers[1].kind==derived_type_kind::volatile_qualified);
#ifdef TEST_ALIAS
    assert(first.base_kind==type_ref_kind::intrinsic);
    assert(first.base_payload==static_cast<std::uint32_t>(intrinsic_type::unsigned_char));
#else
    assert(first.base_kind==type_ref_kind::named && first.base_payload==7);
#endif
    v316::type_spec second;
    assert(v326_type::try_resolve_simple(view,0,3,identity_ref{1},ids,G,cache,second,&hit,&miss));
    assert(hit==1 && miss==1 && second.base_kind==first.base_kind);
    cache.begin_record();
    v316::type_spec third;
    assert(v326_type::try_resolve_simple(view,0,3,identity_ref{1},ids,G,cache,third,&hit,&miss));
    assert(hit==1 && miss==2); // separate record must resolve again
    // Guard contextual intrinsic and invalid/qualified token grammar.
    const token_kind contextual[]={token_kind::identifier};
    const string_id contextual_name[]={{37}};
    const std::uint64_t one[]={0};
    const v316::source_view c{contextual,contextual_name,one,&strings,&strings};
    v316::type_spec x;
    assert(!v326_type::try_resolve_simple(c,0,1,identity_ref{1},ids,G,cache,x));
    const token_kind qualified[]={token_kind::identifier,token_kind::scope,token_kind::identifier};
    const string_id qualified_names[]={{7},{},{7}};
    const std::uint64_t three[]={0,0,0};
    const v316::source_view q{qualified,qualified_names,three,&strings,&strings};
    assert(!v326_type::try_resolve_simple(q,0,3,identity_ref{1},ids,G,cache,x));
    // Fast intrinsic permits only single builtin (+ CV), not signed/unsigned variants.
    const token_kind builtin[]={token_kind::kw_const,token_kind::kw_int};
    const string_id bi[]={{},{}};
    const std::uint64_t two[]={0,0};
    const v316::source_view b{builtin,bi,two,&strings,&strings};
    v316::type_spec y;
    assert(v326_type::try_resolve_single_intrinsic(b,0,2,y));
    assert(y.base_kind==type_ref_kind::intrinsic &&
           y.base_payload==static_cast<std::uint32_t>(intrinsic_type::signed_int));
    assert(y.length==1 && y.layers[0].kind==derived_type_kind::const_qualified);
    const token_kind complex[]={token_kind::kw_unsigned,token_kind::kw_long};
    const v316::source_view cb{complex,bi,two,&strings,&strings};
    assert(!v326_type::try_resolve_single_intrinsic(cb,0,2,y));
    // Epoch overflow invalidates previous per-record cache entries.
    cache.test_epoch(std::numeric_limits<std::uint32_t>::max());
    cache.begin_record();
    type_ref_kind k=type_ref_kind::invalid;
    std::uint32_t payload=0;
    assert(!cache.get(7,k,payload));
    std::cout<<"V326_TYPE_FAST_PATH_PASS\n";
}
