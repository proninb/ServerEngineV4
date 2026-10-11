// Byte-poison test verifies unused slots are not initialized by default construction.
// It never reads an uninitialized *typed* layer. Only byte representations are inspected.
#include "mock.hpp"
#include "v3_16_canonical.hpp"
#include "v3_42_canonical.hpp"
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <new>
#include <type_traits>
#include <cstdlib>
using namespace cw::server;
static void demand(bool ok,const char* what) {if(!ok) {std::cerr<<"V343_LIFETIME_FAIL "<<what<<'\n';std::exit(1);}}
int main(){
    static_assert(std::is_trivially_default_constructible_v<v342_canonical::layer>);
    static_assert(!std::is_trivially_default_constructible_v<v316::layer>);
    static_assert(sizeof(v316::type_spec)==sizeof(v342_canonical::type_spec));
    static_assert(offsetof(v316::type_spec,layers)==offsetof(v342_canonical::type_spec,layers));
    for(unsigned count: {0u,1u,2u,31u,32u,33u}) {
        alignas(v342_canonical::type_spec) unsigned char storage[sizeof(v342_canonical::type_spec)];
        std::memset(storage,0xa5,sizeof storage);
        auto* s=::new (static_cast<void*>(storage)) v342_canonical::type_spec;
        demand(s->length==0,"default length");
        demand(s->base_kind==type_ref_kind::invalid,"default base kind");
        demand(s->failure==v342_canonical::reason::none,"default failure");
        auto* bytes=reinterpret_cast<const unsigned char*>(&s->layers);
        for(std::size_t j=0;j<sizeof(s->layers);++j) demand(bytes[j]==0xa5,"unused slot overwritten during construction");
        v316::type_spec baseline;
        for(unsigned i=0;i<count;++i) {
            const auto kind=i%2?derived_type_kind::const_qualified:derived_type_kind::pointer;
            const std::uint64_t payload=0x12345678ull+i;
            demand(baseline.push(kind,payload)==s->push(kind,payload),"push verdict");
            demand(baseline.length==s->length,"push length");
            demand(static_cast<unsigned>(baseline.failure)==static_cast<unsigned>(s->failure),"push failure");
        }
        for(unsigned i=0;i<s->length;++i) {
            demand(s->layers[i].kind==baseline.layers[i].kind,"active kind");
            demand(s->layers[i].payload==baseline.layers[i].payload,"active payload");
        }
        if(count==33) demand(s->length==32 && s->failure==v342_canonical::reason::too_deep,"33rd overflow");
        // Do not inspect padding of active layers: padding values are not part of the contract.
        for(std::size_t j=s->length*sizeof(v342_canonical::layer);j<sizeof(s->layers);++j)
            demand(bytes[j]==0xa5,"unused tail overwritten");
        s->~type_spec();
    }
    std::cout<<"V343_LIFETIME_PASS cases=0,1,2,31,32,33\n";
}
