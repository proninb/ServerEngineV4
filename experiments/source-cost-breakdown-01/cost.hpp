#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace scost01 {
enum bucket {source, cursor, binding, object_lookup, member_transition,
    endpoint_syntax, path_intern, graph_write, provenance, declaration_syntax,
    scalar_syntax, string_syntax, assignment_syntax, count};
inline constexpr const char* names[]={"source","cursor","binding","object_lookup",
    "member_transition","endpoint_syntax","path_intern","graph_write","provenance",
    "declaration_syntax","scalar_syntax","string_syntax","assignment_syntax"};
using clock=std::chrono::steady_clock;
struct metric {std::uint64_t calls=0,inclusive=0,exclusive=0;};
inline thread_local std::array<metric,count> metrics{};
inline thread_local unsigned mode=0;
struct scope;
inline thread_local scope* parent=nullptr;
struct scope {
    bucket kind;unsigned enabled;scope* outer=nullptr;
    clock::time_point start{};std::uint64_t children=0;
    explicit scope(bucket k)noexcept:kind(k),enabled(mode){
        if(!enabled)return;
        ++metrics[k].calls;
        if(enabled==2){outer=parent;parent=this;start=clock::now();}
    }
    ~scope()noexcept{
        if(enabled!=2)return;
        const auto elapsed=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-start).count());
        auto& m=metrics[kind];m.inclusive+=elapsed;m.exclusive+=elapsed-children;
        parent=outer;if(outer)outer->children+=elapsed;
    }
};
template<class F>decltype(auto) call(bucket kind,F&& fn){scope probe{kind};return fn();}
struct session {
    session()noexcept{
        const auto* setting=std::getenv("CW_SOURCE_COST_MODE");
        mode=setting&&std::strcmp(setting,"timed")==0?2:
             setting&&std::strcmp(setting,"counts")==0?1:0;
        metrics={};parent=nullptr;
    }
    ~session()noexcept{
        if(mode)for(unsigned i=0;i<count;++i)
            std::fprintf(stderr,"SOURCE_COST,%s,%llu,%llu,%llu\n",names[i],
                static_cast<unsigned long long>(metrics[i].calls),
                static_cast<unsigned long long>(metrics[i].inclusive),
                static_cast<unsigned long long>(metrics[i].exclusive));
        mode=0;
    }
};
}
