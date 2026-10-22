#include "../semantic-bind-02/allocation_audit.hpp"
#define SB03_MODEL_HEADER "../semantic-bind-04/model.hpp"
#define main unused_sb01_cli
#include "../semantic-bind-01/main.cpp"
#undef main
#include "boundaries.hpp"
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

void validate(const std::vector<sb01::stream>& input,const hc01::plan& p){
    check(p.fallback_roots.empty(),"positive corpus needs unsplit fallback");
    std::size_t units=0,contexts=0;
    for(id root=0;root<input.size();++root){auto local=sb01::parser(input[root],root).run();check(local.failure.kind==error_kind::none,"boundary validation needs accepted Parser input");
        std::vector<const hc01::unit*> root_units;for(const auto& u:p.units)if(u.root==root)root_units.push_back(&u);
        for(const auto& d:local.declarations){
            const auto& symbol=local.symbols[d.symbol];
            if(d.kind==declaration_kind::name_space){
                const auto it=std::find_if(p.contexts.begin(),p.contexts.end(),[&](const auto& c){return c.at==d.begin;});
                check(it!=p.contexts.end(),"missing namespace context event");++contexts;
                check(input[root].tokens[it->name_token].name==symbol.name,"namespace spelling changed");
                auto parent=symbol.parent,ctx=it->parent;
                while(parent!=0){check(ctx!=none,"missing namespace ancestor");const auto& c=p.contexts.at(ctx);check(c.at==local.symbols[parent].event,"namespace parent event changed");parent=local.symbols[parent].parent;ctx=c.parent;}
                check(ctx==none,"extra namespace ancestor");continue;
            }
            const auto token_index=static_cast<std::uint32_t>(d.begin)-1;
            const auto it=std::upper_bound(root_units.begin(),root_units.end(),token_index,[](std::size_t index,const hc01::unit* u){return index<u->first;});
            check(it!=root_units.begin(),"missing declaration boundary");const auto& u=**std::prev(it);++units;
            check(token_index<u.last&&token_index==u.first+(d.kind==declaration_kind::alias?1:0),"boundary start differs from Parser event");
            if(d.complete!=UINT64_MAX)check(position(root,u.last-1)==d.complete,"boundary end differs from completion event");
            auto parent=symbol.parent,ctx=u.context;
            while(parent!=0){check(ctx!=none,"declaration lost namespace context");const auto& c=p.contexts.at(ctx);check(c.root==root&&c.at==local.symbols[parent].event,"declaration scope event differs");parent=local.symbols[parent].parent;ctx=c.parent;}
            check(ctx==none,"declaration gained namespace context");
        }
    }
    check(units==p.units.size()&&contexts==p.contexts.size(),"extra or missing boundary/context");
}
void tests(){
    selected_variant=variant::combined;
    const std::vector<std::string> texts={
        "struct A; struct B { A* p; }; struct A { int value; };",
        "namespace N { struct A; } namespace N { struct A {}; struct B { A x; }; }",
        "namespace N { namespace M { typedef int T; struct A { T (*p)[2][3]; }; } }",
        "struct A { A() { int local; } int f() { return 1; } int x; };",
        "struct A {}; namespace N { struct A {}; } ;",
        "typedef int T; struct A { const T* a, *b; };"
    };
    for(const auto& text:texts){std::vector<sb01::stream> input{lex(text)};const auto p=hc01::discover(input);validate(input,p);
        for(auto target:{1u,32u,1024u}){const auto chunks=hc01::prepare(p,target);std::size_t count=0;
            for(const auto& c:chunks){check(c.first<c.last&&c.units>0,"empty chunk");count+=c.units;}
            check(count==p.units.size(),"chunk assembly lost units");}}
    for(const auto& text:{"namespace N { struct A {};", "struct A { int x[2); };", "template <class T> struct A {};", "struct A {}"}){
        std::vector<sb01::stream> input{lex(text)};const auto p=hc01::discover(input);check(p.fallback_roots.size()==1&&p.units.empty()&&p.contexts.empty(),"unsafe partial split survived fallback");}
    std::vector<sb01::stream> empty(2);auto p=hc01::discover(empty);check(p.units.empty()&&p.fallback_roots.empty(),"empty roots changed");
    self_test();std::cout<<"boundary_model=passed\n";
}
}

int main(int argc,char** argv){try{
    if(argc==2&&std::string_view(argv[1])=="--self-test"){tests();return 0;}
    if(argc!=5){std::cerr<<"Usage: HeaderChunk01 <project> <workers> <target_tokens> <repeats>\n";return 2;}
    selected_variant=variant::combined;const auto workers_count=std::stoul(argv[2]),target=std::stoul(argv[3]),repeats=std::stoul(argv[4]);check(repeats>=1&&repeats<=30,"invalid repeats");
    world w;input_times prep;const auto input=load(w,fs::path(argv[1]),prep);ca01::pool workers(workers_count);
    {auto p=hc01::discover(input);validate(input,p);auto m=perform(input,workers,false);semantic_world o;check(oracle(w,o).accepted,"positive real input required");verify_positive(m,o);}
#ifdef SB02_ALLOCATIONS
    std::uint64_t plan_live=0,planning_peak=0,consumer_live=0;
    {sb02::phase phase(sb02::parse);auto p=hc01::discover(input);auto chunks=hc01::prepare(p,target);plan_live=sb02::total.live.load();planning_peak=sb02::total.peak.load();
        auto m=perform(input,workers,false);std::weak_ptr<const request_storage::arena> weak=m.data.requests.owner();m.data=batch{};weak.reset();
        check(sb02::live_requests.load()==0,"live requests at consumer");consumer_live=sb02::total.live.load();
        check(consume(m.binding.types,{},false)!=0,"empty consumer checksum");}
    check(sb02::total.live.load()==0,"tracked memory survived final consumer");
    std::cout<<"workers="<<workers_count<<",target="<<target<<",allocation_calls="<<sb02::total.calls<<",allocation_bytes="<<sb02::total.bytes<<",model_and_plan_peak_bytes="<<sb02::total.peak<<",planning_peak_bytes="<<planning_peak<<",plan_live_bytes="<<plan_live<<",consumer_live_bytes="<<consumer_live<<",requests_at_consumer=0,final_live_bytes=0,validation=passed\n";return 0;
#endif
    for(unsigned trial=0;trial<=repeats;++trial){
        const auto begin=clock_type::now();auto p=hc01::discover(input);const auto discovery_ms=elapsed_ms(begin);
        auto stage=clock_type::now();auto chunks=hc01::prepare(p,target);const auto context_ms=elapsed_ms(stage);
        std::size_t largest=0,largest_root=0,unity_chunks=0,unit_tokens=0,largest_unit=0;id critical_root=none,largest_root_id=none;
        for(const auto& c:chunks){const auto n=c.last-c.first;unit_tokens+=n;if(n>largest){largest=n;critical_root=c.root;}}
        for(id i=0;i<input.size();++i)if(input[i].tokens.size()>largest_root){largest_root=input[i].tokens.size();largest_root_id=i;}
        for(const auto& u:p.units)largest_unit=(std::max)(largest_unit,u.last-u.first);
        for(const auto& c:chunks)if(input[c.root].tokens.size()==largest_root)++unity_chunks;
        double largest_root_discovery_ms=0;
        {hc01::plan root_plan;stage=clock_type::now();hc01::discovery(root_plan,input.at(largest_root_id),largest_root_id).run();largest_root_discovery_ms=elapsed_ms(stage);}
        // SB04 control: this deliberately still parses whole roots. It is
        // not reported as a chunk Parser or as a chunked lifecycle.
        const auto lifecycle_start=clock_type::now();auto m=perform(input,workers,false);const auto model_ms=m.total_ms,parse_ms=m.parse_ms;
        std::weak_ptr<const request_storage::arena> weak=m.data.requests.owner();m.data=batch{};check(weak.expired(),"requests retained at consumer");
        const auto checksum=consume(m.binding.types,{},false);m=measured{};const auto lifecycle_ms=elapsed_ms(lifecycle_start);
        const auto plan_bytes=p.bytes()+chunks.capacity()*sizeof(hc01::chunk);
        std::cout<<std::fixed<<std::setprecision(6)<<"trial="<<trial<<",warmup="<<int(trial==0)<<",workers="<<workers_count<<",target="<<target<<",tokens="<<p.tokens<<",units="<<p.units.size()<<",contexts="<<p.contexts.size()<<",chunks="<<chunks.size()<<",largest_unit_tokens="<<largest_unit<<",largest_chunk_tokens="<<largest<<",critical_root="<<critical_root<<",largest_root_id="<<largest_root_id<<",largest_root_tokens="<<largest_root<<",largest_root_chunks="<<unity_chunks<<",declaration_tokens="<<unit_tokens<<",discovery_ms="<<discovery_ms<<",largest_root_discovery_ms="<<largest_root_discovery_ms<<",context_ms="<<context_ms<<",plan_capacity_bytes="<<plan_bytes<<",sb04_parse_ms="<<parse_ms<<",sb04_model_ms="<<model_ms<<",sb04_lifecycle_ms="<<lifecycle_ms<<",checksum="<<checksum<<",fallback_roots="<<p.fallback_roots.size()<<",validation=passed\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
