#include "../semantic-bind-02/allocation_audit.hpp"
#ifdef SB04_LEGACY
#define SB03_MODEL_HEADER "../semantic-bind-03/model.hpp"
#else
#define SB03_MODEL_HEADER "../semantic-bind-04/model.hpp"
#endif
#define main unused_sb03_consumer_cli
#include "../semantic-bind-01/main.cpp"
#undef main
#include <random>
#include <sstream>

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
double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
}

namespace {
std::size_t retained_bytes(const measured& m){
    auto bytes=m.binding.types.record_bytes()+m.binding.types.copied_modifier_bytes()+m.binding.failures.capacity()*sizeof(issue);
#ifdef SB04_LEGACY
    if(m.binding.types.shares_shapes()){
        bytes+=m.data.requests.capacity()*sizeof(request)+m.data.requests.metadata_bytes();
        for(const auto& r:m.data.requests)bytes+=r.type.layers.capacity()*sizeof(modifier);
    }
#else
    bytes+=m.binding.types.shape_bytes();
#endif
    return bytes;
}
void destinations(const measured& m){
    (void)m;
#ifndef SB04_LEGACY
    for(std::size_t i=0;i<m.binding.types.size();++i){const auto& r=m.data.requests[i];const auto& t=m.binding.types.destination(i);
        check(t.owner==r.owner&&t.definition==r.definition&&t.member==r.incomplete_source&&t.base==r.base,"destination parity");}
#endif
}
void lifetime_check(const std::weak_ptr<const request_storage::arena>& weak,const bound_storage& types){
#ifdef SB04_LEGACY
    check(weak.expired()!=types.shares_shapes(),"legacy request lifetime");
#else
    (void)types;check(weak.expired(),"result retains Parser requests");
#endif
}
void lifetime_test(){
    binding_result kept;std::weak_ptr<const request_storage::arena> requests;
#ifndef SB04_LEGACY
    std::weak_ptr<const shape_arena> shapes;
#endif
    {std::vector<sb01::stream> input{lex("struct A { const int (*x)[2][3]; int &y; int * const z; };" )};ca01::pool workers(2);auto m=run(input,workers,false);
        check(m.binding.first.kind==error_kind::none,"lifetime fixture rejected");destinations(m);requests=m.data.requests.owner();
#ifndef SB04_LEGACY
        shapes=m.data.requests.owner()->shape_owners.at(0);
#endif
        kept=std::move(m.binding);}
    lifetime_check(requests,kept.types);
#ifndef SB04_LEGACY
    check(!shapes.expired(),"shape arena released before consumer");
#endif
    auto copy=kept;kept={};
    std::vector<std::size_t> order{0,1,2};auto h=consume(copy.types,order,false);
    std::vector<std::string> churn(1000,std::string(1024,'x'));
    check(h==consume(copy.types,order,false),"shape lifetime changed after move/copy/churn");
    copy={};check(requests.expired(),"last consumer retained requests");
#ifndef SB04_LEGACY
    check(shapes.expired(),"shape arena survives last consumer");
#endif
#ifndef SB04_LEGACY
    shape_arena arena;auto range=arena.append(std::array<modifier,1>{{{D::pointer,0}}});
    for(unsigned i=0;i<10000;++i)arena.append(std::array<modifier,1>{{{D::bounded_array,i+1}}});
    check(range.begin()->kind==D::pointer,"range broken by arena relocation");arena.frozen=true;bool rejected=false;
    try{arena.append({});}catch(const std::exception&){rejected=true;}check(rejected,"frozen shape arena mutated");
#endif
}
}
int main(int argc,char** argv){try{
#ifdef SB02_ALLOCATIONS
    if(argc==4&&std::string_view(argv[1])=="--audit"){
        selected_variant=variant::combined;world w;input_times prep;auto input=load(w,fs::path(argv[2]),prep);ca01::pool workers(std::stoul(argv[3]));
        measured m;{sb02::phase phase(sb02::parse);m=perform(input,workers,false);}
        const auto before=sb02::total.live.load();std::weak_ptr<const request_storage::arena> weak=m.data.requests.owner();m.data=batch{};weak.reset();
        check(sb02::live_requests.load()==0,"live Parser request at consumer boundary");
        const auto after=sb02::total.live.load();m=measured{};check(sb02::total.live.load()==0,"tracked model allocation survived final consumer");
        std::cout<<"allocation_calls="<<sb02::total.calls<<",allocation_bytes="<<sb02::total.bytes<<",peak_bytes="<<sb02::total.peak<<",model_live_bytes="<<before<<",consumer_live_bytes="<<after<<",requests_at_consumer=0,final_live_bytes="<<sb02::total.live<<",validation=passed\n";return 0;
    }
#endif
    if(argc==2&&std::string_view(argv[1])=="--self-test"){selected_variant=variant::combined;self_test();lifetime_test();return 0;}
    if(argc<3||argc>4){std::cerr<<"Usage: SemanticBind04 <project.json> <combined|baseline> [workers]\n";return 2;}
    selected_variant=parse_variant(argv[2]);
#ifndef SB04_LEGACY
    check(selected_variant==variant::combined,"arena experiment supports combined policy only");
#endif
    auto w=std::make_unique<world>();input_times prep;auto input=load(*w,fs::path(argv[1]),prep);ca01::pool workers(argc==4?std::stoul(argv[3]):16);
    std::size_t bytes=0,primary=0;std::uint64_t expected_linear=0,expected_shuffled=0;std::vector<std::size_t> order;bool accepted;
    {auto validation=run(input,workers,false);semantic_world o;const auto expected=oracle(*w,o);accepted=expected.accepted;
        if(accepted)verify_positive(validation,o);else verify_negative(validation,expected);
        destinations(validation);bytes=retained_bytes(validation);primary=validation.model_bytes;order.resize(validation.binding.types.size());std::iota(order.begin(),order.end(),0);std::mt19937 rng(703);std::shuffle(order.begin(),order.end(),rng);
        expected_linear=consume(validation.binding.types,order,false);expected_shuffled=consume(validation.binding.types,order,true);}
    // Contiguous, warm-input lifecycle: Parser through one sequential consumer
    // and destruction of the entire experimental result. Oracle is outside.
    auto start=clock_type::now();auto m=perform(input,workers,false);
    const auto model_ms=m.total_ms,parse_ms=m.parse_ms,combine_ms=m.combine_ms,bind_ms=m.binding_ms;
    const auto n=m.binding.types.size();
    std::weak_ptr<const request_storage::arena> weak=m.data.requests.owner();
    auto stage=clock_type::now();m.data=batch{};const auto release_ms=elapsed_ms(stage);lifetime_check(weak,m.binding.types);
    stage=clock_type::now();const auto linear=consume(m.binding.types,order,false);const auto first_consume_ms=elapsed_ms(stage);
    stage=clock_type::now();m=measured{};const auto result_release_ms=elapsed_ms(stage);const auto lifecycle_ms=elapsed_ms(start);
    check(weak.expired(),"requests survived result destruction");check(linear==expected_linear,"lifecycle checksum mismatch");
    // Independent result-only access trial: input and world destroyed first.
    m=perform(input,workers,false);weak=m.data.requests.owner();m.data=batch{};lifetime_check(weak,m.binding.types);
    std::vector<sb01::stream>{}.swap(input);w.reset();
    check(consume(m.binding.types,order,false)==expected_linear,"result-only linear checksum mismatch");
    check(consume(m.binding.types,order,true)==expected_shuffled,"result-only shuffled checksum mismatch");
    std::vector<double> sequential,shuffled;
    for(unsigned trial=0;trial<5;++trial)for(unsigned task=0;task<2;++task){bool random=((trial+task)&1)!=0;stage=clock_type::now();auto hash=consume(m.binding.types,order,random);auto ms=elapsed_ms(stage);
        check(hash==(random?expected_shuffled:expected_linear),"consumer checksum changed");(random?shuffled:sequential).push_back(ms);}
    auto copy=m.binding;m.binding={};check(consume(copy.types,order,false)==expected_linear,"copy lost shape lifetime");copy={};check(weak.expired(),"last consumer retained requests");
    std::cout<<std::fixed<<std::setprecision(6)<<"variant="<<variant_name(selected_variant)<<",workers="<<workers.size<<",requests="<<n<<",oracle_accepted="<<int(accepted)
        <<",request_size="<<sizeof(request)<<",model_ms="<<model_ms<<",parse_ms="<<parse_ms<<",combine_ms="<<combine_ms<<",binding_ms="<<bind_ms<<",primary_bytes="<<primary
        <<",request_release_ms="<<release_ms<<",first_consume_ms="<<first_consume_ms<<",result_release_ms="<<result_release_ms<<",lifecycle_ms="<<lifecycle_ms
        <<",sequential_ms="<<median(sequential)<<",shuffled_ms="<<median(shuffled)<<",result_only_capacity_bytes="<<bytes<<",linear_checksum="<<expected_linear<<",shuffled_checksum="<<expected_shuffled<<",validation=passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
