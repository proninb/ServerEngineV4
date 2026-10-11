// Compile: -include tests/mock_project/mock.hpp -I tests/mock_project -I .
#include "benchmarks/v3_31_scope_chain.hpp"
#include <cassert>
#include <iostream>
using namespace cw::server;
int main() {
    string_table strings;
    identity_space ids(strings);
    graph G;
    v326_type::base_type_cache cache;
    v331_type::scope_chain scopes;
    const token_kind kinds[]{token_kind::kw_const,token_kind::identifier,token_kind::kw_volatile};
    const string_id names[]{{},{7},{}};
    const std::uint64_t nums[]{0,0,0};
    const v316::source_view src{kinds,names,nums,&strings,&strings};
    cache.begin_record();
    scopes.begin_record(ids,identity_ref{1});
    v331_type::named_profile p;
    v316::type_spec a;
    assert(v331_type::try_resolve_simple(src,0,3,identity_ref{1},ids,G,cache,scopes,a,nullptr,nullptr,&p));
    assert(p.cache_misses==1 && p.cache_hits==0 && p.scope_steps==1);
    assert(a.length==2);
    assert(a.layers[0].kind==derived_type_kind::const_qualified);
    assert(a.layers[1].kind==derived_type_kind::volatile_qualified);
    v316::type_spec b;
    assert(v331_type::try_resolve_simple(src,0,3,identity_ref{1},ids,G,cache,scopes,b,nullptr,nullptr,&p));
    assert(p.cache_misses==1 && p.cache_hits==1);
    assert(b.base_kind==a.base_kind && b.base_payload==a.base_payload);
    // Generation invalidation between records.
    cache.begin_record();
    scopes.begin_record(ids,identity_ref{1});
    v316::type_spec c;
    assert(v331_type::try_resolve_simple(src,0,3,identity_ref{1},ids,G,cache,scopes,c,nullptr,nullptr,&p));
    assert(p.cache_misses==2 && p.cache_hits==1);
    // New owner: stale chain must never be reused; fallback retains lookup semantics.
    cache.begin_record();
    v316::type_spec d;
    assert(v331_type::try_resolve_simple(src,0,3,identity_ref{99},ids,G,cache,scopes,d,nullptr,nullptr,&p));
    assert(p.cache_misses==3 && p.cache_hits==1);
    const token_kind qualified[]={token_kind::identifier,token_kind::scope,token_kind::identifier};
    const string_id qnames[]{{7},{},{7}};
    const v316::source_view q{qualified,qnames,nums,&strings,&strings};
    v316::type_spec e;
    assert(!v331_type::try_resolve_simple(q,0,3,identity_ref{1},ids,G,cache,scopes,e));
#ifdef TEST_ALIAS
    assert(a.base_kind==type_ref_kind::intrinsic);
    assert(p.aliases==3);
#else
    assert(a.base_kind==type_ref_kind::named && a.base_payload==7);
    assert(p.ordinary_named==3);
#endif
    std::cout<<"V331_SCOPE_CHAIN_PASS\n";
}
