#pragma once
#include "../header-chunk-01/boundaries.hpp"
#include "extractor.hpp"
namespace hc02 {
using namespace sb01;
struct task {id root,context;std::size_t first,last;bool fallback=false;};
struct timings {double discovery=0,planning=0,parse=0,assembly=0,critical=0,construction=0;std::size_t tasks=0,fallbacks=0,plan_bytes=0;};
struct symbol_event {event at;id task,index,context;};
struct declaration_event {event at;id task,index,context;};
inline batch assemble(const std::vector<stream>& input,const hc01::plan& plan,const std::vector<task>& tasks,std::vector<local_output>& locals){
    batch out;out.flags=options(variant::combined);
    std::vector<event> stops(input.size(),UINT64_MAX);
    for(std::size_t t=0;t<tasks.size();++t)stops[tasks[t].root]=(std::min)(stops[tasks[t].root],locals[t].failure.at);
    std::vector<symbol_event> symbols;std::vector<declaration_event> declarations;
    std::vector<id> contexts(plan.contexts.size(),none);
    std::vector<std::vector<id>> remap(tasks.size()),definitions(tasks.size());
    std::size_t request_count=0,nonempty=0;
    for(id c=0;c<plan.contexts.size();++c){const auto& ctx=plan.contexts[c];if(ctx.at>stops[ctx.root])continue;
        symbols.push_back({ctx.at,none,none,c});declarations.push_back({ctx.at,none,none,c});}
    for(id t=0;t<tasks.size();++t){const auto& task=tasks[t];auto& local=locals[t];if(position(task.root,task.first)>stops[task.root])continue;
        remap[t].resize(local.symbols.size());definitions[t].resize(local.declarations.size());
        for(id i=1;i<local.symbols.size();++i)symbols.push_back({local.symbols[i].event,t,i,none});
        for(id i=0;i<local.declarations.size();++i)declarations.push_back({local.declarations[i].begin,t,i,none});
        earliest(out.first,local.failure);out.excluded_callables+=local.excluded_callables;out.excluded_initializers+=local.excluded_initializers;
        request_count+=local.requests.size();nonempty+=!local.requests.empty();
    }
    std::sort(symbols.begin(),symbols.end(),[](auto a,auto b){return a.at<b.at;});
    std::sort(declarations.begin(),declarations.end(),[](auto a,auto b){return a.at<b.at;});
    ca01::require(symbols.size()+input.size()<none&&declarations.size()<none,"assembly index limit");
    out.identities.symbols.reserve(symbols.size()+input.size());out.declarations.reserve(declarations.size());
    std::vector<id> roots(input.size());std::size_t cursor=0;
    for(id r=0;r<input.size();++r){roots[r]=static_cast<id>(out.identities.symbols.size());out.identities.symbols.push_back({none,r,0,{},0});
        while(cursor<symbols.size()&&static_cast<id>((symbols[cursor].at>>32)-1)==r){const auto e=symbols[cursor++];const auto index=static_cast<id>(out.identities.symbols.size());
            if(e.task==none){const auto& c=plan.contexts[e.context];const auto& name=input[r].tokens[c.name_token];
                out.identities.symbols.push_back({c.parent==none?roots[r]:contexts[c.parent],r,1,name.name,c.at});contexts[e.context]=index;
            }else{auto s=locals[e.task].symbols[e.index];const auto& task=tasks[e.task];
                if(s.parent==0)s.parent=task.context==none?roots[r]:contexts[task.context];else s.parent=remap[e.task][s.parent];
                out.identities.symbols.push_back(s);remap[e.task][e.index]=index;}
        }
    }
    for(id t=0;t<tasks.size();++t)if(!remap[t].empty())remap[t][0]=tasks[t].context==none?roots[tasks[t].root]:contexts[tasks[t].context];
    for(const auto e:declarations){const auto index=static_cast<id>(out.declarations.size());
        if(e.task==none){const auto& c=plan.contexts[e.context];out.declarations.push_back({contexts[e.context],declaration_kind::name_space,K::invalid,I::none,c.at,c.at,input[c.root].tokens[c.name_token].source});}
        else{auto d=locals[e.task].declarations[e.index];d.symbol=remap[e.task][d.symbol];out.declarations.push_back(d);definitions[e.task][e.index]=index;}}
    out.requests.prepare(true,request_count,nonempty);
    for(id t=0;t<tasks.size();++t){if(remap[t].empty())continue;auto& local=locals[t];
        for(auto& r:local.requests){r.scope=remap[t][r.scope];r.owner=remap[t][r.owner];r.definition=definitions[t][r.definition];}
        if(tasks[t].fallback&&constructor_coverage_enabled&&(!nested_coverage_enabled||(local.failure.kind!=error_kind::none&&local.failure.kind!=error_kind::coverage)))for(id i=0;i<local.declarations.size();++i)if(local.declarations[i].kind==declaration_kind::record)local.operations.push_back({i,operation_kind::gap,{},{},local.declarations[i].begin,local.declarations[i].source,gap_kind::structural_fallback});
        out.paths.push_back(local.paths);
        for(auto op:local.operations){op.definition=definitions[t][op.definition];out.operations.push_back(op);}
        out.requests.retain(local.shapes);out.requests.append(local.requests);
    }
    out.requests.freeze();out.identities.prepare();return out;
}
}
