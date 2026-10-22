#pragma once
#include <array>
#include <chrono>
#include <vector>
namespace ea01 {
using clock=std::chrono::steady_clock;
enum category {other,actions,catalog,index_build,state_construction,state_cleanup,record_setup,lookup,nested,compatibility,pending,output,merge,finalize,count};
inline constexpr const char* names[]={"other","action_preparation","catalog_preparation","member_index_build","state_construction","state_cleanup","record_setup","member_lookup","nested_traversal","type_compatibility","pending_generations","sparse_output","definition_merge","finalize"};
struct record_cost {unsigned definition=0;double ms=0;std::size_t events=0;};
struct audit {
    unsigned mode=0; // 0 off, 1 coarse, 2 per-record, 3 detailed, 4 sampled records
    unsigned sample_seed=0;bool selected=false;
    std::array<double,count> ms{};
    std::array<std::uint64_t,count> calls{},bytes{},hits{};
    std::vector<record_cost> records;
    std::vector<unsigned> schema_builds,schema_accesses;
    std::vector<std::pair<unsigned,unsigned>> schema_users;
    category current=other;clock::time_point last;
    std::uint64_t prior_calls=0,prior_bytes=0;
    void account(){const auto now=clock::now();ms[current]+=std::chrono::duration<double,std::milli>(now-last).count();last=now;
#ifdef SB02_ALLOCATIONS
        const auto c=sb02::total.calls.load(),b=sb02::total.bytes.load();calls[current]+=c-prior_calls;bytes[current]+=b-prior_bytes;prior_calls=c;prior_bytes=b;
#endif
    }
    void start(){last=clock::now();
#ifdef SB02_ALLOCATIONS
        prior_calls=sb02::total.calls.load();prior_bytes=sb02::total.bytes.load();
#endif
    }
};
inline thread_local audit* active=nullptr;
struct scope {
    audit* a=nullptr;category previous=other;
    scope()=default;
    void start(category c){a=active&&active->mode&&(active->mode==3||(active->mode==4&&active->selected))?active:nullptr;if(a){previous=a->current;a->account();a->current=c;++a->hits[c];}}
    void stop(){if(a){a->account();a->current=previous;a=nullptr;}}
    scope(category c,bool detailed=true):a(active&&active->mode&&(!detailed||(active->mode==3||(active->mode==4&&active->selected)))?active:nullptr),previous(a?a->current:other){if(a){a->account();a->current=c;++a->hits[c];}}
    ~scope(){stop();}
};
template<class F>decltype(auto) measure(category c,F&& f){scope s(c);return f();}
struct record_scope {
    audit* a;unsigned id;std::size_t events;clock::time_point start;
    record_scope(unsigned i,std::size_t n):a(active&&active->mode>=2?active:nullptr),id(i),events(n){if(a){a->selected=a->mode==3||(ca01::mix(std::uint64_t(i)^a->sample_seed)%32==0);start=clock::now();}}
    ~record_scope(){if(a)a->records.push_back({id,std::chrono::duration<double,std::milli>(clock::now()-start).count(),events});if(a)a->selected=false;}
};
}
#define EA_EXPR(cat,expr) ea01::measure(ea01::cat,[&]()->decltype(auto){return (expr);})
