#include "probe.hpp"
#include <iostream>
int main(){if(!pls14::enabled)return 2;pls14::active=true;pls14::is_string=false;
 for(std::uint64_t i=0;i<1000000;++i){pls14::sample empty(pls14::endpoint);}
 const auto& m=pls14::metrics[pls14::endpoint];std::cout<<"calls="<<m.calls<<",samples="<<m.samples<<",ns="<<m.ns<<",mean_empty_sample_ns="<<(m.samples?double(m.ns)/m.samples:0)<<'\n';}
