#include "binding_reduction.hpp"
#include <iostream>
#include <stdexcept>
void check(bool value){if(!value)throw std::runtime_error("Final binding reduction invariant failed");}
int main(){
    using pls10::absent;pls10::binding_reduction state;
    const auto later=[](auto a,auto b){return a>b;};
    check(state.find(0)==absent);state.select_direct(0,0,later);state.select_direct(0,2,later);state.select_direct(0,1,later);
    state.select_direct(9,3,later);check(state.find(0)==2&&state.find(9)==3);
    state.begin_round();state.select_next(0,4,later);state.select_next(100000,5,later);
    // All targets resolve against the previous snapshot until commit.
    check(state.find(0)==2&&state.find(100000)==absent);check(!state.commit_round());
    check(state.find(0)==4&&state.find(100000)==5);
    state.begin_round();state.select_next(0,4,later);state.select_next(100000,5,later);check(state.commit_round());
    // A target moving away must restore its direct winner, or disappear.
    state.begin_round();state.select_next(99,6,later);check(!state.commit_round());
    check(state.find(0)==2&&state.find(100000)==absent&&state.find(99)==6);
    const auto keys=state.keys();check((keys==std::vector<std::uint32_t>{0,9,99}));
    state.begin_round();state.select_next(0,1,later);check(!state.commit_round());
    check(state.find(0)==2&&state.find(99)==absent);check((state.keys()==std::vector<std::uint32_t>{0,9}));
    std::cout<<"final bindings: order, snapshot isolation, sparse growth, restoration and stable contexts passed\n";
}
