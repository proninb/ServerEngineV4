#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
#include <utility>
#include <iostream>
struct declaration {std::uint32_t owner;};
struct desc {std::uint32_t declaration;};
static std::vector<std::pair<unsigned,unsigned>> selected(
    std::vector<declaration> const& d,std::vector<desc> const& a,
    unsigned first,unsigned count,unsigned owner){
    auto it=std::lower_bound(a.begin(),a.end(),first,
        [](desc const& x,unsigned id){return x.declaration<id;});
    std::vector<std::pair<unsigned,unsigned>> r;
    for(std::size_t di=first;di<first+count;++di){
        if(d[di].owner!=owner)continue;
        while(it!=a.end()&&it->declaration<di)++it;
        r.emplace_back((unsigned)di,it!=a.end()&&it->declaration==di?1u:0u);
    }
    return r;
}
int main(){
    // Parent 0 has field, access, nested child 1, then field and ctor.
    std::vector<declaration> d{{0},{0},{1},{1},{0},{0},{2},{0}};
    std::vector<desc> a{{0},{2},{3},{4},{6}};
    assert((selected(d,a,0,8,0)==std::vector<std::pair<unsigned,unsigned>>{{0,1},{1,0},{4,1},{5,0},{7,0}}));
    assert((selected(d,a,2,2,1)==std::vector<std::pair<unsigned,unsigned>>{{2,1},{3,1}}));
    assert((selected(d,a,6,1,2)==std::vector<std::pair<unsigned,unsigned>>{{6,1}}));
    // no descriptor and no nested declarations
    assert((selected(d,a,4,2,0)==std::vector<std::pair<unsigned,unsigned>>{{4,1},{5,0}}));
    std::cout<<"V337_NESTED_OWNER_ORDER_PASS\n";
}
