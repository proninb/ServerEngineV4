#include "../semantic-bind-02/allocation_audit.hpp"
#define SB03_MODEL_HEADER "../header-semantic-coverage-03/model.hpp"
#define main unused_sb01_cli
#include "../semantic-bind-01/main.cpp"
#undef main
#include "pipeline.hpp"
#include "constructors.hpp"
namespace {
std::vector<sb01::stream> load_numbers(world& w,const fs::path& path,input_times& times){
    auto t=clock_type::now();check(succeeded(compose_project_configuration(path,operation_id{1},w.diagnostics,w.manifest,w.files,w.config)),"compose failed");
    for(id i=1;i<=w.files.size();++i)if(w.files.kind(file_id{i})==file_kind::header)w.roots.push_back(file_id{i});
    check(!w.roots.empty(),"no Header roots");times.compose=elapsed_ms(t);t=clock_type::now();prepared_include_failure_v2 failure;
    check(succeeded(prepare_header_include_closure_v2(w.files,w.roots,w.config,w.closure,&failure)),"physical preparation failed");times.physical=elapsed_ms(t);
    std::vector<sb01::stream> output(w.roots.size());std::vector<std::vector<string_id>> names(w.roots.size());
    preprocessor_v2_failure pf;semantic_preprocessor_v2 pp{w.files,w.closure.view(),w.config,w.strings,&pf};
    token_reuse reuse(pp,w.files.size());pp.configure_reuse(&reuse,&token_reuse::dispatch);t=clock_type::now();
    for(std::size_t r=0;r<w.roots.size();++r){check(succeeded(pp.start(w.roots[r])),"effective preprocessing start failed");
        while(const auto* token=pp.current()){
            output[r].tokens.push_back({token->kind,{},token->number.bits,{token->file.value(),token->source_offset,token->source_length},static_cast<id>(token->number.kind)});
            names[r].push_back(token->identifier);++times.tokens;check(succeeded(pp.advance()),"effective preprocessing advance failed");
        }check(pp.finished(),"incomplete effective stream");
    }
    times.effective=elapsed_ms(t);times.reused=reuse.reused;t=clock_type::now();
    // Resolve spelling views only after all interning is finished: string_table
    // storage may grow during preprocessing. No legacy IDs cross this boundary.
    for(std::size_t r=0;r<output.size();++r)for(std::size_t i=0;i<output[r].tokens.size();++i)output[r].tokens[i].name=w.strings.get(names[r][i]);
    times.adapt=elapsed_ms(t);return output;
}


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
        if(task.fallback&&!nested_coverage_enabled)locals[i]=sb01::parser(input[task.root],task.root).run();
        else locals[i]=hc02::extractor(input[task.root],task.root,task.first,task.last).run();
        durations[i]=elapsed_ms(begin);
    });times.parse=elapsed_ms(stage);out.parse_ms=times.parse;
    for(auto ms:durations)times.critical=(std::max)(times.critical,ms);
    stage=clock_type::now();out.data=hc02::assemble(input,plan,tasks,locals);locals.clear();times.assembly=elapsed_ms(stage);out.combine_ms=times.assembly;
    ca01::memory memory(false);std::vector<ca01::comparisons> counters(pool.size);
    stage=clock_type::now();auto canonical=ca01::canonicalize(out.data.identities,"index",pool,memory,counters,false);out.canonical_ms=elapsed_ms(stage);
    stage=clock_type::now();out.binding=bind(out.data,canonical,pool);out.binding_ms=elapsed_ms(stage);stage=clock_type::now();if(constructor_coverage_enabled)sc02::resolve(out.data,canonical,out.binding);times.construction=elapsed_ms(stage);
    stage=clock_type::now();auto numbered=ca01::number(out.data.identities,canonical,memory);out.number_ms=elapsed_ms(stage);
    out.representatives.assign(canonical.representative.begin(),canonical.representative.end());out.who.assign(numbered.who.begin(),numbered.who.end());out.total_ms=elapsed_ms(start);return out;
}

void exact(const measured& a,const measured& b){
    same_result(a,b);check(a.binding.first.diagnostic_kind==b.binding.first.diagnostic_kind&&a.binding.first.detail==b.binding.first.detail,"diagnostic detail changed by scheduling");
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

}


namespace {
CV default_value(const binding_result& b,id request){auto it=std::lower_bound(b.defaults.begin(),b.defaults.end(),request,[](auto x,id i){return x.request<i;});return it!=b.defaults.end()&&it->request==request?it->value:CV{};}
std::size_t verify_defaults(const measured& m,const semantic_world& o){
    const auto symbols=oracle_symbols(m,o);std::vector<bool> gaps(m.data.declarations.size(),false);for(const auto& op:m.data.operations)if(op.kind==operation_kind::gap)gaps[op.definition]=true;
    std::size_t checked=0;
    m.data.requests.range(0,m.data.requests.size(),[&](auto i,const request& r){if(r.base||gaps[r.definition])return;
        const auto owner=o.G.find_type(symbols[r.owner]);const auto member=o.G.find_member(owner,o.strings.find(r.member));const auto* value=o.G.construction(owner,member);
        check(value&&*value==default_value(m.binding,static_cast<id>(i)),"constructor construction_value differs from V2");++checked;});return checked;
}
void verify_error(const measured& m,const oracle_result& expected){
    if(m.binding.first.kind!=error_kind::coverage){verify_negative(m,expected);return;}
    const auto& actual=m.binding.first;check(actual.diagnostic_kind==static_cast<id>(expected.failure.kind)&&actual.detail==expected.failure.detail&&actual.source.file==expected.failure.file.value()&&actual.source.offset==expected.failure.source.offset&&actual.source.length==expected.failure.source.length,"constructor first diagnostic kind/detail/origin differs from V2");
}
void verify_nested(const measured& m,const semantic_world& o){
    const auto symbols=oracle_symbols(m,o);std::unordered_map<id,id> reverse;std::unordered_set<id> excluded;
    for(id i=0;i<symbols.size();++i)reverse.emplace(symbols[i].value(),m.representatives[i]);
    for(const auto& op:m.data.operations)if(op.kind==operation_kind::gap)excluded.insert(m.representatives[m.data.declarations[op.definition].symbol]);
    std::map<std::pair<id,std::string>,CV> expected,actual;
    for(const auto& v:o.G.constructor_defaults.entries()){auto owner=reverse.at(v.owner.value());if(!excluded.contains(owner))expected.emplace(std::make_pair(owner,std::string(o.strings.get(v.path))),v.value);}
    for(const auto& v:m.binding.nested)actual.emplace(std::make_pair(v.owner,v.path),v.value);
    check(expected==actual,"nested constructor defaults differ from V2");
}
std::size_t nested_bytes(const binding_result& b){std::size_t bytes=b.nested.capacity()*sizeof(resolved_nested);for(const auto& v:b.nested)if(v.path.capacity()>15)bytes+=v.path.capacity()+1;return bytes;}
std::uint64_t consume_all(const binding_result& b){auto hash=consume(b.types,{},false);for(const auto& d:b.defaults){hash=ca01::mix(hash^d.request);hash=ca01::mix(hash^static_cast<id>(d.value.kind));hash=ca01::mix(hash^d.value.bits());hash=ca01::mix(hash^d.value.operand);}for(const auto& d:b.nested){hash=ca01::mix(hash^d.owner^ca01::hash_text(d.path));hash=ca01::mix(hash^static_cast<id>(d.value.kind)^d.value.bits()^d.value.operand);}return hash;}
void tests(){
    const std::vector<std::string> cases={
        "struct B { int x; }; struct A { B b[2]; A(){b[1].x=3;b[1].x=5;} };",
        "struct B { int x; }; struct A { A(){b.x=1;b.x=2;} B b; };",
        "struct A { A(){a[3]=1;a[0]=2;} int a[2]; };",
        "struct B { int x; }; struct A { B& b; A(){b.x=3;} };",
        "struct A { int a[2][3]; A(){a[1][2]=7;} };",
        "struct A { int x; int& r; A():r(x){} };",
        "struct A { int& r; A():r(x){} int x; };",
        "struct A { A():r(x){} int& r; int x; };",
        "struct A { int& r; A():r(late){r=nullptr;} int late; };",
        "struct A { int& r; A():r(late){r=nullptr;} bool late; };",
        "struct A { int& r; A():r(missing){r=nullptr;} };",
        "struct A { int known; int& r; A():r(late){r=known;} int late; };",
        "struct A { int& r; A(){r=a;r=b;} int b; int a; };",
        "struct A { int& r; int& q; A():r(x),q(x){} bool x; };",
"struct A { int x; A():x(3){} };","struct A { A(){x=2;x=4;} int x; };","struct A { int x; A(){x=-3;} };","struct A { int* x; A():x(1){} };","struct A { int x; A(){} A(int y){} };","struct A { A():missing(1){} int x; };","struct A { A():x(1){} Missing y; int x; };","namespace N { typedef int T; struct A { A():x(1){} T x; }; }"};
    std::size_t count=0;for(const auto& source:cases){std::vector<sb01::stream> input{lex(source)};for(auto& token:input[0].tokens)if(token.kind==K::pp_number)token.number_kind=1;
        for(auto workers:{1u,2u,4u,8u,16u}){ca01::pool pool(workers);constructor_coverage_enabled=true;reference_coverage_enabled=true;nested_coverage_enabled=true;hc02::timings base;auto reference=chunked(input,pool,8192,false,base);
            for(auto target:{1u,32u,8192u})for(bool reverse:{false,true}){hc02::timings t;auto m=chunked(input,pool,target,reverse,t);exact(reference,m);check(consume_all(reference.binding)==consume_all(m.binding),"nested result changed by scheduling");check(reference.binding.defaults.size()==m.binding.defaults.size(),"defaults count changed");for(std::size_t i=0;i<m.binding.defaults.size();++i)check(reference.binding.defaults[i].request==m.binding.defaults[i].request&&reference.binding.defaults[i].value==m.binding.defaults[i].value,"defaults changed by scheduling");++count;}}}
    binding_result saved;std::weak_ptr<const request_storage::arena> request_owner;std::weak_ptr<const shape_arena> shape_owner;
    {std::string text="struct A { int x; int& r; A():r(x){} };";std::vector<sb01::stream> input{lex(text)};ca01::pool pool(4);hc02::timings t;reference_coverage_enabled=true;
        auto m=chunked(input,pool,1,true,t);request_owner=m.data.requests.owner();shape_owner=m.data.requests.owner()->shape_owners.at(0);saved=std::move(m.binding);}
    check(request_owner.expired()&&!shape_owner.expired(),"reference result owns Parser requests or lost shapes");auto copy=saved;saved={};const auto expected=consume_all(copy);
    std::vector<std::string> churn(1000,std::string(1024,'x'));check(consume_all(copy)==expected&&default_value(copy,1)==CV::member_binding(1),"reference value/operand lifetime lost");copy={};check(shape_owner.expired(),"shape survived last reference consumer");
    std::weak_ptr<const path_arena> path_owner;
    {std::string text="struct B { int x; }; struct A { B b; A(){b.x=2;} };";std::vector<sb01::stream> input{lex(text)};for(auto& token:input[0].tokens)if(token.kind==K::pp_number)token.number_kind=1;ca01::pool pool(4);hc02::timings t;auto m=chunked(input,pool,1,true,t);for(const auto& arena:m.data.paths)if(!arena->steps.empty())path_owner=arena;saved=std::move(m.binding);}
    check(path_owner.expired()&&saved.nested.size()==1&&saved.nested[0].path=="b.x", "nested result retains Parser path arena or lost path");auto nested_copy=saved;saved={};std::vector<std::string> nested_churn(1000,std::string(1024,'y'));check(nested_copy.nested[0].path=="b.x"&&nested_copy.nested[0].value==CV::constant(CK::unsigned_integer,2),"nested result lifetime lost");
    std::cout<<"constructor_schedule_checks="<<count<<",validation=passed\n";
}
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string_view(argv[1])=="--self-test"){selected_variant=variant::combined;tests();return 0;}
    if(argc!=5){std::cerr<<"Usage: HeaderSemanticCoverage03 <project> <workers> <target> <repeats>\n";return 2;}
    selected_variant=variant::combined;const auto worker_count=std::stoul(argv[2]),target=std::stoul(argv[3]),repeats=std::stoul(argv[4]);check(target>0&&repeats>0&&repeats<=30,"invalid configuration");
    static_assert(sizeof(sb01::token)==48);world w;input_times prep;const auto input=load_numbers(w,fs::path(argv[1]),prep);ca01::pool pool(worker_count);
    struct extraction_audit {std::size_t ctors=0,scalars=0,refs=0,nested=0;std::array<std::size_t,7> gaps{};};
    extraction_audit audits[2];auto audit=[](const batch& b){extraction_audit a;for(const auto& op:b.operations){a.ctors+=op.kind==operation_kind::begin;a.scalars+=op.kind==operation_kind::scalar;a.refs+=op.kind==operation_kind::reference;a.nested+=op.kind==operation_kind::nested;if(op.kind==operation_kind::gap)++a.gaps[static_cast<unsigned>(op.gap)];}return a;};
    std::uint64_t checksums[2]{};std::size_t retained_sizes[2]{};std::array<std::size_t,2> checked{},gaps{};std::size_t raw_constructors=0,raw_scalars=0,raw_references=0;std::array<std::size_t,7> gap_counts{};bool accepted,constructor_error=false;
    {constructor_coverage_enabled=true;reference_coverage_enabled=true;nested_coverage_enabled=true;hc02::timings t;auto m=chunked(input,pool,target,false,t);hc02::timings reverse;auto reversed=chunked(input,pool,target,true,reverse);exact(m,reversed);check(consume_all(m.binding)==consume_all(reversed.binding),"construction differs by reverse scheduling");
        semantic_world o;const auto expected=oracle(w,o);accepted=expected.accepted;gaps[1]=m.binding.gap_definitions;
        if(accepted){verify_positive(m,o);checked[1]=verify_defaults(m,o);verify_nested(m,o);}else if(!gaps[1]){verify_error(m,expected);constructor_error=m.binding.first.kind==error_kind::coverage;}
        for(const auto& op:m.data.operations){raw_constructors+=op.kind==operation_kind::begin;raw_scalars+=op.kind==operation_kind::scalar;raw_references+=op.kind==operation_kind::reference;if(op.kind==operation_kind::gap)++gap_counts[static_cast<unsigned>(op.gap)];}
        audits[1]=audit(m.data);retained_sizes[1]=retained(m)+m.binding.defaults.capacity()*sizeof(resolved_default)+nested_bytes(m.binding);checksums[1]=consume_all(m.binding);if(accepted)check(raw_references==m.binding.reference_events+m.binding.skipped_reference_events,"unclassified reference events");
        nested_coverage_enabled=false;hc02::timings control;auto baseline=chunked(input,pool,target,false,control);gaps[0]=baseline.binding.gap_definitions;audits[0]=audit(baseline.data);if(accepted){checked[0]=verify_defaults(baseline,o);verify_nested(baseline,o);}retained_sizes[0]=retained(baseline)+baseline.binding.defaults.capacity()*sizeof(resolved_default)+nested_bytes(baseline.binding);checksums[0]=consume_all(baseline.binding);
        if(accepted&&baseline.binding.first.kind==error_kind::none)exact(baseline,m);
    }
#ifdef SB02_ALLOCATIONS
    for(unsigned mode=0;mode<2;++mode){nested_coverage_enabled=mode!=0;sb02::total.peak=0;sb02::total.calls=0;sb02::total.bytes=0;sb02::peak_requests=0;std::uint64_t consumer_live=0;
        {sb02::phase phase(sb02::parse);hc02::timings t;auto m=chunked(input,pool,target,false,t);m.data=batch{};check(sb02::live_requests.load()==0,"requests retained before consumer");consumer_live=sb02::total.live.load();check(consume_all(m.binding)==checksums[mode],"audit checksum");}
        check(sb02::total.live.load()==0,"tracked allocations survived result");std::cout<<"variant="<<(mode?"nested":"references02")<<",allocation_calls="<<sb02::total.calls<<",allocation_bytes="<<sb02::total.bytes<<",peak_bytes="<<sb02::total.peak<<",consumer_live_bytes="<<consumer_live<<",requests_at_consumer=0,final_live_bytes=0,validation=passed\n";
    }return 0;
#endif
    for(unsigned trial=0;trial<=repeats;++trial)for(unsigned order=0;order<2;++order){const unsigned mode=(trial+order)&1;nested_coverage_enabled=mode!=0;hc02::timings t;const auto begin=clock_type::now();auto m=chunked(input,pool,target,false,t);
        const auto bind_ms=m.binding_ms,model_ms=m.total_ms;const auto ctors=m.binding.constructors,ops=m.binding.scalar_operations,defs=m.binding.covered_definitions,uncovered=m.binding.gap_definitions,nvalues=m.binding.defaults.size(),nnested=m.binding.nested.size(),raw=m.data.operations.size(),retained_bytes=retained_sizes[mode];
        const auto ref_events=m.binding.reference_events,ref_skipped=m.binding.skipped_reference_events,ref_immediate=m.binding.immediate_references,ref_queued=m.binding.queued_references,ref_resolved=m.binding.resolved_references,ref_stale=m.binding.stale_references,ref_pending=m.binding.pending_reference_targets,ref_superseded=m.binding.superseded_reference_targets;
        m.data=batch{};const auto sum=consume_all(m.binding);m=measured{};const auto lifecycle=elapsed_ms(begin);check(sum==checksums[mode],"timed checksum mismatch");
        std::cout<<std::fixed<<std::setprecision(6)<<"variant="<<(mode?"nested":"references02")<<",workers="<<worker_count<<",target="<<target<<",trial="<<trial<<",warmup="<<int(trial==0)<<",category_status="<<(gaps[mode]?"partial":(accepted?"verified_values":"verified_diagnostic"))<<",oracle_accepted="<<int(accepted)<<",constructor_error="<<int(constructor_error)<<",checked_construction_fields="<<checked[mode]<<",extracted_constructors="<<audits[mode].ctors<<",extracted_scalar_ops="<<audits[mode].scalars<<",extracted_reference_ops="<<audits[mode].refs<<",reference_gaps="<<audits[mode].gaps[1]<<",nested_gaps="<<audits[mode].gaps[2]<<",body_gaps="<<audits[mode].gaps[3]<<",initializer_gaps="<<audits[mode].gaps[4]<<",structural_gaps="<<audits[mode].gaps[5]<<",operand_gaps="<<audits[mode].gaps[6]<<",constructors="<<ctors<<",scalar_operations="<<ops<<",operation_events="<<raw<<",covered_definitions="<<defs<<",gap_definitions="<<uncovered<<",reference_events="<<ref_events<<",references_in_gap_definitions="<<ref_skipped<<",immediate_references="<<ref_immediate<<",queued_references="<<ref_queued<<",resolved_references="<<ref_resolved<<",stale_references="<<ref_stale<<",pending_reference_targets="<<ref_pending<<",superseded_reference_targets="<<ref_superseded<<",extracted_nested_ops="<<audits[mode].nested<<",nested_defaults="<<nnested<<",defaults="<<nvalues<<",discovery_ms="<<t.discovery<<",extraction_ms="<<t.parse<<",assembly_ms="<<t.assembly<<",type_binding_ms="<<bind_ms<<",constructor_resolution_ms="<<t.construction<<",model_ms="<<model_ms<<",lifecycle_ms="<<lifecycle<<",retained_bytes="<<retained_bytes<<",checksum="<<sum<<",validation=passed\n";
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
