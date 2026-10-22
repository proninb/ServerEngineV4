#pragma once
#include <iostream>
#include <string_view>

namespace sl01 {
template<class C, class Insert> void probe_one(const char* name, Insert insert) {
    struct counts {std::uint64_t calls,bytes;};
    auto snapshot=[](){return counts{sb02::total.calls.load(),sb02::total.bytes.load()};};
    counts eager{},eager_write{},lazy{},read{},write{};
    {sb02::phase phase(sb02::parse);const auto start=snapshot();
        {C container;eager=snapshot();eager.calls-=start.calls;eager.bytes-=start.bytes;const auto before=snapshot();insert(container);eager_write=snapshot();eager_write.calls-=before.calls;eager_write.bytes-=before.bytes;}
    }
    {sb02::phase phase(sb02::parse);const auto start=snapshot();
        {lazy_container<C> container;lazy=snapshot();lazy.calls-=start.calls;lazy.bytes-=start.bytes;
            const auto before_read=snapshot();
            ca01::require(container.empty()&&container.begin()==container.end()&&container.find(typename C::key_type{})==container.end(),"empty read differs");
            read=snapshot();read.calls-=before_read.calls;read.bytes-=before_read.bytes;
            const auto before_write=snapshot();insert(container);write=snapshot();write.calls-=before_write.calls;write.bytes-=before_write.bytes;
            ca01::require(container.materialized()&&!container.empty(),"write did not materialize state");
        }
    }
    ca01::require(lazy.calls==0&&read.calls==0,"lazy empty state allocated");
#ifdef SB02_ALLOCATIONS
    constexpr unsigned expected=[](){if constexpr(requires(C c){c.bucket_count();})return 2u;else return 1u;}();
    ca01::require(eager.calls==expected,"unexpected standard-container construction allocation count");
    ca01::require(write.calls==eager.calls+eager_write.calls&&write.bytes==eager.bytes+eager_write.bytes,"first-use allocation behavior changed");
#endif
    std::cout<<"container="<<name<<",control_empty_calls="<<eager.calls<<",control_empty_bytes="<<eager.bytes<<",lazy_empty_calls="<<lazy.calls<<",empty_read_calls="<<read.calls<<",control_write_calls="<<eager_write.calls<<",first_write_calls="<<write.calls<<",validation=passed\n";
}
inline void container_probe(){
#ifndef SB02_ALLOCATIONS
    throw std::runtime_error("Use StateLazy01Allocations for --container-probe");
#endif
    struct waiting_value {id target;std::uint64_t generation;origin source;event assignment;};
    probe_one<std::unordered_map<std::string_view,id>>("fields",[](auto& c){c.emplace("field",1);});
    probe_one<std::unordered_map<std::string_view,const operation*>>("pending",[](auto& c){c["field"]=nullptr;});
    probe_one<std::unordered_map<id,value_cell>>("values",[](auto& c){c[1]={};});
    probe_one<std::unordered_map<id,std::uint64_t>>("generations",[](auto& c){c.try_emplace(1,0);});
    probe_one<std::unordered_multimap<std::string_view,waiting_value>>("waiting",[](auto& c){c.emplace("field",waiting_value{});});
    probe_one<std::unordered_map<std::string_view,std::vector<const operation*>>>("pending_nested",[](auto& c){c["field"].push_back(nullptr);});
    probe_one<std::map<std::string,value_cell>>("completed_nested",[](auto& c){c["field"]={};});
    ca01::require(sb02::total.live.load()==0,"probe retained allocations");
}
inline void container_tests(){
    lazy_container<std::unordered_map<unsigned,unsigned>> map;
    ca01::require(map.empty()&&map.size()==0&&!map.materialized(),"empty state differs");
    auto missing=map.find(9);ca01::require(missing==map.end()&&!map.materialized(),"miss materialized state");
    auto [first,inserted]=map.try_emplace(9,3);ca01::require(inserted&&first->second==3,"insert differs");
    auto [again,duplicate]=map.try_emplace(9,8);ca01::require(!duplicate&&again->second==3,"duplicate differs");
    map.erase(map.find(9));ca01::require(map.empty()&&map.materialized(),"erase must retain constructed state");
    const auto& view=map;ca01::require(view.find(9)==view.end(),"const lookup differs");
    map[2]=7;ca01::require(map.begin()->second==7,"write after erase differs");
    lazy_container<std::unordered_multimap<unsigned,unsigned>> multi;
    auto absent=multi.equal_range(1);ca01::require(absent.first==absent.second&&!multi.materialized(),"empty range differs");
    multi.emplace(1,2);multi.emplace(1,3);auto range=multi.equal_range(1);unsigned count=0;
    for(auto it=range.first;it!=range.second;){++count;it=multi.erase(it);}ca01::require(count==2&&multi.empty(),"multimap erase/range differs");
}
}
