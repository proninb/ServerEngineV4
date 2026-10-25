#include "cost.hpp"
#include <cassert>
#include <stdexcept>
int main(){
    using namespace scost01;
    for(unsigned selected:{0u,1u,2u}){
        mode=selected;metrics={};parent=nullptr;
        {scope root{source};{scope a{cursor};{scope b{binding};}{scope c{cursor};}}}
        assert(parent==nullptr);
        assert(metrics[source].calls==(selected?1u:0u));
        assert(metrics[cursor].calls==(selected?2u:0u));
        assert(metrics[binding].calls==(selected?1u:0u));
        std::uint64_t sum=0;for(const auto& m:metrics)sum+=m.exclusive;
        assert(sum==metrics[source].inclusive);
        if(selected!=2)assert(sum==0);
    }
    mode=2;metrics={};int value=7;
    auto& result=call(binding,[&]()->int&{return value;});assert(&result==&value);
    try{call(cursor,[]()->int{throw std::runtime_error("expected");});assert(false);}catch(const std::runtime_error&){}
    assert(parent==nullptr&&metrics[binding].calls==1&&metrics[cursor].calls==1);
    mode=0;
}
