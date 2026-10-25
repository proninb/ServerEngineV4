#include "project/parser/header_record_index.hpp"
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
struct allocations {static inline std::size_t calls=0,bytes=0;};
template<class T> struct allocator {
    using value_type=T;
    allocator()=default;
    template<class U> allocator(const allocator<U>&) noexcept {}
    T* allocate(std::size_t n){++allocations::calls;allocations::bytes+=n*sizeof(T);return std::allocator<T>{}.allocate(n);}
    void deallocate(T* p,std::size_t n) noexcept {std::allocator<T>{}.deallocate(p,n);}
    template<class U> bool operator==(const allocator<U>&) const noexcept{return true;}
};
using map=std::unordered_map<int,int,std::hash<int>,std::equal_to<int>,allocator<std::pair<const int,int>>>;
using multi=std::unordered_multimap<int,int,std::hash<int>,std::equal_to<int>,allocator<std::pair<const int,int>>>;
template<class T> using index=cw::server::detail::header_record_index<T>;
void require(bool value){if(!value)throw std::runtime_error("header record index contract failed");}
}
int main(){try{
    cw::server::detail::header_record_state_eager=false;
    allocations::calls=allocations::bytes=0;
    {
        index<map> pending,generations,nested,lookup;index<multi> waiting;
        require(pending.empty()&&pending.size()==0&&pending.find(4)==pending.end());
        require(pending.begin()==pending.end());
        const auto& read=pending;require(read.find(4)==read.end()&&read.begin()==read.end());
        const auto range=waiting.equal_range(1);require(range.first==range.second);
        require(allocations::calls==0&&!pending.materialized()&&!waiting.materialized());
    }
    require(allocations::calls==0);
    cw::server::detail::header_record_state_eager=true;
    {
        index<map> pending,generations,nested,lookup;index<multi> waiting;
        require(pending.materialized()&&waiting.materialized());
    }
    std::cout<<"five_eager_indices_calls="<<allocations::calls<<",bytes="<<allocations::bytes<<",lazy_empty_calls=0\n";
#ifdef _MSC_VER
    require(allocations::calls>0);
#endif
    for(bool eager:{false,true}){
        cw::server::detail::header_record_state_eager=eager;
        index<map> value;auto [found,inserted]=value.try_emplace(7,11);require(inserted&&found->second==11);
        value.insert_or_assign(7,12);require(value[7]==12&&value.size()==1);
        value[8]=13;value.erase(value.find(7));require(value.find(7)==value.end()&&value.find(8)->second==13);
        value.reserve(512);for(int i=0;i<256;++i)value.insert_or_assign(i,i*2);
        std::size_t count=0;for(const auto& entry:value){require(entry.second==entry.first*2);++count;}require(count==256);
        const auto& read=value;require(read.find(255)->second==510);
        index<multi> waiting;waiting.emplace(3,1);waiting.emplace(3,2);waiting.emplace(4,9);
        const auto range=waiting.equal_range(3);int sum=0;
        for(auto it=range.first;it!=range.second;) {sum+=it->second;it=waiting.erase(it);}
        require(sum==3&&waiting.size()==1&&waiting.find(4)->second==9);
    }
    std::cout<<"header record index tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
