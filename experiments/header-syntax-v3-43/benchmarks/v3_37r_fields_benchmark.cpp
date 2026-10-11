// V3-37R vs frozen V3-37; common semantics and separate V2 oracle.
#include "v3_26_reference_for_v327.inc"
#define CW_V323_DIRECT_INDEX 1
#define CW_V324_DIRECT_GRAPH_WRITE 1
#define CW_V326_FAST_TYPE_RESOLVE 1
#define CW_V327_DERIVED_CACHE 1
#define CW_V331_SAMPLED_TYPE_PROFILE 1
#define CW_V331_SCOPE_CHAIN 1
#define v319 v337
#include "v3_37_graph_producer.inc"
#undef v319
#define v319 v337r
#include "v3_37r_fields_graph_producer.inc"
#undef v319
#undef CW_V331_SCOPE_CHAIN
#undef CW_V331_SAMPLED_TYPE_PROFILE
#undef CW_V327_DERIVED_CACHE
#undef CW_V326_FAST_TYPE_RESOLVE
#undef CW_V324_DIRECT_GRAPH_WRITE
#undef CW_V323_DIRECT_INDEX

namespace {
template<class State,class Comparison>
bool verified(const State& s,const semantic_world& o,const Comparison& c){
    return s.unsupported==0 && s.G.type_count()==o.G.type_count() &&
      s.G.member_count()==o.G.member_count() &&
      s.G.derived_type_count()==o.G.derived_type_count() &&
      s.G.constructor_defaults.entries().size()==o.G.constructor_defaults.entries().size() &&
      c.type_missing==0 && c.extra_types==0 && c.member_mismatch==0 &&
      c.construction_mismatch==0 && c.base_mismatch==0 &&
      c.polymorphic_mismatch==0 && c.nested_mismatch==0 &&
      c.identity_mismatch==0 && c.type_where_mismatch==0 &&
      c.member_where_mismatch==0 && c.derived_where_mismatch==0 &&
      c.derived_extra_v3==0 && c.derived_extra_v2==0 &&
      c.member_matches==o.G.member_count();
}
template<class State,class Compare>
void ensure_parity(const State& s,const semantic_world& oracle,
                   const string_table& strings, Compare fn,const char* stage){
    const auto c=fn(s,strings,oracle);
    if(!verified(s,oracle,c)){
       std::ostringstream ss;
       ss<<"HEADER_GRAPH_PARITY_FAILED stage="<<stage<<" unsupported="<<s.unsupported
         <<" types="<<s.G.type_count()<<'/'<<oracle.G.type_count()
         <<" members="<<s.G.member_count()<<'/'<<oracle.G.member_count()
         <<" identity="<<c.identity_mismatch
         <<" type_where="<<c.type_where_mismatch
         <<" member_where="<<c.member_where_mismatch
         <<" derived_where="<<c.derived_where_mismatch
         <<" construction="<<c.construction_mismatch
         <<" nested="<<c.nested_mismatch;
       throw std::runtime_error(ss.str());
    }
}
void run(const fs::path& project,const fs::path& pair_path,
    const fs::path& phase_path,const fs::path& type_path,
    const fs::path& summary_path,int warmups,int pairs,int stride){
    world w; double compose=0,prepare=0,decode=0,capture=0;
    std::uint64_t checksum=0;
    build_inputs(w,project,compose,prepare,decode,capture,checksum);
    semantic_world oracle;
    run_header_v2_semantic(w,oracle); // strictly outside all paired intervals
    std::ofstream pair_file,phase_file,type_file,summary_file;
    create_report(pair_path,pair_file);
    create_report(phase_path,phase_file);
    create_report(type_path,type_file);
    create_report(summary_path,summary_file);
    pair_file << "pair,phase,order,v337_ms,v337r_ms,saved_ms,speedup_pct,types,members,derived,header_graph_gate,full_publish_gate\n";
    pair_file << std::fixed << std::setprecision(6);
    std::vector<double> old_times,new_times,saved_times,speedups;
    std::size_t wins=0;
    for(int it=0;it<warmups+pairs;++it){
        const bool warm=it<warmups;
        const int i=warm?it:it-warmups;
        const bool old_first=i%2==0;
        v337::producer_state old_s{w.strings};
        v337r::producer_state new_s{w.strings};
        double old_ms=0,new_ms=0;
        const auto old_run=[&]{const auto t=clock_type::now();
            if(!v337::emit(old_s,w))throw std::runtime_error("V337 incomplete");
            old_ms=elapsed_ms(t);};
        const auto new_run=[&]{const auto t=clock_type::now();
            if(!v337r::emit(new_s,w))throw std::runtime_error("V337R incomplete");
            new_ms=elapsed_ms(t);};
        if(old_first){old_run();new_run();}else{new_run();old_run();}
        ensure_parity(old_s,oracle,w.strings,v337::compare,"V337");
        ensure_parity(new_s,oracle,w.strings,v337r::compare,"V337R");
        const double delta=old_ms-new_ms, pct=old_ms>0?100*delta/old_ms:0;
        pair_file<<i+1<<','<<(warm?"warmup":"measure")<<','
                 <<(old_first?"V337-V337R":"V337R-V337")<<','
                 <<old_ms<<','<<new_ms<<','<<delta<<','<<pct<<','
                 <<new_s.G.type_count()<<','<<new_s.G.member_count()<<','
                 <<new_s.G.derived_type_count()<<",HEADER_GRAPH_PARITY_PASS,PARTIAL\n";
        pair_file.flush();
        if(!warm){old_times.push_back(old_ms);new_times.push_back(new_ms);
            saved_times.push_back(delta);speedups.push_back(pct);wins+=delta>0;}
        std::cout<<(warm?"Warmup ":"Pair ")<<i+1
                 <<" V337="<<old_ms<<" V337R="<<new_ms<<" saved="<<delta
                 <<" HEADER_GRAPH_PARITY_PASS\n"<<std::flush;
    }
    // Profile is separate. No clocks are called inside ordinary, unsampled
    // field type resolution; sample selection is stable across runs.
    v337r::producer_state state{w.strings};
    v337r::phase_profile profile;
    profile.sample_stride=static_cast<std::uint64_t>(stride);
    state.profile=&profile;
    const auto began=clock_type::now();
    if(!v337r::emit(state,w))throw std::runtime_error("V337R sampled profile incomplete");
    const double total=elapsed_ms(began);
    ensure_parity(state,oracle,w.strings,v337r::compare,"profile");
    // Negative-gate invariant: any nonzero WHERE mismatch MUST be rejected.
    auto negative=v337r::compare(state,w.strings,oracle);
    ++negative.member_where_mismatch;
    if(verified(state,oracle,negative))
        throw std::runtime_error("V337R negative parity-gate self-check failed");
    if(profile.sampled_records==0 || profile.sampled_field_types==0 ||
       profile.sampled_records>profile.eligible_records ||
       profile.type_fast_hits+profile.type_fast_misses > profile.sampled_named_attempts ||
       profile.sampled_field_derived.calls !=
           profile.sampled_field_derived.hits+profile.sampled_field_derived.misses ||
       profile.sampled_field_derived.graph_created>state.G.derived_type_count() ||
       profile.sampled_named_details.cache_hits != profile.type_fast_hits ||
       profile.sampled_named_details.cache_misses != profile.type_fast_misses ||
       profile.sampled_named_details.name_attempts !=
           profile.sampled_named_details.cache_hits+profile.sampled_named_details.cache_misses ||
       profile.sampled_named_details.lookup_failures > profile.sampled_named_details.cache_misses ||
       profile.sampled_named_details.aliases+profile.sampled_named_details.ordinary_named+
           profile.sampled_named_details.lookup_failures != profile.sampled_named_details.cache_misses){
        throw std::runtime_error("Invalid V337R sampled profiler counters");
    }
    const double explained=profile.syntax_ms+profile.identity_ms+profile.index_ms+
        profile.fields_ms+profile.constructors_ms+profile.inheritance_ms+
        profile.graph_define_ms+profile.nested_ms;
    if(explained>total+0.5)throw std::runtime_error("V337R phase accounting invalid");
    phase_file<<"total_ms,syntax_ms,identity_ms,record_index_ms,fields_ms,constructors_ms,inheritance_ms,graph_commit_ms,nested_ms,unattributed_ms,header_graph_gate\n";
    phase_file<<total<<','<<profile.syntax_ms<<','<<profile.identity_ms<<','
        <<profile.index_ms<<','<<profile.fields_ms<<','<<profile.constructors_ms<<','
        <<profile.inheritance_ms<<','<<profile.graph_define_ms<<','
        <<profile.nested_ms<<','<<std::max(0.0,total-explained)
        <<",HEADER_GRAPH_PARITY_PASS\n";
    type_file<<"stride,eligible_records,sampled_records,sampled_field_types,sampled_builtin_resolved,sampled_named_attempts,sampled_named_cache_hits,sampled_named_cache_misses,sampled_fallback_attempts,base_ms_sampled,shape_ms_sampled,prepare_ms_sampled,derived_ms_sampled,builtin_ms_sampled,named_ms_sampled,fallback_ms_sampled,derived_calls_sampled,derived_hits_sampled,derived_misses_sampled,derived_created_sampled,scope_prepare_ms_sampled,name_parse_ms_sampled,name_cache_probe_ms_sampled,scope_lookup_ms_sampled,alias_classify_ms_sampled,scope_steps_sampled,aliases_sampled,plain_named_sampled,named_lookup_failures_sampled,header_graph_gate\n";
    type_file<<stride<<','<<profile.eligible_records<<','<<profile.sampled_records<<','
      <<profile.sampled_field_types<<','<<profile.sampled_builtin<<','
      <<profile.sampled_named_attempts<<','<<profile.type_fast_hits<<','
      <<profile.type_fast_misses<<','<<profile.sampled_fallback_attempts<<','
      <<profile.type_base_ms<<','<<profile.type_shape_ms<<','
      <<profile.sampled_prepare_ms<<','<<profile.type_derived_ms<<','
      <<profile.sampled_builtin_ms<<','<<profile.sampled_named_ms<<','
      <<profile.sampled_fallback_ms<<','<<profile.sampled_field_derived.calls<<','
      <<profile.sampled_field_derived.hits<<','<<profile.sampled_field_derived.misses<<','
      <<profile.sampled_field_derived.graph_created<<','
      <<profile.sampled_scope_prepare_ms<<','
      <<profile.sampled_named_details.parse_ms<<','
      <<profile.sampled_named_details.cache_probe_ms<<','
      <<profile.sampled_named_details.scope_lookup_ms<<','
      <<profile.sampled_named_details.alias_classify_ms<<','
      <<profile.sampled_named_details.scope_steps<<','
      <<profile.sampled_named_details.aliases<<','
      <<profile.sampled_named_details.ordinary_named<<','
      <<profile.sampled_named_details.lookup_failures
      <<",HEADER_GRAPH_PARITY_PASS\n";
    summary_file<<"warmups,pairs,effective_tokens,types,members,derived,median_v337_ms,median_v337r_ms,median_paired_saved_ms,median_paired_speedup_pct,paired_wins,sample_stride,header_graph_gate,full_publish_gate\n";
    summary_file<<warmups<<','<<pairs<<','<<observe_corpus(w).effective_tokens<<','
        <<state.G.type_count()<<','<<state.G.member_count()<<','<<state.G.derived_type_count()<<','
        <<v323_median(old_times)<<','<<v323_median(new_times)<<','
        <<v323_median(saved_times)<<','<<v323_median(speedups)<<','<<wins<<','<<stride
        <<",HEADER_GRAPH_PARITY_PASS,PARTIAL\n";
    if(!pair_file||!phase_file||!type_file||!summary_file)
        throw std::runtime_error("V337R CSV write failed");
    std::cout<<"V3-37R sampled type: stride="<<stride
        <<" records="<<profile.sampled_records<<'/'<<profile.eligible_records
        <<" fields="<<profile.sampled_field_types
        <<" base_ms="<<profile.type_base_ms
        <<" shape_ms="<<profile.type_shape_ms
        <<" derived_ms="<<profile.type_derived_ms
        <<" scope_lookup_ms="<<profile.sampled_named_details.scope_lookup_ms
        <<" alias_ms="<<profile.sampled_named_details.alias_classify_ms<<'\n';
    std::cout<<"V3-37R PASSED pairs="<<pairs<<" wins="<<wins
        <<" median_v337="<<v323_median(old_times)
        <<" median_v337r="<<v323_median(new_times)
        <<" paired_saved="<<v323_median(saved_times)
        <<" HEADER_GRAPH_PARITY_PASS full_PUBLISH_gate=PARTIAL\n";
}
#ifdef CW_V323_ALLOCATION_PROFILE
template<class State,class Fn,class Compare>
v323_allocation::snapshot counted(world& w,const semantic_world& oracle,Fn fn,Compare cmp){
    State s{w.strings};
    v323_allocation::counts={};v323_allocation::enabled=true;
    const bool success=fn(s,w);
    v323_allocation::enabled=false;
    const auto result=v323_allocation::counts;
    if(!success)throw std::runtime_error("allocation producer incomplete");
    ensure_parity(s,oracle,w.strings,cmp,"allocation");
    return result;
}
void allocations(const fs::path& project,const fs::path& output){
    world w;double c=0,p=0,d=0,cap=0;std::uint64_t checksum=0;
    build_inputs(w,project,c,p,d,cap,checksum);
    semantic_world oracle;run_header_v2_semantic(w,oracle);
    const auto a=counted<v337::producer_state>(w,oracle,v337::emit,v337::compare);
    const auto b=counted<v337r::producer_state>(w,oracle,v337r::emit,v337r::compare);
    std::ofstream csv;create_report(output,csv);
    csv<<"variant,new_calls,requested_bytes,coverage,header_graph_gate\n";
    csv<<"v337,"<<a.calls<<','<<a.requested_bytes
       <<",current_thread_global_new_only,HEADER_GRAPH_PARITY_PASS\n";
    csv<<"v337r,"<<b.calls<<','<<b.requested_bytes
       <<",current_thread_global_new_only,HEADER_GRAPH_PARITY_PASS\n";
    if(!csv)throw std::runtime_error("V337R allocation CSV write failed");
    std::cout<<"V3-37R allocation PASS V337="<<a.calls<<" V337R="<<b.calls
             <<" HEADER_GRAPH_PARITY_PASS\n";
}
#endif
}
// Diagnostic-only path; no paired timing used or stored.
namespace {
void run_fields_only(const fs::path& project,const fs::path& output,int stride){
    world w;double compose=0,prepare=0,decode=0,capture=0;
    std::uint64_t checksum=0;
    build_inputs(w,project,compose,prepare,decode,capture,checksum);
    semantic_world oracle;
    run_header_v2_semantic(w,oracle);
    v337r::producer_state s{w.strings};
    v337r::phase_profile p;
    p.sample_stride=static_cast<std::uint64_t>(stride);
    s.profile=&p;
    const auto t=clock_type::now();
    if(!v337r::emit(s,w))throw std::runtime_error("fields diagnostic emit incomplete");
    const auto total_ms=elapsed_ms(t);
    ensure_parity(s,oracle,w.strings,v337r::compare,"fields-only");
    if(p.sampled_records==0 || p.sampled_field_types==0 ||
       p.diag_fields_committed==0 ||
       p.diag_declaration_visits < p.diag_fields_committed ||
       p.diag_declaration_visits != p.diag_field_candidates+p.diag_callable_candidates+
           p.diag_access_decls+p.diag_other_decls)
        throw std::runtime_error("field diagnostic coverage invalid");
    std::ofstream csv;create_report(output,csv);
    csv << "stride,eligible_records,sampled_records,sampled_field_type_attempts,"
           "sampled_declaration_visits,sampled_field_candidates,sampled_callable_candidates,"
           "sampled_access_declarations,sampled_other_declarations,sampled_fields_committed,"
           "sampled_references,total_profile_ms,fields_ms_full_profile,"
           "descriptor_lower_bound_ms_sampled,produce_type_ms_sampled,"
           "member_write_ms_sampled,initializer_decode_ms_sampled,"
           "construction_set_ms_sampled,method_return_ms_sampled,"
           "reference_binding_ms_sampled,header_graph_gate,full_publish_gate\n";
    csv<<std::fixed<<std::setprecision(6)
       <<stride<<','<<p.eligible_records<<','<<p.sampled_records<<','
       <<p.sampled_field_types<<','<<p.diag_declaration_visits<<','
       <<p.diag_field_candidates<<','<<p.diag_callable_candidates<<','
       <<p.diag_access_decls<<','<<p.diag_other_decls<<','
       <<p.diag_fields_committed<<','<<p.diag_references<<','
       <<total_ms<<','<<p.fields_ms<<','
       <<p.diag_descriptor_lower_bound_ms<<','<<p.diag_type_ms<<','
       <<p.diag_member_write_ms<<','<<p.diag_init_decode_ms<<','
       <<p.diag_construction_ms<<','<<p.diag_method_return_ms<<','
       <<p.diag_reference_binding_ms
       <<",HEADER_GRAPH_PARITY_PASS,PARTIAL\n";
    if(!csv)throw std::runtime_error("fields diagnostic output failed");
    std::cout<<"V3-37R31 FIELD_DIAGNOSTIC_PASS sampled_records="<<p.sampled_records
             <<" fields="<<p.diag_fields_committed
             <<" produce_type_sampled_ms="<<p.diag_type_ms
             <<" HEADER_GRAPH_PARITY_PASS\n";
}
} // namespace
int main(int argc,char** argv){
    try{
        if(argc==5 && std::string_view{argv[1]}=="--fields-only"){
            const auto stride=parse_count(argv[4],"stride",1,1024);
            if(fs::exists(fs::path{argv[3]}))throw std::runtime_error("Output exists");
            run_fields_only(fs::path{argv[2]},fs::path{argv[3]},stride);
            return 0;
        }
#ifdef CW_V323_ALLOCATION_PROFILE
        if(argc==4 && std::string_view{argv[1]}=="--alloc-only"){
            allocations(fs::path{argv[2]},fs::path{argv[3]});return 0;
        }
#endif
        if(argc!=9){
            std::cerr<<"Usage: V337RPerf project.json pairs.csv phases.csv type.csv summary.csv warmups pairs stride\n";
            return 2;
        }
        const int warmups=parse_count(argv[6],"warmups",0,30);
        const int pairs=parse_count(argv[7],"pairs",1,100);
        const int stride=parse_count(argv[8],"stride",1,1024);
        const fs::path paths[]{argv[2],argv[3],argv[4],argv[5]};
        for(std::size_t i=0;i<4;++i){
            if(fs::exists(paths[i]))throw std::runtime_error("Output exists: "+paths[i].string());
            for(std::size_t j=0;j<i;++j)
                if(fs::absolute(paths[i])==fs::absolute(paths[j]))
                    throw std::runtime_error("Duplicate output path");
        }
        run(fs::path{argv[1]},paths[0],paths[1],paths[2],paths[3],warmups,pairs,stride);
        return 0;
    }catch(const std::exception& ex){
        std::cerr<<"V3-37 FAIL: "<<ex.what()<<'\n';return 1;
    }
}
