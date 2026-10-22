#include "kernel.hpp"
#include <iostream>
#include <random>

namespace {
ca01::input permute(ca01::input in,unsigned seed){
    using ca01::id;std::vector<id> order(in.symbols.size()),inverse(order.size());
    std::iota(order.begin(),order.end(),0);std::mt19937 rng(seed);std::shuffle(order.begin(),order.end(),rng);
    for(id i=0;i<order.size();++i)inverse[order[i]]=i;
    auto original=in.symbols;for(id i=0;i<order.size();++i){in.symbols[i]=original[order[i]];
        if(in.symbols[i].parent!=ca01::none)in.symbols[i].parent=inverse[in.symbols[i].parent];}
    for(auto& q:in.queries)q.parent=inverse[q.parent];in.prepare();return in;
}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}ca01::require(rejected,message);}
}
int main(){try{
    using namespace ca01;
    input entities;entities.collisions=true;
    entities.symbols={{none,0,0,{},0,0,1},{none,1,0,{},0,0,1},
        {0,0,2,"same-long-prefix-0000-X",1,0,0x80000002},
        {1,1,2,"same-long-prefix-0000-Y",2,0,0x80000003},
        {1,1,2,"same-long-prefix-0000-X",3,0,0x80000002},
        {0,0,2,"same-long-prefix-0000-x",4,0,0x80000004},
        {0,0,1,"N",5,0,0x40000005},
        {6,0,2,"same-long-prefix-0000-X",6,0,0x80000006},
        {1,1,1,"N",7,0,0x40000005},
        {8,1,2,"same-long-prefix-0000-X",8,0,0x80000006},
        {0,0,3,"same-long-prefix-0000-X",9,0,0xc0000007}};
    entities.queries={{0,2,0,"same-long-prefix-0000-Y",1},
        {6,2,0x80000002,"same-long-prefix-0000-X",5,true},
        {8,2,0x80000006,"same-long-prefix-0000-X",8,true},
        {0,3,0,"same-long-prefix-0000-X",8},
        {0,3,0xc0000007,"same-long-prefix-0000-X",10}};
    input strings;strings.collisions=true;strings.strings=true;
    strings.symbols={{none,0,0,{},0,0,0},{none,1,0,{},0,0,0},
        {0,0,1,"same-long-prefix-0000-X",1,0,1},
        {1,1,1,"same-long-prefix-0000-Y",2,0,2},
        {1,1,1,"same-long-prefix-0000-X",3,0,1},
        {0,0,1,"same-long-prefix-0000-x",4,0,3},
        {1,1,1,"unused-string",5,0,4}};
    std::size_t checks=0;
    for(auto threads:{1u,2u,4u,8u,16u}){pool workers(threads);
        for(auto fixture:{entities,strings})for(unsigned seed=0;seed<8;++seed){
            auto in=permute(fixture,seed);memory mem(false);auto ref=reference(in,&mem);auto expected=number(in,ref,mem);verify(in,ref,expected);
            for(auto algorithm:{"sort","index","hierarchical"}){std::vector<comparisons> stats(threads);
                auto out=canonicalize(in,algorithm,workers,mem,stats,false);auto ids=number(in,out,mem);verify(in,out,ids);
                require(ids.who==expected.who,"collision partition mismatch");++checks;}
        }
    }
    auto bad=entities;bad.symbols[2].name={};rejects([&]{bad.prepare();},"empty nonroot name accepted");
    bad=entities;bad.symbols.push_back({0,0,2,"import-only",import_event,0,0});bad.prepare();
    memory mem(false);auto imported=reference(bad,&mem);rejects([&]{(void)number(bad,imported,mem);},"import invented declaration");
    bad=entities;bad.symbols[3].event=1;bad.prepare();auto tied=reference(bad,&mem);
    rejects([&]{(void)number(bad,tied,mem);},"ambiguous event order accepted");
    bad=entities;bad.queries[1].expected=0x80000006;bad.prepare();auto premature=reference(bad,&mem);auto numbered=number(bad,premature,mem);
    rejects([&]{verify(bad,premature,numbered);},"future shadow accepted");
    std::cout<<"collision_model=passed,algorithm_checks="<<checks<<",workers=1/2/4/8/16\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
