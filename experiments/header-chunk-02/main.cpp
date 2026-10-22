#include "../semantic-bind-02/allocation_audit.hpp"
#define SB03_MODEL_HEADER "../semantic-bind-04/model.hpp"
#define main unused_sb01_cli
#include "../semantic-bind-01/main.cpp"
#undef main
#include "pipeline.hpp"
namespace {
measured perform(const std::vector<sb01::stream>& input,ca01::pool& pool,bool reverse){
    measured out;const auto start=clock_type::now();std::vector<local_output> locals(input.size());
    auto t=clock_type::now();
    pool.run(input.size(),[&](std::size_t task,auto){const auto i=reverse?input.size()-1-task:task;locals[i]=sb01::parser(input[i],static_cast<id>(i)).run();});
    out.parse_ms=elapsed_ms(t);t=clock_type::now();out.data=combine(locals);out.combine_ms=elapsed_ms(t);
    // Include destruction of worker output copies in the end-to-end model time.
    locals.clear();
    ca01::memory memory(false);std::vector<ca01::comparisons> counters(pool.size);
    t=clock_type::now();auto canonical=ca01::canonicalize(out.data.identities,"index",pool,memory,counters,false);out.canonical_ms=elapsed_ms(t);
    t=clock_type::now();out.binding=bind(out.data,canonical,pool);out.binding_ms=elapsed_ms(t);
    t=clock_type::now();auto numbered=ca01::number(out.data.identities,canonical,memory);out.number_ms=elapsed_ms(t);
    out.representatives.assign(canonical.representative.begin(),canonical.representative.end());
    out.who.assign(numbered.who.begin(),numbered.who.end());
    out.total_ms=elapsed_ms(start);return out;
}

std::uint64_t consume(const bound_storage& types,const std::vector<std::size_t>& order,bool shuffled){
    std::uint64_t hash=14695981039346656037ull;
    for(std::size_t i=0;i<types.size();++i){const auto t=types[shuffled?order[i]:i];
        hash=(hash^t.entity)*1099511628211ull;hash=(hash^static_cast<unsigned>(t.builtin))*1099511628211ull;
        hash=(hash^t.layers.size())*1099511628211ull;
        for(const auto& layer:t.layers){hash=(hash^static_cast<unsigned>(layer.kind))*1099511628211ull;hash=(hash^layer.payload)*1099511628211ull;}
    }return hash;
}

measured chunked(const std::vector<sb01::stream>& input,ca01::pool& pool,std::size_t target,bool reverse,hc02::timings& times){
    measured out;const auto start=clock_type::now();auto stage=clock_type::now();auto plan=hc01::discover(input);times.discovery=elapsed_ms(stage);
    stage=clock_type::now();auto chunks=hc01::prepare(plan,target);std::vector<hc02::task> tasks;tasks.reserve(chunks.size()+plan.fallback_roots.size());
    for(const auto& c:chunks)tasks.push_back({c.root,c.context,c.first,c.last,false});
    for(auto root:plan.fallback_roots)tasks.push_back({root,none,0,input[root].tokens.size(),true});
    std::sort(tasks.begin(),tasks.end(),[](auto a,auto b){return std::tie(a.root,a.first)<std::tie(b.root,b.first);});
    times.planning=elapsed_ms(stage);times.tasks=tasks.size();times.fallbacks=plan.fallback_roots.size();times.plan_bytes=plan.bytes()+chunks.capacity()*sizeof(hc01::chunk)+tasks.capacity()*sizeof(hc02::task);
    std::vector<local_output> locals(tasks.size());std::vector<double> durations(tasks.size());stage=clock_type::now();
    pool.run(tasks.size(),[&](std::size_t index,auto){const auto i=reverse?tasks.size()-1-index:index;const auto& task=tasks[i];const auto begin=clock_type::now();
        if(task.fallback)locals[i]=sb01::parser(input[task.root],task.root).run();
        else locals[i]=hc02::extractor(input[task.root],task.root,task.first,task.last).run();
        durations[i]=elapsed_ms(begin);
    });times.parse=elapsed_ms(stage);out.parse_ms=times.parse;
    for(auto ms:durations)times.critical=(std::max)(times.critical,ms);
    stage=clock_type::now();out.data=hc02::assemble(input,plan,tasks,locals);locals.clear();times.assembly=elapsed_ms(stage);out.combine_ms=times.assembly;
    ca01::memory memory(false);std::vector<ca01::comparisons> counters(pool.size);
    stage=clock_type::now();auto canonical=ca01::canonicalize(out.data.identities,"index",pool,memory,counters,false);out.canonical_ms=elapsed_ms(stage);
    stage=clock_type::now();out.binding=bind(out.data,canonical,pool);out.binding_ms=elapsed_ms(stage);
    stage=clock_type::now();auto numbered=ca01::number(out.data.identities,canonical,memory);out.number_ms=elapsed_ms(stage);
    out.representatives.assign(canonical.representative.begin(),canonical.representative.end());out.who.assign(numbered.who.begin(),numbered.who.end());out.total_ms=elapsed_ms(start);return out;
}

void exact(const measured& a,const measured& b){
    same_result(a,b);check(a.data.excluded_callables==b.data.excluded_callables&&a.data.excluded_initializers==b.data.excluded_initializers,"exclusion parity");
    const auto& as=a.data.identities.symbols;const auto& bs=b.data.identities.symbols;check(as.size()==bs.size(),"symbol count");
    for(std::size_t i=0;i<as.size();++i)check(std::tie(as[i].parent,as[i].worker,as[i].kind,as[i].name,as[i].event)==std::tie(bs[i].parent,bs[i].worker,bs[i].kind,bs[i].name,bs[i].event),"symbol/event parity");
    check(a.data.declarations.size()==b.data.declarations.size(),"declaration count");
    for(std::size_t i=0;i<a.data.declarations.size();++i){const auto& x=a.data.declarations[i];const auto& y=b.data.declarations[i];
        check(std::tie(x.symbol,x.kind,x.record_kind,x.alias,x.begin,x.complete,x.open)==std::tie(y.symbol,y.kind,y.record_kind,y.alias,y.begin,y.complete,y.open)&&x.source==y.source&&x.complete_source==y.complete_source&&x.open_source==y.open_source,"declaration/origin/event parity");}
    check(a.data.requests.size()==b.data.requests.size(),"request count");
    for(std::size_t i=0;i<a.data.requests.size();++i){const auto& x=a.data.requests[i];const auto& y=b.data.requests[i];
        check(std::tie(x.scope,x.owner,x.definition,x.member,x.at,x.base,x.type.name,x.type.builtin)==std::tie(y.scope,y.owner,y.definition,y.member,y.at,y.base,y.type.name,y.type.builtin)&&x.source==y.source&&x.incomplete_source==y.incomplete_source&&std::equal(x.type.layers.begin(),x.type.layers.end(),y.type.layers.begin(),y.type.layers.end()),"request/origin/event parity");
        const auto& dx=a.binding.types.destination(i);const auto& dy=b.binding.types.destination(i);check(dx.owner==dy.owner&&dx.definition==dy.definition&&dx.member==dy.member&&dx.base==dy.base,"destination parity");}
}
std::size_t retained(const measured& m){return m.binding.types.record_bytes()+m.binding.types.shape_bytes()+m.binding.failures.capacity()*sizeof(issue);}
void tests(){
    selected_variant=variant::combined;
    const std::vector<std::vector<std::string>> cases={
        {"struct A; struct B { A* p; }; struct A { int value; };"},
        {"struct A; struct B { A p; }; struct A {};"},
        {"typedef int T; struct A { T value; };"},
        {"struct A { T value; }; typedef int T;"},
        {"namespace N { struct A; } namespace N { struct A {}; struct B { A x; }; }"},
        {"namespace N { namespace M { typedef int T; struct A { T (*p)[2][3]; }; } }"},
        {"namespace Empty {} namespace N { struct A {}; } namespace N { struct B { A x; }; }"},
        {"struct A {}; namespace N { struct A; struct B { A x; }; }"},
        {"struct A { int x; };", "struct A { bool x; };"},
        {"struct A; class A {};"},
        {"struct X; struct A { X* x; }; typedef int X;"},
        {"struct A { int x y; }; struct B {}; namespace N { struct C {}; }"},
        {"namespace N { struct A { int x y; }; struct B {}; } namespace N { struct C {}; }"},
        {"struct A { int x[2); }; struct B {};"},
        {"namespace N { struct A {};"},
        {"struct A { A() { int local; } int f() { return 1; } int x; };"}
    };
    std::size_t checks=0;
    for(const auto& texts:cases){std::vector<sb01::stream> input;for(const auto& text:texts)input.push_back(lex(text));
        for(auto workers:{1u,2u,4u,8u,16u}){ca01::pool pool(workers);const auto control=perform(input,pool,false);
            for(auto target:{1u,32u,8192u})for(bool reverse:{false,true}){hc02::timings t;auto m=chunked(input,pool,target,reverse,t);exact(control,m);++checks;}}}
    std::vector<sb01::stream> empty(3);ca01::pool pool(4);hc02::timings t;exact(perform(empty,pool,false),chunked(empty,pool,8192,false,t));
    self_test();std::cout<<"chunk_parity_checks="<<checks<<",validation=passed\n";
}
}

int main(int argc,char** argv){try{
    if(argc==2&&std::string_view(argv[1])=="--self-test"){tests();return 0;}
    if(argc!=5){std::cerr<<"Usage: HeaderChunk02 <project> <workers> <target> <repeats>\n";return 2;}
    selected_variant=variant::combined;const auto workers_count=std::stoul(argv[2]),target=std::stoul(argv[3]),repeats=std::stoul(argv[4]);check(target>0&&repeats>0&&repeats<=30,"invalid configuration");
    world w;input_times prep;const auto input=load(w,fs::path(argv[1]),prep);ca01::pool pool(workers_count);
    bool accepted;std::uint64_t checksum;std::size_t control_bytes,chunk_bytes,control_primary,chunk_primary;
    {auto control=perform(input,pool,false);hc02::timings stats;auto candidate=chunked(input,pool,target,false,stats);exact(control,candidate);hc02::timings reverse;exact(control,chunked(input,pool,target,true,reverse));
        semantic_world o;const auto expected=oracle(w,o);accepted=expected.accepted;if(accepted)verify_positive(candidate,o);else verify_negative(candidate,expected);
        checksum=consume(control.binding.types,{},false);control_bytes=retained(control);chunk_bytes=retained(candidate);control_primary=primary_bytes(control.data,control.binding);chunk_primary=primary_bytes(candidate.data,candidate.binding);}
#ifdef SB02_ALLOCATIONS
    for(bool chunk:{false,true}){sb02::total.peak=0;sb02::total.calls=0;sb02::total.bytes=0;sb02::peak_requests=0;std::uint64_t model_live=0,consumer_live=0;
        {sb02::phase phase(sb02::parse);hc02::timings stats;auto m=chunk?chunked(input,pool,target,false,stats):perform(input,pool,false);model_live=sb02::total.live.load();
            m.data=batch{};check(sb02::live_requests.load()==0,"requests retained before consumer");consumer_live=sb02::total.live.load();check(consume(m.binding.types,{},false)==checksum,"audit checksum");}
        check(sb02::total.live.load()==0,"model allocation survived last consumer");
        std::cout<<"variant="<<(chunk?"chunk":"sb04")<<",workers="<<workers_count<<",target="<<target<<",allocation_calls="<<sb02::total.calls<<",allocation_bytes="<<sb02::total.bytes<<",peak_bytes="<<sb02::total.peak<<",model_live_bytes="<<model_live<<",consumer_live_bytes="<<consumer_live<<",peak_request_instances="<<sb02::peak_requests<<",requests_at_consumer=0,final_live_bytes=0,validation=passed\n";
    }return 0;
#endif
    for(unsigned trial=0;trial<=repeats;++trial)for(unsigned order=0;order<2;++order){bool chunk=((trial+order)&1)!=0;hc02::timings stats;
        const auto begin=clock_type::now();auto m=chunk?chunked(input,pool,target,false,stats):perform(input,pool,false);
        const auto model=m.total_ms,parse=m.parse_ms,assembly=m.combine_ms,canonical=m.canonical_ms,binding=m.binding_ms,numbering=m.number_ms;
        std::weak_ptr<const request_storage::arena> weak=m.data.requests.owner();m.data=batch{};check(weak.expired(),"requests retained before consumer");
        const auto sum=consume(m.binding.types,{},false);m=measured{};const auto lifecycle=elapsed_ms(begin);check(sum==checksum,"timed checksum");
        std::cout<<std::fixed<<std::setprecision(6)<<"variant="<<(chunk?"chunk":"sb04")<<",trial="<<trial<<",warmup="<<int(trial==0)<<",workers="<<workers_count<<",target="<<target<<",oracle_accepted="<<int(accepted)<<",discovery_ms="<<stats.discovery<<",planning_ms="<<stats.planning<<",parser_ms="<<parse<<",assembly_ms="<<assembly<<",critical_chunk_ms="<<stats.critical<<",canonical_ms="<<canonical<<",binding_ms="<<binding<<",numbering_ms="<<numbering<<",model_ms="<<model<<",lifecycle_ms="<<lifecycle<<",tasks="<<stats.tasks<<",fallback_roots="<<stats.fallbacks<<",plan_capacity_bytes="<<stats.plan_bytes<<",primary_bytes="<<(chunk?chunk_primary:control_primary)<<",retained_bytes="<<(chunk?chunk_bytes:control_bytes)<<",checksum="<<sum<<",validation=passed\n";
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
