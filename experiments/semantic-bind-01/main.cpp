#ifdef SB03_MODEL_HEADER
#include SB03_MODEL_HEADER
#else
#include "model.hpp"
#endif
#include "token_reuse.hpp"
#include "../../server_engine/project/project_configuration_manifest.hpp"
#include "../../server_engine/project/parser/header_parser_v2.hpp"
#include "../../server_engine/project/frontend/lexer.hpp"
#include "../../server_engine/diagnostics/diagnostic_collection.hpp"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <unordered_set>
#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#endif

namespace {
using namespace sb01;
using namespace cw::server;
namespace fs=std::filesystem;
using clock_type=std::chrono::steady_clock;
double elapsed_ms(clock_type::time_point start){return std::chrono::duration<double,std::milli>(clock_type::now()-start).count();}
struct world {
    file_context files;project_configuration_manifest manifest;preprocessor_configuration config;
    diagnostic_collection diagnostics;std::vector<file_id> roots;string_table strings;prepared_include_closure_v2 closure;
};
struct semantic_world {string_table strings;identity_space identities{strings};graph G;};
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::size_t peak_ws(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS m{};if(GetProcessMemoryInfo(GetCurrentProcess(),&m,sizeof m))return m.PeakWorkingSetSize;
#endif
    return 0;
}
struct input_times {double compose=0,physical=0,effective=0,adapt=0;std::size_t tokens=0,reused=0;};
std::vector<sb01::stream> load(world& w,const fs::path& path,input_times& times){
    auto t=clock_type::now();check(succeeded(compose_project_configuration(path,operation_id{1},w.diagnostics,w.manifest,w.files,w.config)),"compose failed");
    for(id i=1;i<=w.files.size();++i)if(w.files.kind(file_id{i})==file_kind::header)w.roots.push_back(file_id{i});
    check(!w.roots.empty(),"no Header roots");times.compose=elapsed_ms(t);t=clock_type::now();prepared_include_failure_v2 failure;
    check(succeeded(prepare_header_include_closure_v2(w.files,w.roots,w.config,w.closure,&failure)),"physical preparation failed");times.physical=elapsed_ms(t);
    std::vector<sb01::stream> output(w.roots.size());std::vector<std::vector<string_id>> names(w.roots.size());
    preprocessor_v2_failure pf;semantic_preprocessor_v2 pp{w.files,w.closure.view(),w.config,w.strings,&pf};
    token_reuse reuse(pp,w.files.size());pp.configure_reuse(&reuse,&token_reuse::dispatch);t=clock_type::now();
    for(std::size_t r=0;r<w.roots.size();++r){check(succeeded(pp.start(w.roots[r])),"effective preprocessing start failed");
        while(const auto* token=pp.current()){
            output[r].tokens.push_back({token->kind,{},token->number.kind==prepared_number_kind_v2::unsigned_integer?token->number.bits:0,{token->file.value(),token->source_offset,token->source_length}});
            names[r].push_back(token->identifier);++times.tokens;check(succeeded(pp.advance()),"effective preprocessing advance failed");
        }check(pp.finished(),"incomplete effective stream");
    }
    times.effective=elapsed_ms(t);times.reused=reuse.reused;t=clock_type::now();
    // Resolve spelling views only after all interning is finished: string_table
    // storage may grow during preprocessing. No legacy IDs cross this boundary.
    for(std::size_t r=0;r<output.size();++r)for(std::size_t i=0;i<output[r].tokens.size();++i)output[r].tokens[i].name=w.strings.get(names[r][i]);
    times.adapt=elapsed_ms(t);return output;
}
struct measured {
    batch data; std::vector<id> representatives,who;binding_result binding;
    double parse_ms=0,combine_ms=0,canonical_ms=0,binding_ms=0,number_ms=0,total_ms=0;
    std::size_t model_bytes=0,peak=0;
};
measured run(const std::vector<sb01::stream>& input,ca01::pool& pool,bool reverse){
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
    out.total_ms=elapsed_ms(start);out.peak=peak_ws();
#ifdef SB03_MODEL_HEADER
    out.model_bytes=primary_bytes(out.data,out.binding);
#else
    out.model_bytes=out.data.identities.symbols.capacity()*sizeof(ca01::symbol)+out.data.declarations.capacity()*sizeof(declaration)+out.data.requests.capacity()*sizeof(request)+out.binding.types.capacity()*sizeof(bound_type)+out.binding.failures.capacity()*sizeof(issue);
    for(const auto& r:out.data.requests)out.model_bytes+=r.type.layers.capacity()*sizeof(modifier);
    for(const auto& t2:out.binding.types)out.model_bytes+=t2.layers.capacity()*sizeof(modifier);
#endif
    return out;
}
struct oracle_result {bool accepted=true;parser_v2_failure failure;};
oracle_result oracle(world& w,semantic_world& output){
    oracle_result result;
    preprocessor_v2_failure pf;semantic_preprocessor_v2 pp{w.files,w.closure.view(),w.config,output.strings,&pf};
    for(const auto root:w.roots){
        check(succeeded(pp.start(root)),"oracle preprocessing failed");header_parser_v2 parser{pp,output.identities,output.G,&result.failure};
        if(!succeeded(parser.parse())){result.accepted=false;break;}check(pp.finished(),"oracle input incomplete");
    }return result;
}
std::vector<identity_ref> oracle_symbols(const measured& m,const semantic_world& o){
    std::vector<identity_ref> map(m.data.identities.symbols.size());
    for(const auto& level:m.data.identities.levels)for(auto i:level){const auto& s=m.data.identities.symbols[i];
        if(s.parent==none){map[i]=o.identities.root();continue;}
        const auto name=o.strings.find(s.name);map[i]=o.identities.find(map[s.parent],name,static_cast<identity_kind>(s.kind));
        check(bool(map[i]),"declared local symbol missing from V2");
    }return map;
}
bound_type normalize(type_ref type,const graph& g,const std::unordered_map<std::uint32_t,id>& reverse){
    bound_type out;derived_type_record d;std::vector<modifier> layers;
    while(g.derived(type,d)){layers.push_back({d.kind,d.payload});type=d.child;}
    if(type.kind()==type_ref_kind::intrinsic)out.builtin=static_cast<I>(type.payload());
    else if(type.kind()==type_ref_kind::named){const auto who=(2u<<30)|type.payload();const auto found=reverse.find(who);check(found!=reverse.end(),"oracle type outside local symbol projection");out.entity=found->second;}
    else check(false,"invalid oracle type");
    out.layers.assign(layers.rbegin(),layers.rend());return out;
}
void verify_positive(const measured& m,const semantic_world& o){
    if(m.binding.first.kind!=error_kind::none)throw std::runtime_error("local Parser/Binder rejected positive corpus: kind="+std::to_string(static_cast<int>(m.binding.first.kind))+" file="+std::to_string(m.binding.first.source.file)+" offset="+std::to_string(m.binding.first.source.offset));
    const auto map=oracle_symbols(m,o);std::unordered_map<std::uint32_t,id> reverse;
    for(id i=0;i<map.size();++i){reverse.emplace(map[i].value(),m.representatives[i]);check(map[i].value()==m.who[i],"identity numbering differs from V2");}
    for(std::size_t i=0;i<m.data.requests.size();++i){const auto& r=m.data.requests[i];const auto owner=o.G.find_type(map[r.owner]);check(bool(owner),"oracle owner has no G type");
        if(r.base){const auto target=map[m.binding.types[i].entity];bool found=false;for(const auto& base:o.G.bases(owner))found|=base.type==target;check(found,"base binding differs from V2");}
        else {const auto member=o.G.find_member(owner,o.strings.find(r.member));const auto* entry=o.G.member(owner,member);check(entry!=nullptr,"local field missing from V2");check(normalize(entry->type,o.G,reverse)==m.binding.types[i],"normalized field type differs from V2");}
    }
    // Count unique complete record projections, so repeated includes don't hide
    // missing fields behind per-request successful comparisons.
    std::unordered_set<id> definitions;std::size_t fields=0;
    std::vector<std::size_t> field_counts(m.data.declarations.size());
    for(const auto& r:m.data.requests)if(!r.base)++field_counts[r.definition];
    for(id di=0;di<m.data.declarations.size();++di){const auto& d=m.data.declarations[di];if(d.kind!=declaration_kind::record||d.complete==UINT64_MAX||!definitions.insert(m.representatives[d.symbol]).second)continue;
        const auto owner=o.G.find_type(map[d.symbol]);const auto count=field_counts[di];
        check(count==o.G.members(owner).size(),"record field coverage differs from V2");fields+=count;
    }
    check(fields==o.G.member_count(),"Header field inventory differs from V2");
}
void same_result(const measured& a,const measured& b){
    check(a.who==b.who,"worker scheduling changed WHO");check(a.binding.first.kind==b.binding.first.kind&&a.binding.first.at==b.binding.first.at&&a.binding.first.source==b.binding.first.source,"worker scheduling changed first diagnostic");
    check(a.binding.types.size()==b.binding.types.size(),"worker scheduling changed requests");
    // Representative indices are deterministic because local streams are
    // assembled by manifest root order, never worker completion order.
    check(a.binding.types==b.binding.types,"worker scheduling changed binding");
}
void verify_negative(const measured& m,const oracle_result& expected){
    const auto k=m.binding.first.kind;
    check(k!=error_kind::none,"candidate accepted V2 rejection");
    check(k!=error_kind::unsupported,"unsupported candidate cannot prove negative parity");
    const auto actual=m.binding.first.source;
    check(actual.file==expected.failure.file.value()&&actual.offset==expected.failure.source.offset&&actual.length==expected.failure.source.length,"first diagnostic location differs from V2");
    const auto detail=expected.failure.detail;
    const bool category=k==error_kind::unknown_type?detail.find("named type is unknown")!=std::string_view::npos:
        k==error_kind::incomplete_type?(detail.find("incomplete type")!=std::string_view::npos||detail.find("Base class must name")!=std::string_view::npos):
        k==error_kind::record_family?(detail.find("record key conflicts")!=std::string_view::npos||detail.find("record definition conflicts")!=std::string_view::npos):
        k==error_kind::alias_conflict?(detail.find("Typedef conflicts")!=std::string_view::npos||detail.find("record definition conflicts")!=std::string_view::npos):
        k==error_kind::definition_conflict?detail.find("record definition conflicts")!=std::string_view::npos:
        false;
    check(category,"first diagnostic category differs from V2");
}
sb01::stream lex(std::string_view source){
    lexical_stream words;std::vector<header_lexical_token> ready;lexical_error e;
    check(succeeded(lexer::tokenize(file_id{1},source,words,&e)),"fixture lexer failed");sb01::stream out;
    const auto compact=words.words();std::uint32_t offset=0;
    for(std::size_t i=0;i<compact.size();){const auto t=lexical_token::from_value(compact[i++]);auto delta=t.delta(),length=t.length();
        if(delta==lexical_token::extended_delta){check(i<compact.size(),"bad lexer delta");delta=compact[i++];}
        if(length==lexical_token::extended_length){check(i<compact.size(),"bad lexer length");length=compact[i++];}
        offset+=delta;ready.push_back({offset,length,t.kind()});}
    check(!source.empty()&&!ready.empty(),"empty test token stream");
    for(auto t:ready){std::uint64_t n=0;auto spelling=source.substr(t.source_offset,t.source_length);if(t.kind==K::pp_number)n=std::stoull(std::string(spelling));
        out.tokens.push_back({t.kind,t.kind==K::identifier?spelling:std::string_view{},n,{1,t.source_offset,t.source_length}});}
    return out;
}
void self_test(){
    struct test {std::vector<std::string> roots;error_kind expected;};
    const std::vector<test> cases={
        {{"struct A; struct B { A* value; }; struct A { int x; };"},error_kind::none},
        {{"struct A; struct B { A value; }; struct A { int x; };"},error_kind::incomplete_type},
        {{"struct B { A* value; }; struct A {};"},error_kind::unknown_type},
        {{"struct A {};","struct B { A value; };"},error_kind::none},
        {{"struct B { A* value; };","struct A {};"},error_kind::unknown_type},
        {{"namespace N { struct A; }","namespace N { struct A {}; struct B { A value; }; }"},error_kind::none},
        {{"struct A {}; namespace N { struct B { A value; }; struct A; }"},error_kind::none},
        {{"struct A {}; namespace N { struct A; struct B { A value; }; }"},error_kind::incomplete_type},
        {{"struct A { A* next; };"},error_kind::none},
        {{"struct A { A value; };"},error_kind::incomplete_type},
        {{"struct A; struct B : A {}; struct A {};"},error_kind::incomplete_type},
        {{"struct A {}; struct B : public A { int x; };"},error_kind::none},
        {{"union A {}; struct B : A {};"},error_kind::incomplete_type},
        {{"struct A; union A;"},error_kind::record_family},
        {{"struct A; class A {};"},error_kind::none},
        {{"typedef int X; struct A { X x; };"},error_kind::none},
        {{"typedef int X; typedef bool X;"},error_kind::alias_conflict},
        {{"typedef int X; struct X {};"},error_kind::alias_conflict},
        {{"struct X; typedef int X; struct B { X value; };"},error_kind::none},
        {{"typedef int X; struct X; struct B { X value; };"},error_kind::none},
        {{"struct X; struct B { X* value; }; typedef int X;"},error_kind::none},
        {{"struct A { int x; };","struct A { bool x; };"},error_kind::definition_conflict},
        {{"struct A { int x; };","struct A { int x; };"},error_kind::none},
        {{"struct A { const int* x[2]; volatile short & y; };"},error_kind::none},
        {{"namespace N { struct A {}; } namespace N { struct B { A value; }; }"},error_kind::none},
        {{"namespace N { struct A {}; } struct B { A* value; };"},error_kind::unknown_type},
        {{"struct A { int (*p)[3]; };"},error_kind::none},
    };
    std::size_t checks=0;
    for(const auto& fixture:cases){std::vector<sb01::stream> tokens;for(const auto& root:fixture.roots)tokens.push_back(lex(root));
        ca01::pool serial(1);auto reference=run(tokens,serial,false);if(reference.binding.first.kind!=fixture.expected)throw std::runtime_error("fixture outcome mismatch: "+fixture.roots[0]+" actual="+std::to_string(static_cast<int>(reference.binding.first.kind))+" expected="+std::to_string(static_cast<int>(fixture.expected)));
        for(auto workers:{1,2,4,8,16})for(bool reverse:{false,true}){ca01::pool pool(workers);auto candidate=run(tokens,pool,reverse);same_result(reference,candidate);++checks;}
    }
    std::cout<<"model_checks="<<checks<<",validation=passed\n";
}
}
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string_view(argv[1])=="--self-test"){self_test();return 0;}
        if(argc<2||argc>4){std::cerr<<"Usage: SemanticBind01 <project.json> [workers=1] [repeats=1]\n";return 2;}
        const auto workers=argc>2?std::stoul(argv[2]):1;const auto repeats=argc>3?std::stoul(argv[3]):1;
        check(repeats>=1&&repeats<=100,"repeats must be 1..100");
        world w;input_times input_metrics;auto input=load(w,fs::path(argv[1]),input_metrics);
        auto t=clock_type::now();
        t=clock_type::now();ca01::pool pool(workers);const auto startup_ms=elapsed_ms(t);
        // Execute candidate before invoking independent semantic oracle.
        auto reference=run(input,pool,false);
        semantic_world o;const auto expected=oracle(w,o);
        if(expected.accepted)verify_positive(reference,o);
        else verify_negative(reference,expected);
        for(std::size_t repeat=0;repeat<repeats;++repeat){auto m=repeat?run(input,pool,(repeat&1)!=0):std::move(reference);
            if(expected.accepted)verify_positive(m,o);
            else verify_negative(m,expected);
            std::cout<<std::fixed<<std::setprecision(3)<<"workers="<<workers<<",repeat="<<repeat<<",roots="<<input.size()
                <<",compose_ms="<<input_metrics.compose<<",physical_ms="<<input_metrics.physical<<",effective_tokens="<<input_metrics.tokens<<",token_reuse_hits="<<input_metrics.reused<<",effective_preprocess_capture_ms="<<input_metrics.effective<<",adapt_ms="<<input_metrics.adapt<<",pool_start_ms="<<startup_ms
                <<",local_parse_ms="<<m.parse_ms<<",combine_prepare_ms="<<m.combine_ms<<",canonical_ms="<<m.canonical_ms<<",binding_ms="<<m.binding_ms<<",numbering_ms="<<m.number_ms<<",model_total_ms="<<m.total_ms
                <<",local_symbols="<<m.data.identities.symbols.size()<<",declarations="<<m.data.declarations.size()<<",binding_requests="<<m.data.requests.size()<<",compatibility_checks="<<m.binding.compatibility_checks
                <<",excluded_callables="<<m.data.excluded_callables<<",excluded_initializers="<<m.data.excluded_initializers<<",model_capacity_bytes="<<m.model_bytes<<",process_peak_ws_bytes="<<m.peak
                <<",oracle_accepted="<<expected.accepted<<",candidate_error="<<static_cast<int>(m.binding.first.kind)<<",candidate_error_file="<<m.binding.first.source.file<<",candidate_error_offset="<<m.binding.first.source.offset
                <<",oracle_error_file="<<expected.failure.file.value()<<",oracle_error_offset="<<expected.failure.source.offset
                <<",parity="<<(expected.accepted?"identity_and_field_type_projection":"rejection_and_first_location")<<",coverage="<<((m.data.excluded_callables||m.data.excluded_initializers)?"partial":"type_binding_subset")<<",validation=passed\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
