#include "harness.hpp"
#include "trace.hpp"
#include "server_engine/project/source/source_map.hpp"
#include "reuse.hpp"

namespace {
template<class T>void same_bytes(std::span<const T> a,std::span<const T> b){
    const auto x=std::as_bytes(a),y=std::as_bytes(b);check(x.size()==y.size()&&std::equal(x.begin(),x.end(),y.begin()),"trace changed serialized domain bytes");
}
void same_world(const semantic_world& a,const semantic_world& b){
    check(a.strings.size()==b.strings.size()&&a.identities.size()==b.identities.size(),"trace changed tables");
    for(unsigned i=1;i<=a.strings.size();++i)check(a.strings.spelling(i)==b.strings.spelling(i),"trace changed string IDs");
    for(unsigned i=1;i<=a.identities.size();++i){auto x=a.identities.at_slot(i),y=b.identities.at_slot(i);identity_record xr,yr;check(x==y&&a.identities.record(x,xr)&&b.identities.record(y,yr)&&xr.name==yr.name&&xr.parent==yr.parent,"trace changed WHO");}
    same_bytes(a.G.type_entries(),b.G.type_entries());same_bytes(a.G.type_identity_entries(),b.G.type_identity_entries());
    same_bytes(a.G.member_entries(),b.G.member_entries());same_bytes(a.G.base_entries(),b.G.base_entries());
    same_bytes(a.G.member_construction_entries(),b.G.member_construction_entries());same_bytes(a.G.derived_type_entries(),b.G.derived_type_entries());
    same_bytes(a.G.constructor_defaults.entries(),b.G.constructor_defaults.entries());
}
oracle_result traced_oracle(world& w,semantic_world& out,bool tracing,std::vector<std::byte>& snapshot){
    oracle_result result; source_map provenance;
    preprocessor_v2_failure pf;semantic_preprocessor_v2 pp{w.files,w.closure.view(),w.config,out.strings,&pf};
    cw::server::header_v2_semantic_reuse_cache cache{provenance,pp};
    check(succeeded(cache.reset(w.files.size())),"reuse reset");
    pp.configure_reuse(&cache,&cw::server::header_v2_semantic_reuse_cache::dispatch);
    pd01::side="production";pd01::active=tracing;
    for(auto r:w.roots){pd01::root=r.value();check(succeeded(provenance.begin_root(r)),"begin root");
        auto status=pp.start(r);
        if(succeeded(status)){header_parser_v2 parser{pp,out.identities,out.G,&result.failure};parser.set_provenance(&provenance);status=parser.parse();}
        if(!succeeded(status)){result.accepted=false;if(!result.failure.file)result.failure={parser_v2_failure_kind::syntax,pf.file,pf.source,pf.detail};break;}
        check(pp.finished(),"oracle stream incomplete");check(succeeded(provenance.end_root()),"end root");
    }
    if(result.accepted)check(succeeded(provenance.finalize(w.files.size(),out.identities,out.G)),"source finalize");
    auto save=[&](auto values){auto bytes=std::as_bytes(values);const auto size=bytes.size();auto size_bytes=std::as_bytes(std::span{&size,1});snapshot.insert(snapshot.end(),size_bytes.begin(),size_bytes.end());snapshot.insert(snapshot.end(),bytes.begin(),bytes.end());};
    save(provenance.root_entries());save(provenance.file_entries());save(provenance.contribution_entries());
    save(provenance.root_dependency_entries());save(provenance.dependency_entries());
    save(provenance.file_index_entries());save(provenance.dependency_index_entries());save(provenance.dependent_root_entries());
    save(provenance.root_initialization_entries());save(provenance.initialization_target_entries());
    save(provenance.type_presence_entries());save(provenance.object_presence_entries());save(provenance.link_presence_entries());
    if(tracing){pd01::root=0;
        for(unsigned i=0;i<out.G.type_entries().size();++i){const auto& type=out.G.type_entries()[i];pd01::emit(pd01::semantic,"final_record_metadata",out.G.type_identity_entries()[i].value(),static_cast<unsigned>(type.record_kind),type.flags);}
        for(unsigned i=0;i<out.G.member_entries().size();++i){const auto& member=out.G.member_entries()[i];pd01::emit(pd01::semantic,"final_member_metadata",member.name.value(),member.type.value(),static_cast<unsigned>(member.access));}
        for(const auto& base:out.G.base_entries())pd01::emit(pd01::semantic,"final_base_metadata",base.type.value(),static_cast<unsigned>(base.access),base.flags);
    }
    pd01::active=false;return result;
}
}
int main(int argc,char** argv){try{
    if(argc!=5){std::cerr<<"Usage: PublishDifferentialTrace01 <project> <output-dir> <workers> <target>\n";return 2;}
    fs::path folder=argv[2];fs::create_directories(folder);pd01::open(folder.string());
    const auto n=std::stoul(argv[3]),target=std::stoul(argv[4]);check(n>=1&&n<=16&&target>0,"configuration");
    selected_variant=variant::combined;ep01::enabled=true;ep01::workers=static_cast<unsigned>(n);
    world w;input_times times;auto input=load_numbers(w,fs::path(argv[1]),times);
    std::vector<std::byte> observed_provenance,control_provenance;
    semantic_world o;auto expected=traced_oracle(w,o,true,observed_provenance);
    semantic_world control;auto plain=traced_oracle(w,control,false,control_provenance);
    check(expected.accepted==plain.accepted,"trace changed acceptance");
    check(expected.failure.kind==plain.failure.kind&&expected.failure.file==plain.failure.file&&expected.failure.source.offset==plain.failure.source.offset&&expected.failure.source.length==plain.failure.source.length&&expected.failure.detail==plain.failure.detail,"trace changed diagnostic");
    same_world(o,control);
    check(observed_provenance==control_provenance,"trace changed SourceMap/dependencies");
    ca01::pool pool(16);hc02::timings timing;auto m=chunked(input,pool,target,false,timing);
    hc02::timings reverse_timing;auto reversed=chunked(input,pool,target,true,reverse_timing);exact(m,reversed);
    for(unsigned order:{1u,2u}){ep01::task_order=order;hc02::timings t;auto shuffled=chunked(input,pool,target,false,t);exact(m,shuffled);}ep01::task_order=0;
    const bool accepted=m.binding.first.kind==error_kind::none;
    std::string comparison="not_compared",detail;
    if(expected.accepted&&accepted){try{verify_positive(m,o);verify_defaults(m,o);verify_nested(m,o);comparison=m.binding.gap_definitions||m.data.excluded_callables?"partial_projection":"projection_parity";}catch(const std::exception& e){comparison="value_mismatch";detail=e.what();}}
    else if(expected.accepted!=accepted)comparison="acceptance_mismatch";
    else {const auto& a=m.binding.first;const auto& e=expected.failure;
        comparison=a.diagnostic_kind==static_cast<id>(e.kind)&&a.detail==e.detail&&a.source.file==e.file.value()&&a.source.offset==e.source.offset&&a.source.length==e.source.length?"exact_diagnostic":"diagnostic_mismatch";}
    pd01::side="projection";pd01::active=true;
    for(const auto& d:m.data.declarations){pd01::root=w.roots.at(static_cast<unsigned>((d.begin>>32)-1)).value();pd01::emit(pd01::semantic,"declaration",d.begin,d.symbol,static_cast<unsigned>(d.kind),m.data.identities.symbols[d.symbol].name);}
    m.data.requests.range(0,m.data.requests.size(),[&](auto i,const request& r){pd01::root=w.roots.at(static_cast<unsigned>((r.at>>32)-1)).value();pd01::emit(pd01::semantic,r.base?"base_request":"member_request",r.at,r.owner,i,r.member);pd01::emit(pd01::semantic,"request_origin",r.source.file,r.source.offset,r.source.length,r.member);});
    for(const auto& op:m.data.operations){pd01::root=w.roots.at(static_cast<unsigned>((op.at>>32)-1)).value();pd01::emit(pd01::semantic,"operation",op.at,static_cast<unsigned>(op.kind),static_cast<unsigned>(op.gap),op.target);}
    pd01::emit(pd01::strings,"not_emitted",0,0,0,"projection has spelling refs; no production string_id numbering");
    pd01::emit(pd01::types,"not_emitted",0,0,0,"projection has immutable shapes; no graph-local derived IDs");
    pd01::emit(pd01::provenance,"not_emitted",0,0,0,"physical origins do not implement SourceMap/reuse");
    pd01::side="input_preparation";pd01::root=0;
    for(unsigned i=1;i<=w.strings.size();++i)pd01::emit(pd01::strings,"prepared_table_snapshot",i,0,0,w.strings.spelling(i));
    pd01::side="projection";
    for(unsigned i=0;i<m.who.size();++i)pd01::emit(pd01::semantic,"numbered_who",i,m.who[i],m.data.identities.symbols[i].event,m.data.identities.symbols[i].name);
    pd01::active=false;pd01::flush();
    std::ofstream diagnostics(folder/"diagnostics.csv");diagnostics<<"side,accepted,kind,file,offset,length,detail\n";
    auto diagnostic=[&](std::string_view side,bool ok,unsigned kind,unsigned file,unsigned offset,unsigned length,std::string_view message){diagnostics<<side<<','<<ok<<','<<kind<<','<<file<<','<<offset<<','<<length<<','<<pd01::quote(message)<<'\n';};
    diagnostic("production",expected.accepted,static_cast<unsigned>(expected.failure.kind),expected.failure.file.value(),expected.failure.source.offset,expected.failure.source.length,expected.failure.detail);
    const auto& issue=m.binding.first;diagnostic("projection",accepted,issue.diagnostic_kind,issue.source.file,issue.source.offset,issue.source.length,issue.detail);
    diagnostics.flush();check(bool(diagnostics),"diagnostics write failed");
    std::cout<<"comparison="<<comparison<<",production_accepted="<<expected.accepted<<",projection_accepted="<<accepted<<",gaps="<<m.binding.gap_definitions<<",excluded_callables="<<m.data.excluded_callables<<",excluded_initializers="<<m.data.excluded_initializers<<",trace_observer_check=passed,schedule_check=passed\n";
    if(!detail.empty())std::cerr<<detail<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
