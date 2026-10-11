// V3-25: direct Graph write against untouched V3-23. Experimental overlay only.
#include "v3_24_perf_baseline.inc"
#define CW_V323_DIRECT_INDEX 1
#define CW_V324_DIRECT_GRAPH_WRITE 1
#define v319 v325
#include "v3_25_graph_producer.inc"
#undef v319
#undef CW_V324_DIRECT_GRAPH_WRITE
#undef CW_V323_DIRECT_INDEX

namespace {
[[nodiscard]] bool v325_parity(const v325::producer_state& produced,
    const semantic_world& semantic,const v325::comparison& c){
    return produced.unsupported==0 &&
        produced.G.type_count()==semantic.G.type_count() &&
        produced.G.member_count()==semantic.G.member_count() &&
        produced.G.derived_type_count()==semantic.G.derived_type_count() &&
        produced.G.constructor_defaults.entries().size()==semantic.G.constructor_defaults.entries().size() &&
        c.type_missing==0 && c.extra_types==0 && c.member_mismatch==0 &&
        c.construction_mismatch==0 && c.base_mismatch==0 &&
        c.polymorphic_mismatch==0 && c.nested_mismatch==0 &&
        c.identity_mismatch==0 && c.type_where_mismatch==0 &&
        c.member_where_mismatch==0 && c.derived_where_mismatch==0 &&
        c.derived_extra_v3==0 && c.derived_extra_v2==0 &&
        c.member_matches==semantic.G.member_count();
}

void v325_ab(const fs::path& project,const fs::path& pairs_path,
    const fs::path& phases_path,const fs::path& summary_path,
    int warmups,int pairs){
    world w;
    double compose=0,prepare=0,decode=0,capture=0;
    std::uint64_t checksum=0;
    build_inputs(w,project,compose,prepare,decode,capture,checksum);
    semantic_world oracle;
    run_header_v2_semantic(w,oracle); // reference, outside every timed interval
    std::ofstream paired;
    create_report(pairs_path,paired);
    paired<<"pair,phase,order,v323_ms,v325_ms,saved_ms,speedup_pct,types,members,derived,header_graph_gate,full_publish_gate\n";
    paired<<std::fixed<<std::setprecision(6);
    std::vector<double> older,newer,saved,speed;
    std::size_t wins=0;
    for(int iteration=0;iteration<warmups+pairs;++iteration){
        const bool warmup=iteration<warmups;
        const int index=warmup?iteration:iteration-warmups;
        const bool old_first=index%2==0;
        v323::producer_state old{w.strings};
        v325::producer_state neu{w.strings};
        double time_old=0,time_new=0;
        const auto before_old=[&](){
            const auto start=clock_type::now();
            if(!v323::emit(old,w))throw std::runtime_error("V323 incomplete");
            time_old=elapsed_ms(start);
        };
        const auto before_new=[&](){
            const auto start=clock_type::now();
            if(!v325::emit(neu,w))throw std::runtime_error("V325 incomplete");
            time_new=elapsed_ms(start);
        };
        if(old_first){before_old();before_new();}else{before_new();before_old();}
        const auto old_c=v323::compare(old,w.strings,oracle);
        const auto new_c=v325::compare(neu,w.strings,oracle);
        if(!full_parity(old,oracle,old_c) || !v325_parity(neu,oracle,new_c)){
            std::ostringstream reason;
            reason<<"HEADER_GRAPH_PARITY_FAILED pair="<<index+1
                <<" old_unchecked="<<old.unsupported
                <<" new_unchecked="<<neu.unsupported
                <<" types="<<neu.G.type_count()<<'/'<<oracle.G.type_count()
                <<" members="<<neu.G.member_count()<<'/'<<oracle.G.member_count()
                <<" who="<<new_c.identity_mismatch<<" type_where="<<new_c.type_where_mismatch
                <<" member_where="<<new_c.member_where_mismatch
                <<" derived_where="<<new_c.derived_where_mismatch
                <<" construction="<<new_c.construction_mismatch
                <<" nested="<<new_c.nested_mismatch;
            throw std::runtime_error(reason.str());
        }
        const double delta=time_old-time_new;
        const double pct=time_old>0?100*delta/time_old:0;
        paired<<index+1<<','<<(warmup?"warmup":"measure")<<','
              <<(old_first?"V323-V325":"V325-V323")<<','<<time_old<<','<<time_new<<','
              <<delta<<','<<pct<<','<<neu.G.type_count()<<','<<neu.G.member_count()<<','
              <<neu.G.derived_type_count()<<",HEADER_GRAPH_PARITY_PASS,PARTIAL\n";
        paired.flush();
        if(!warmup){older.push_back(time_old);newer.push_back(time_new);
            saved.push_back(delta);speed.push_back(pct);wins+=delta>0;}
        std::cout<<(warmup?"Warmup ":"Pair ")<<index+1
             <<" V323="<<time_old<<" V325="<<time_new<<" saved="<<delta
             <<" HEADER_GRAPH_PARITY_PASS\n"<<std::flush;
    }
    v325::producer_state instrumented{w.strings};
    v325::phase_profile p;
    instrumented.profile=&p;
    const auto began=clock_type::now();
    if(!v325::emit(instrumented,w))throw std::runtime_error("Instrumented V325 incomplete");
    const double total=elapsed_ms(began);
    const auto cmp=v325::compare(instrumented,w.strings,oracle);
    if(!v325_parity(instrumented,oracle,cmp))
        throw std::runtime_error("Instrumented V325 HEADER_GRAPH_PARITY_FAILED");
    const double explained=p.syntax_ms+p.identity_ms+p.index_ms+p.fields_ms+
        p.constructors_ms+p.inheritance_ms+p.graph_define_ms+p.nested_ms;
    if(explained>total+0.1)throw std::runtime_error("V325 phase accounting exceeds total");
    std::ofstream phases,summary;
    create_report(phases_path,phases);
    phases<<"mode,total_ms,syntax_scan_ms,identity_ms,record_index_ms,fields_ms,constructors_ms,inheritance_ms,graph_commit_ms,nested_ms,unattributed_ms,records,header_graph_gate\n";
    phases<<std::fixed<<std::setprecision(6)
          <<"v325_instrumented,"<<total<<','<<p.syntax_ms<<','<<p.identity_ms<<','
          <<p.index_ms<<','<<p.fields_ms<<','<<p.constructors_ms<<','
          <<p.inheritance_ms<<','<<p.graph_define_ms<<','<<p.nested_ms<<','
          <<std::max(0.0,total-explained)<<','<<p.records<<",HEADER_GRAPH_PARITY_PASS\n";
    create_report(summary_path,summary);
    summary<<"mode,warmups,pairs,effective_tokens,types,members,derived,median_v323_ms,median_v325_ms,median_paired_saved_ms,median_paired_speedup_pct,paired_wins,header_graph_gate,full_publish_gate\n";
    const auto corpus=observe_corpus(w);
    summary<<std::fixed<<std::setprecision(6)
           <<"v325,"<<warmups<<','<<pairs<<','<<corpus.effective_tokens<<','
           <<instrumented.G.type_count()<<','<<instrumented.G.member_count()<<','
           <<instrumented.G.derived_type_count()<<','<<v323_median(older)<<','
           <<v323_median(newer)<<','<<v323_median(saved)<<','
           <<v323_median(speed)<<','<<wins<<",HEADER_GRAPH_PARITY_PASS,PARTIAL\n";
    if(!paired||!phases||!summary)throw std::runtime_error("V325 CSV output failed");
    std::cout<<"V3-25 PASSED pairs="<<pairs<<" wins="<<wins
             <<" median_v323="<<v323_median(older)
             <<" median_v325="<<v323_median(newer)
             <<" paired_saved="<<v323_median(saved)
             <<" HEADER_GRAPH_PARITY_PASS\n";
}
#ifdef CW_V323_ALLOCATION_PROFILE
void v325_allocations(const fs::path& project,const fs::path& output){
    world w;
    double compose=0,prepare=0,decode=0,capture=0;
    std::uint64_t checksum=0;
    build_inputs(w,project,compose,prepare,decode,capture,checksum);
    semantic_world oracle;
    run_header_v2_semantic(w,oracle);
    // Equal warmed conditions; no timers inside the allocation probe.
    v323::producer_state old{w.strings};
    v325::producer_state neu{w.strings};
    v323_allocation::counts={};v323_allocation::enabled=true;
    const bool baseline_ok=v323::emit(old,w);
    v323_allocation::enabled=false;
    const auto baseline=v323_allocation::counts;
    v323_allocation::counts={};v323_allocation::enabled=true;
    const bool new_ok=v325::emit(neu,w);
    v323_allocation::enabled=false;
    const auto direct=v323_allocation::counts;
    if(!baseline_ok || !new_ok || !full_parity(old,oracle,v323::compare(old,w.strings,oracle)) ||
       !v325_parity(neu,oracle,v325::compare(neu,w.strings,oracle)))
        throw std::runtime_error("Allocation profile HEADER_GRAPH_PARITY_FAILED");
    std::ofstream f;create_report(output,f);
    f<<"mode,v323_new_calls,v323_requested_bytes,v325_new_calls,v325_requested_bytes,coverage,header_graph_gate\n";
    f<<"v325_alloc,"<<baseline.calls<<','<<baseline.requested_bytes<<','
     <<direct.calls<<','<<direct.requested_bytes
     <<",current_thread_global_new_only,HEADER_GRAPH_PARITY_PASS\n";
    if(!f)throw std::runtime_error("Allocation CSV failed");
    std::cout<<"V3-25 allocation probe: V323 calls="<<baseline.calls
             <<" V325 calls="<<direct.calls<<" HEADER_GRAPH_PARITY_PASS\n";
}
#endif
} // anonymous namespace

int main(int argc,char** argv){
    try{
#ifdef CW_V323_ALLOCATION_PROFILE
        if(argc==4 && std::string_view{argv[1]}=="--alloc-only"){
            v325_allocations(fs::path{argv[2]},fs::path{argv[3]});return 0;
        }
#endif
        if(argc!=7){
            std::cerr<<"Usage: V325Perf project.json new-pairs.csv new-phases.csv new-summary.csv warmups pairs\n";
            return 2;
        }
        const int warmups=parse_count(argv[5],"warmups",0,30);
        const int pairs=parse_count(argv[6],"pairs",1,100);
        const fs::path paths[]{argv[2],argv[3],argv[4]};
        for(unsigned i=0;i<3;++i){
            if(fs::exists(paths[i]))throw std::runtime_error("Output already exists: "+paths[i].string());
            for(unsigned j=0;j<i;++j)
                if(fs::absolute(paths[i])==fs::absolute(paths[j]))
                    throw std::runtime_error("Outputs must be distinct");
        }
        v325_ab(fs::path{argv[1]},paths[0],paths[1],paths[2],warmups,pairs);
        return 0;
    }catch(const std::exception& ex){
        std::cerr<<"V3-25 FAIL: "<<ex.what()<<'\n';return 1;
    }
}
