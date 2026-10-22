#include "allocation_audit.hpp"
#define main sb01_reference_main
#include "../semantic-bind-01/main.cpp"
#undef main

namespace {
struct root_sample {std::size_t worker=0;double begin=0,end=0;std::uint64_t cycles=0;std::size_t requests=0,symbols=0,declarations=0,request_capacity=0;};
// Independent baseline layout: the optional empty lifecycle member must not
// change request size/alignment in the allocation-diagnostic executable.
struct request_abi_layout {id scope,owner,definition;std::string_view member;type_expr type;event at;origin source;bool base;origin incomplete_source;};
static_assert(sizeof(request)==sizeof(request_abi_layout)&&alignof(request)==alignof(request_abi_layout),"audit changed request ABI");
struct memory_row {const char* point;const char* array;std::size_t size,capacity,element;};
struct phase_row {const char* point;std::uint64_t live_bytes,peak_bytes,live_requests,peak_requests;};
struct audit_data {
    std::vector<root_sample> roots;
    std::vector<memory_row> memory;
    std::vector<phase_row> phases;
};
std::uint64_t thread_cycles(){
#ifdef _WIN32
    ULONG64 value=0;QueryThreadCycleTime(GetCurrentThread(),&value);return value;
#else
    return 0;
#endif
}
template<class V>void array_row(audit_data& a,const char* point,const char* name,const V& v){a.memory.push_back({point,name,v.size(),v.capacity(),sizeof(typename V::value_type)});}
void modifiers_row(audit_data& a,const char* point,const char* name,const auto& records,const auto& get){
    std::size_t size=0,capacity=0;for(const auto& r:records){const auto& layers=get(r);size+=layers.size();capacity+=layers.capacity();}
    a.memory.push_back({point,name,size,capacity,sizeof(modifier)});
}
void snapshot(audit_data& a,const char* point,const measured& m,const std::vector<local_output>& locals){
    std::size_t symbols=0,sc=0,decl=0,dc=0,req=0,rc=0,mods=0,mc=0;
    for(const auto& l:locals){symbols+=l.symbols.size();sc+=l.symbols.capacity();decl+=l.declarations.size();dc+=l.declarations.capacity();req+=l.requests.size();rc+=l.requests.capacity();
        for(const auto& r:l.requests){mods+=r.type.layers.size();mc+=r.type.layers.capacity();}}
    a.memory.push_back({point,"worker_symbols",symbols,sc,sizeof(ca01::symbol)});
    a.memory.push_back({point,"worker_declarations",decl,dc,sizeof(declaration)});
    a.memory.push_back({point,"worker_requests",req,rc,sizeof(request)});
    a.memory.push_back({point,"worker_modifiers",mods,mc,sizeof(modifier)});
    array_row(a,point,"batch_symbols",m.data.identities.symbols);array_row(a,point,"batch_declarations",m.data.declarations);array_row(a,point,"batch_requests",m.data.requests);
    modifiers_row(a,point,"batch_modifiers",m.data.requests,[](const auto& r)->const auto&{return r.type.layers;});
    array_row(a,point,"bound_types",m.binding.types);array_row(a,point,"per_request_failures",m.binding.failures);
    modifiers_row(a,point,"bound_modifiers",m.binding.types,[](const auto& r)->const auto&{return r.layers;});
    array_row(a,point,"representatives",m.representatives);array_row(a,point,"who",m.who);
    std::size_t ls=0,lc=0,cs=0,cc=0;
    for(const auto& level:m.data.identities.levels){ls+=level.size();lc+=level.capacity();}
    for(const auto& children:m.data.identities.children){cs+=children.size();cc+=children.capacity();}
    a.memory.push_back({point,"level_elements",ls,lc,sizeof(id)});a.memory.push_back({point,"child_elements",cs,cc,sizeof(id)});
    array_row(a,point,"level_vector_headers",m.data.identities.levels);array_row(a,point,"child_vector_headers",m.data.identities.children);
    a.phases.push_back({point,sb02::total.live.load(),sb02::total.peak.load(),sb02::live_requests.load(),sb02::peak_requests.load()});
}
measured profile(const std::vector<sb01::stream>& input,ca01::pool& workers,audit_data& audit){
    measured out;const auto start=clock_type::now();std::vector<local_output> locals;
    {sb02::phase stage(sb02::parse);locals.resize(input.size());}
    auto t=clock_type::now();
    {sb02::phase stage(sb02::parse);workers.run(input.size(),[&](std::size_t i,std::size_t worker){
        auto& sample=audit.roots[i];sample.worker=worker;const auto begin=clock_type::now();const auto cycles=thread_cycles();
        {sb02::root_scope root(static_cast<unsigned>(i));locals[i]=sb01::parser(input[i],static_cast<id>(i)).run();}
        sample.cycles=thread_cycles()-cycles;const auto end=clock_type::now();
        sample.begin=std::chrono::duration<double,std::milli>(begin-t).count();sample.end=std::chrono::duration<double,std::milli>(end-t).count();
        sample.requests=locals[i].requests.size();sample.symbols=locals[i].symbols.size();sample.declarations=locals[i].declarations.size();sample.request_capacity=locals[i].requests.capacity();
    });}
    out.parse_ms=elapsed_ms(t);snapshot(audit,"after_parse",out,locals);
    t=clock_type::now();{sb02::phase stage(sb02::combine);out.data=combine(locals);}out.combine_ms=elapsed_ms(t);snapshot(audit,"after_combine_before_worker_release",out,locals);
    {sb02::phase stage(sb02::release_locals);locals.clear();}snapshot(audit,"after_worker_release",out,locals);
    ca01::memory memory(false);std::vector<ca01::comparisons> counters(workers.size);
    t=clock_type::now();
    auto canonical=[&]{sb02::phase stage(sb02::canonical);return ca01::canonicalize(out.data.identities,"index",workers,memory,counters,false);}();
    out.canonical_ms=elapsed_ms(t);snapshot(audit,"after_canonical",out,locals);
    t=clock_type::now();{sb02::phase stage(sb02::binding);out.binding=bind(out.data,canonical,workers);}out.binding_ms=elapsed_ms(t);snapshot(audit,"after_binding",out,locals);
    t=clock_type::now();auto numbered=[&]{sb02::phase stage(sb02::numbering);return ca01::number(out.data.identities,canonical,memory);}();out.number_ms=elapsed_ms(t);
    {sb02::phase stage(sb02::export_result);out.representatives.assign(canonical.representative.begin(),canonical.representative.end());out.who.assign(numbered.who.begin(),numbered.who.end());}
    out.total_ms=elapsed_ms(start);out.peak=peak_ws();snapshot(audit,"after_numbering",out,locals);
    return out;
}
void write_types(const fs::path& dir){
    std::ofstream out(dir/"types.csv");out<<"type,sizeof_bytes\n";
    out<<"token,"<<sizeof(token)<<"\nsymbol,"<<sizeof(ca01::symbol)<<"\ndeclaration,"<<sizeof(declaration)<<"\nrequest,"<<sizeof(request)<<"\ntype_expr,"<<sizeof(type_expr)<<"\nbound_type,"<<sizeof(bound_type)<<"\nmodifier,"<<sizeof(modifier)<<"\nissue,"<<sizeof(issue)<<"\norigin,"<<sizeof(origin)<<"\nmodifier_vector_header,"<<sizeof(std::vector<modifier>)<<'\n';
    std::ofstream layout(dir/"request-layout.csv");layout<<"field,offset,bytes\n";
    layout<<"scope,"<<offsetof(request,scope)<<','<<sizeof(request::scope)<<"\nowner,"<<offsetof(request,owner)<<','<<sizeof(request::owner)<<"\ndefinition,"<<offsetof(request,definition)<<','<<sizeof(request::definition)<<"\nmember,"<<offsetof(request,member)<<','<<sizeof(request::member)<<"\ntype,"<<offsetof(request,type)<<','<<sizeof(request::type)<<"\nat,"<<offsetof(request,at)<<','<<sizeof(request::at)<<"\nsource,"<<offsetof(request,source)<<','<<sizeof(request::source)<<"\nbase,"<<offsetof(request,base)<<','<<sizeof(request::base)<<"\nincomplete_source,"<<offsetof(request,incomplete_source)<<','<<sizeof(request::incomplete_source)<<'\n';
}
void write_audit(const fs::path& dir,const std::vector<sb01::stream>& input,const world& w,const measured& m,const audit_data& a,std::size_t workers){
    std::ofstream roots(dir/"roots.csv");roots<<"root_ordinal,file_id,tokens,worker,begin_ms,end_ms,wall_ms,thread_cycles,allocation_calls,allocation_bytes,malloc_ns,requests,symbols,declarations,request_capacity,path\n";
    for(std::size_t i=0;i<input.size();++i){const auto& r=a.roots[i];const auto& c=sb02::roots[i];roots<<i<<','<<w.roots[i].value()<<','<<input[i].tokens.size()<<','<<r.worker<<','<<std::setprecision(9)<<r.begin<<','<<r.end<<','<<r.end-r.begin<<','<<r.cycles<<','<<c.calls.load()<<','<<c.bytes.load()<<','<<c.malloc_ns.load()<<','<<r.requests<<','<<r.symbols<<','<<r.declarations<<','<<r.request_capacity<<','<<std::quoted(fs::path(w.files.path(w.roots[i])).generic_string(),'"','"')<<'\n';}
    std::ofstream worker(dir/"workers.csv");worker<<"worker,tasks,tokens,task_wall_ms,first_start_ms,last_end_ms,dispatch_delay_ms,between_task_gap_ms,tail_wait_ms,non_task_wall_ms,thread_cycles\n";
    for(std::size_t i=0;i<workers;++i){std::size_t count=0,tokens=0;double busy=0,first=m.parse_ms,last=0;std::uint64_t cycles=0;
        for(std::size_t j=0;j<a.roots.size();++j){const auto& r=a.roots[j];if(r.worker!=i)continue;++count;tokens+=input[j].tokens.size();busy+=r.end-r.begin;first=(std::min)(first,r.begin);last=(std::max)(last,r.end);cycles+=r.cycles;}
        worker<<i<<','<<count<<','<<tokens<<','<<std::setprecision(9)<<busy<<','<<first<<','<<last<<','<<(count?first:0)<<','<<(count?(std::max)(0.0,last-first-busy):0)<<','<<(count?(std::max)(0.0,m.parse_ms-last):m.parse_ms)<<','<<(std::max)(0.0,m.parse_ms-busy)<<','<<cycles<<'\n';
    }
    std::ofstream mem(dir/"arrays.csv");mem<<"point,array,size,capacity,element_bytes,size_bytes,capacity_bytes\n";
    for(const auto& r:a.memory)mem<<r.point<<','<<r.array<<','<<r.size<<','<<r.capacity<<','<<r.element<<','<<r.size*r.element<<','<<r.capacity*r.element<<'\n';
    std::ofstream phases(dir/"lifetimes.csv");phases<<"point,tracked_requested_live_bytes,tracked_requested_peak_bytes,request_instances_live,request_instances_peak\n";
    for(const auto& p:a.phases)phases<<p.point<<','<<p.live_bytes<<','<<p.peak_bytes<<','<<p.live_requests<<','<<p.peak_requests<<'\n';
}
void write_allocations(const fs::path& dir){
    std::ofstream out(dir/"allocations.csv");out<<"allocation_origin,calls,requested_bytes,live_requested_bytes,peak_origin_live_bytes,peak_origin_live_allocations,malloc_ns\n";
    for(unsigned i=1;i<sb02::stage_count;++i){const auto& c=sb02::stages[i];out<<sb02::stage_names[i]<<','<<c.calls.load()<<','<<c.bytes.load()<<','<<c.live.load()<<','<<c.peak.load()<<','<<c.peak_allocations.load()<<','<<c.malloc_ns.load()<<'\n';}
    const auto& c=sb02::total;out<<"total,"<<c.calls.load()<<','<<c.bytes.load()<<','<<c.live.load()<<','<<c.peak.load()<<','<<c.peak_allocations.load()<<','<<c.malloc_ns.load()<<'\n';
}
void audit_self_test(){
#ifdef SB02_ALLOCATIONS
    const auto before=sb02::total.live.load();
    void* allocation=nullptr;
    {sb02::phase stage(sb02::parse);sb02::root_scope root(0);allocation=::operator new(37,std::align_val_t{64});
        check(reinterpret_cast<std::uintptr_t>(allocation)%64==0,"audit aligned allocation failed");check(sb02::total.live.load()==before+37,"allocation accounting failed");}
    // Attribution and release must survive a phase and thread boundary.
    std::thread release([&]{::operator delete(allocation,std::align_val_t{64});});release.join();
    check(sb02::total.live.load()==before,"cross-thread allocation release failed");
    const auto live=sb02::live_requests.load();
    {request r;std::vector<request> v;v.reserve(2);v.push_back(r);v.push_back(std::move(r));check(sb02::live_requests.load()==live+3,"request lifecycle accounting failed");}
    check(sb02::live_requests.load()==live,"request destruction accounting failed");
#endif
}
}
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string_view(argv[1])=="--self-test"){audit_self_test();self_test();return 0;}
        if(argc!=5){std::cerr<<"Usage: SemanticBind02<Timing|Allocations> <project.json> <workers> <control|timing|alloc> <new-output-directory>\n";return 2;}
        const auto workers=std::stoul(argv[2]);const std::string_view mode=argv[3];const fs::path dir=fs::absolute(argv[4]);
#ifdef SB02_ALLOCATIONS
        check(mode=="alloc","allocation executable requires alloc mode");
#else
        check(mode=="timing"||mode=="control","timing executable requires timing/control mode");
#endif
        check(!fs::exists(dir),"audit output directory must be new");fs::create_directories(dir);
        auto w=std::make_unique<world>();input_times input_metrics;std::vector<sb01::stream> input;
        {sb02::phase p(sb02::input);input=load(*w,fs::path(argv[1]),input_metrics);}
        check(input.size()<sb02::max_roots,"root audit capacity exceeded");
        auto t=clock_type::now();std::unique_ptr<ca01::pool> pool;{sb02::phase p(sb02::pool_start);pool=std::make_unique<ca01::pool>(workers);}const auto startup=elapsed_ms(t);
        audit_data audit;audit.roots.resize(input.size());audit.memory.reserve(160);audit.phases.reserve(16);
        auto result=mode=="control"?run(input,*pool,false):profile(input,*pool,audit);
        // Oracle is deliberately excluded from tracked allocation stages.
        {semantic_world o;const auto expected=oracle(*w,o);if(expected.accepted)verify_positive(result,o);else verify_negative(result,expected);}
        if(mode!="control")write_audit(dir,input,*w,result,audit,workers);
        write_types(dir);
        std::ofstream summary(dir/"summary.csv");summary<<"mode,workers,roots,effective_tokens,parse_ms,combine_ms,canonical_ms,binding_ms,numbering_ms,model_total_ms,pool_start_ms,process_peak_ws_bytes,request_lifetime_peak,validation\n";
        summary<<mode<<','<<workers<<','<<input.size()<<','<<input_metrics.tokens<<','<<std::setprecision(9)<<result.parse_ms<<','<<result.combine_ms<<','<<result.canonical_ms<<','<<result.binding_ms<<','<<result.number_ms<<','<<result.total_ms<<','<<startup<<','<<result.peak<<','<<sb02::peak_requests.load()<<",passed\n";
        {sb02::phase p(sb02::release_result);result=measured{};std::vector<sb01::stream>{}.swap(input);w.reset();pool.reset();}
        write_allocations(dir);
        std::cout<<"mode="<<mode<<",workers="<<workers<<",validation=passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
