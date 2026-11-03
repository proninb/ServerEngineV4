// Standalone clock-floor diagnostic. No PUBLISH, fixture or Graph mutation.
#include "probe.hpp"
#include <iostream>
int main(){
 if(!pls11::enabled)return 2;
 pls11::in_string=true;
 for(std::uint64_t i=0;i<1000000;++i){pls11::sample empty(pls11::path);}
 const auto& m=pls11::metrics[pls11::path];
 std::cout<<"calls="<<m.calls<<",samples="<<m.samples<<",ns="<<m.ns
          <<",mean_empty_sample_ns="<<(m.samples?double(m.ns)/m.samples:0)<<'\n';
}
