#include "reference_state.hpp"
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
void check(bool value){if(!value)throw std::runtime_error("Indexed reference state invariant failed");}
int main(){
    using pls09b1::reference_state;std::uint32_t value=99;
    reference_state state;check(!state.find(0,value)&&value==99);
    check(state.enter(0));check(!state.enter(0));state.store(0,0);check(state.find(0,value)&&value==0);
    // Simulate recursive interning/growth, then complete the original frame by ID.
    check(state.enter(1));check(state.enter(131071));state.store(131071,0);state.leave(131071);
    check(!state.enter(1));state.leave(1);state.store(1,131071);check(state.find(1,value)&&value==131071);
    check(!state.find(100000,value));check(state.enter(100000));
    // A caught unresolved traversal leaves marks. A new context invalidates both.
    state.clear();check(!state.find(0,value)&&!state.find(1,value)&&!state.find(131071,value));
    check(state.enter(0)&&state.enter(1)&&state.enter(100000));
    reference_state wrap(UINT32_MAX);wrap.store(0,7);check(wrap.enter(9));wrap.clear();
    check(!wrap.find(0,value));check(wrap.enter(9));wrap.store(0,8);check(wrap.find(0,value)&&value==8);
    // Compare sparse and repeated operations to the original two containers.
    reference_state actual;std::unordered_map<std::uint32_t,std::uint32_t> cache;std::unordered_set<std::uint32_t> visiting;
    std::uint32_t random=33;
    for(unsigned i=0;i<50000;++i){random=random*1664525u+1013904223u;const auto cell=(random>>8)%4097;
        switch(random%5){
        case 0:{const auto it=cache.find(cell);const bool found=actual.find(cell,value);check(found==(it!=cache.end()));if(found)check(value==it->second);break;}
        case 1:check(actual.enter(cell)==visiting.insert(cell).second);break;
        case 2:actual.leave(cell);visiting.erase(cell);break;
        case 3:actual.store(cell,random);cache[cell]=random;break;
        case 4:actual.clear();cache.clear();visiting.clear();break;
        }
    }
    std::cout<<"indexed reference state: growth, sparse IDs, contexts, cycles and rollover passed\n";
}
