#include "mock.hpp"
#include "v3_16_canonical.hpp"
#include "v3_42_canonical.hpp"
#include <type_traits>
#include <iostream>
#include <cstdlib>
#include <cstdint>
using namespace cw::server;
static void demand(bool condition,const char* message) {
    if(!condition){std::cerr<<"V342 TYPE_SPEC FAILED: "<<message<<'\n';std::exit(1);}
}
int main(){
    static_assert(std::is_trivially_default_constructible_v<v342_canonical::layer>);
    static_assert(!std::is_trivially_default_constructible_v<v316::layer>);
    static_assert(sizeof(v316::type_spec)==sizeof(v342_canonical::type_spec));
    static_assert(alignof(v316::type_spec)==alignof(v342_canonical::type_spec));
    for(unsigned count : {0u,1u,2u,31u,32u,33u}){
        v316::type_spec baseline;
        v342_canonical::type_spec candidate;
        demand(baseline.length==0 && candidate.length==0,"length reset");
        demand(baseline.base_kind==candidate.base_kind,"base default");
        demand(static_cast<int>(baseline.failure)==static_cast<int>(candidate.failure),"failure default");
        for(unsigned i=0;i<count;++i){
            auto k=i%2==0?derived_type_kind::pointer:derived_type_kind::const_qualified;
            std::uint64_t payload=i*37u+1u;
            bool a=baseline.push(k,payload), b=candidate.push(k,payload);
            demand(a==b,"push verdict");
            demand(baseline.length==candidate.length,"push length");
            demand(static_cast<int>(baseline.failure)==static_cast<int>(candidate.failure),"push failure");
        }
        for(unsigned i=0;i<baseline.length;++i){
            demand(baseline.layers[i].kind==candidate.layers[i].kind,"layer kind");
            demand(baseline.layers[i].payload==candidate.layers[i].payload,"layer payload");
        }
        demand(count<33 || (candidate.length==32 && candidate.failure==v342_canonical::reason::too_deep),"33rd overflow");
    }
    std::cout<<"V342_TYPE_SPEC_PASS 0/1/2/31/32/33 layers\n";
}
