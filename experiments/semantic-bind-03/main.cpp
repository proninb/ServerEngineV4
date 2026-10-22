#include "../semantic-bind-02/allocation_audit.hpp"
#include <sstream>
#define SB03_MODEL_HEADER "../semantic-bind-03/model.hpp"
#define main sb03_unused_legacy_cli
#include "../semantic-bind-01/main.cpp"
#undef main

namespace {
struct sample {const char* point;std::uint64_t live_requests,peak_requests,live_bytes,peak_bytes;std::size_t local_records,batch_records,local_capacity,batch_capacity;};
void take(std::vector<sample>& samples,const char* point,const measured& m,const std::vector<local_output>& locals){
    std::size_t count=0,capacity=0;for(const auto& local:locals){count+=local.requests.size();capacity+=local.requests.capacity();}
    samples.push_back({point,sb02::live_requests.load(),sb02::peak_requests.load(),sb02::total.live.load(),sb02::total.peak.load(),count,m.data.requests.size(),capacity,m.data.requests.capacity()});
}
measured diagnostic(const std::vector<sb01::stream>& input,ca01::pool& workers,std::vector<sample>& samples){
    measured out;const auto start=clock_type::now();std::vector<local_output> locals;
    {sb02::phase stage(sb02::parse);locals.resize(input.size());}
    auto t=clock_type::now();{sb02::phase stage(sb02::parse);workers.run(input.size(),[&](std::size_t i,auto){sb02::root_scope root(static_cast<unsigned>(i));locals[i]=sb01::parser(input[i],static_cast<id>(i)).run();});}out.parse_ms=elapsed_ms(t);
    take(samples,"after_parse",out,locals);t=clock_type::now();{sb02::phase stage(sb02::combine);out.data=combine(locals);}out.combine_ms=elapsed_ms(t);
    take(samples,"after_combine",out,locals);{sb02::phase stage(sb02::release_locals);locals.clear();}take(samples,"after_worker_release",out,locals);
    ca01::memory memory(false);std::vector<ca01::comparisons> counters(workers.size);
    t=clock_type::now();auto canonical=[&]{sb02::phase stage(sb02::canonical);return ca01::canonicalize(out.data.identities,"index",workers,memory,counters,false);}();out.canonical_ms=elapsed_ms(t);
    t=clock_type::now();{sb02::phase stage(sb02::binding);out.binding=bind(out.data,canonical,workers);}out.binding_ms=elapsed_ms(t);take(samples,"after_binding",out,locals);
    t=clock_type::now();auto numbered=[&]{sb02::phase stage(sb02::numbering);return ca01::number(out.data.identities,canonical,memory);}();out.number_ms=elapsed_ms(t);
    {sb02::phase stage(sb02::export_result);out.representatives.assign(canonical.representative.begin(),canonical.representative.end());out.who.assign(numbered.who.begin(),numbered.who.end());}
    out.total_ms=elapsed_ms(start);out.peak=peak_ws();out.model_bytes=primary_bytes(out.data,out.binding);take(samples,"after_numbering",out,locals);return out;
}
void write_diagnostic(const fs::path& dir,const measured& m,const std::vector<sample>& samples){
    std::ofstream life(dir/"lifetimes.csv");life<<"point,live_requests,peak_requests,tracked_live_bytes,tracked_peak_bytes,local_records,batch_records,local_capacity,batch_capacity\n";
    for(const auto& s:samples)life<<s.point<<','<<s.live_requests<<','<<s.peak_requests<<','<<s.live_bytes<<','<<s.peak_bytes<<','<<s.local_records<<','<<s.batch_records<<','<<s.local_capacity<<','<<s.batch_capacity<<'\n';
    std::ofstream memory(dir/"memory.csv");memory<<"array,capacity_bytes\n";std::size_t modifiers=0;for(const auto& r:m.data.requests)modifiers+=r.type.layers.capacity()*sizeof(modifier);
    memory<<"requests,"<<m.data.requests.capacity()*sizeof(request)<<"\ninput_modifiers,"<<modifiers<<"\nbound_records,"<<m.binding.types.record_bytes()<<"\ncopied_bound_modifiers,"<<m.binding.types.copied_modifier_bytes()<<"\ndiagnostics,"<<m.binding.failures.capacity()*sizeof(issue)<<"\nsymbols,"<<m.data.identities.symbols.capacity()*sizeof(ca01::symbol)<<"\ndeclarations,"<<m.data.declarations.capacity()*sizeof(declaration)<<"\nprimary_total,"<<m.model_bytes<<"\nrequest_arena_metadata_excluding_shared_control_block,"<<m.data.requests.metadata_bytes()<<'\n';
}
void write_allocations(const fs::path& dir){
    std::ofstream out(dir/"allocations.csv");out<<"stage,calls,requested_bytes,live_bytes,peak_origin_live_bytes\n";
    for(unsigned i=1;i<sb02::stage_count;++i){const auto& c=sb02::stages[i];out<<sb02::stage_names[i]<<','<<c.calls.load()<<','<<c.bytes.load()<<','<<c.live.load()<<','<<c.peak.load()<<'\n';}
    out<<"total,"<<sb02::total.calls.load()<<','<<sb02::total.bytes.load()<<','<<sb02::total.live.load()<<','<<sb02::total.peak.load()<<'\n';
}
std::vector<bound_type> materialize(const bound_storage& values){
    std::vector<bound_type> out;for(std::size_t i=0;i<values.size();++i){const auto v=values[i];out.push_back({v.entity,v.builtin,{v.layers.begin(),v.layers.end()}});}return out;
}
binding_result retained_shapes(variant v){
    selected_variant=v;const std::string source="struct A { const int * p[3]; short (&r)[2]; };";
    auto input=lex(source);std::vector<local_output> locals;locals.push_back(sb01::parser(input,0).run());auto data=combine(locals);locals.clear();
    ca01::pool pool(2);ca01::memory memory(false);std::vector<ca01::comparisons> counters(2);auto canonical=ca01::canonicalize(data.identities,"index",pool,memory,counters,false);
    auto result=bind(data,canonical,pool);check(result.first.kind==error_kind::none,"lifetime fixture rejected");return result;
}
void extra_tests(){
    const auto expected=materialize(retained_shapes(variant::baseline).types);
    for(auto v:{variant::baseline,variant::sparse,variant::shared_shape,variant::segmented,variant::combined}){
        auto retained=retained_shapes(v);auto moved=std::move(retained);auto copy=moved;
        // Source, token stream, worker outputs, batch and canonicalizer from
        // retained_shapes() are already destroyed. Numeric shapes must survive.
        std::vector<std::vector<modifier>> churn(4000,std::vector<modifier>(3,{D::bounded_array,99}));
        check(materialize(moved.types)==expected&&materialize(copy.types)==expected,"shape owner did not survive destruction/move/copy");
        moved=binding_result{};check(materialize(copy.types)==expected,"last shape consumer lost its owner");
        selected_variant=v;
        for(const std::string source:{"struct A; struct B { A x, y; };","struct B { Missing x, y; };"}){
            auto tokens=lex(source);std::vector<sb01::stream> in;in.push_back(tokens);ca01::pool serial(1);auto a=run(in,serial,false);
            for(auto workers:{2,4,8,16}){ca01::pool pool(workers);auto b=run(in,pool,true);same_result(a,b);}
        }
    }
    // Range cursors must cross empty/nonempty root boundaries without gaps,
    // duplicates or per-element random-access assumptions.
    request_storage storage;storage.prepare(true,6,3);std::vector<request> empty;storage.append(empty);
    for(unsigned count:{1,3,2}){std::vector<request> part(count);for(auto& r:part)r.at=storage.size()+(&r-part.data());storage.append(part);}
    storage.freeze();for(std::size_t begin=0;begin<=6;++begin)for(std::size_t end=begin;end<=6;++end){std::size_t next=begin;storage.range(begin,end,[&](std::size_t i,const request& r){check(i==next++&&r.at==i,"segmented cursor differs from index order");});check(next==end,"segmented range lost records");}
    bool rejected=false;try{storage.append(empty);}catch(const std::runtime_error&){rejected=true;}check(rejected,"frozen request arena was mutable");
    std::cout<<"compact_lifetime_and_cursor_checks=passed\n";
}
}
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string_view(argv[1])=="--self-test"){
            for(auto v:{variant::baseline,variant::sparse,variant::shared_shape,variant::segmented,variant::combined}){selected_variant=v;self_test();}extra_tests();return 0;
        }
        if(argc<4||argc>5){std::cerr<<"Usage: SemanticBind03<Timing|Allocations> <project.json> <workers> <baseline|sparse|shared_shape|segmented|combined> [new-output-directory]\n";return 2;}
        selected_variant=parse_variant(argv[3]);const auto workers=std::stoul(argv[2]);fs::path dir;
        if(argc==5){dir=fs::absolute(argv[4]);check(!fs::exists(dir),"output directory must be new");fs::create_directories(dir);}
        auto w=std::make_unique<world>();input_times prep;std::vector<sb01::stream> input;
        {sb02::phase stage(sb02::input);input=load(*w,fs::path(argv[1]),prep);}check(input.size()<sb02::max_roots,"allocation root capacity exceeded");
        std::unique_ptr<ca01::pool> pool;{sb02::phase stage(sb02::pool_start);pool=std::make_unique<ca01::pool>(workers);}
        std::vector<sample> samples;samples.reserve(8);
#ifdef SB02_ALLOCATIONS
        auto m=diagnostic(input,*pool,samples);constexpr bool allocations=true;
#else
        auto m=run(input,*pool,false);constexpr bool allocations=false;
#endif
        bool accepted;origin oracle_origin;
        {semantic_world oracle_data;const auto expected=oracle(*w,oracle_data);accepted=expected.accepted;oracle_origin={expected.failure.file.value(),expected.failure.source.offset,expected.failure.source.length};if(accepted)verify_positive(m,oracle_data);else verify_negative(m,expected);}
        if(!dir.empty())write_diagnostic(dir,m,samples);
        std::ostringstream result;
        result<<std::fixed<<std::setprecision(6)<<"variant="<<variant_name(selected_variant)<<",allocations="<<allocations<<",workers="<<workers<<",roots="<<input.size()<<",effective_tokens="<<prep.tokens
            <<",local_parse_ms="<<m.parse_ms<<",combine_prepare_ms="<<m.combine_ms<<",canonical_ms="<<m.canonical_ms<<",binding_ms="<<m.binding_ms<<",numbering_ms="<<m.number_ms<<",model_total_ms="<<m.total_ms
            <<",local_symbols="<<m.data.identities.symbols.size()<<",declarations="<<m.data.declarations.size()<<",binding_requests="<<m.data.requests.size()<<",model_capacity_bytes="<<m.model_bytes<<",request_arena_metadata_bytes="<<m.data.requests.metadata_bytes()<<",process_peak_ws_bytes="<<m.peak
            <<",excluded_callables="<<m.data.excluded_callables<<",excluded_initializers="<<m.data.excluded_initializers<<",request_instance_peak="<<sb02::peak_requests.load()<<",diagnostic_slots="<<m.binding.failures.size()
            <<",oracle_accepted="<<accepted<<",candidate_error="<<static_cast<int>(m.binding.first.kind)<<",candidate_error_file="<<m.binding.first.source.file<<",candidate_error_offset="<<m.binding.first.source.offset<<",oracle_error_file="<<oracle_origin.file<<",oracle_error_offset="<<oracle_origin.offset
            <<",parity="<<(accepted?"identity_and_type_projection":"rejection_and_first_location")<<",coverage="<<((m.data.excluded_callables||m.data.excluded_initializers)?"partial":"type_binding_subset")<<",validation=passed\n";
        {sb02::phase stage(sb02::release_result);m=measured{};std::vector<sb01::stream>{}.swap(input);w.reset();pool.reset();}
        if(!dir.empty()){write_allocations(dir);std::ofstream(dir/"summary.txt")<<result.str();}
        std::cout<<result.str();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
