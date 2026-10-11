// V3-22: paired native Header Graph benchmark. Standalone and read-only.
// Reuse the exact V3-21 corpus preparation / producers / parity comparator.
#define main header_syntax_v321_reference_main_unused
#include "header_syntax_v3_17_project_benchmark.cpp"
#undef main
#include <algorithm>
#include <cmath>
#include <numeric>
#ifdef CW_V322_ALLOCATION_PROFILE
#include <atomic>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
namespace v322_allocation {
    // Intentional single-thread profiling counters. Only allocations made by
    // the calling thread while enabled are counted. These are requested bytes,
    // NOT OS committed bytes, allocator overhead, or live/peak memory.
    struct snapshot final { std::uint64_t calls=0, requested_bytes=0; };
    inline thread_local bool enabled=false;
    inline thread_local snapshot counts{};
    void record(std::size_t bytes) noexcept {
        if(enabled){ ++counts.calls; counts.requested_bytes+=bytes; }
    }
}
void* operator new(std::size_t n){
    if(void* p=std::malloc(n?n:1)){v322_allocation::record(n);return p;}
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){
    const auto align=static_cast<std::size_t>(a);
#ifdef _WIN32
    void* p=_aligned_malloc(n?n:1,align);
#else
    void* p=nullptr;
    if(posix_memalign(&p,align,n?n:1)!=0)p=nullptr;
#endif
    if(!p)throw std::bad_alloc();
    v322_allocation::record(n);return p;
}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete(void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>
#endif

namespace {
struct process_memory final {
    std::uint64_t private_bytes=0, working_set_bytes=0, peak_working_set_bytes=0;
};
[[nodiscard]] process_memory memory_now() noexcept {
    process_memory x;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX c{};
    c.cb=sizeof(c);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c),sizeof(c))){
        x.private_bytes=static_cast<std::uint64_t>(c.PrivateUsage);
        x.working_set_bytes=static_cast<std::uint64_t>(c.WorkingSetSize);
        x.peak_working_set_bytes=static_cast<std::uint64_t>(c.PeakWorkingSetSize);
    }
#elif defined(__linux__)
    // /proc records are process-wide snapshots, NOT allocator-trace / graph ownership.
    std::ifstream status("/proc/self/status");
    std::string key;
    std::uint64_t kb=0;
    while(status>>key>>kb){
        if(key=="VmRSS:")x.working_set_bytes=kb*1024;
        else if(key=="VmHWM:")x.peak_working_set_bytes=kb*1024;
        else if(key=="VmSize:")x.private_bytes=kb*1024; // Virtual size, unlike Windows private bytes.
        std::string rest;std::getline(status,rest);
    }
#endif
    return x;
}
[[nodiscard]] bool full_parity(const v319::producer_state& produced,
    const semantic_world& semantic,const v319::comparison& c) {
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
[[nodiscard]] double v322_median(std::vector<double> a){
    if(a.empty())throw std::runtime_error("No samples for median");
    std::sort(a.begin(),a.end());
    const auto middle=a.size()/2;
    return a.size()%2?a[middle]:(a[middle-1]+a[middle])*0.5;
}
[[nodiscard]] double percentile(std::vector<double> a,double quantile){
    if(a.empty())throw std::runtime_error("No percentile samples");
    std::sort(a.begin(),a.end());
    const double index=(a.size()-1)*quantile;
    const auto lower=static_cast<std::size_t>(index);
    const auto upper=std::min(a.size()-1,lower+1);
    return a[lower]+(a[upper]-a[lower])*(index-lower);
}
void create_report(const fs::path& p,std::ofstream& out){
    if(fs::exists(p))throw std::runtime_error("Output already exists: "+p.string());
    if(!p.parent_path().empty())fs::create_directories(p.parent_path());
    out.open(p,std::ios::out|std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("Cannot create output: "+p.string());
}
struct pair_results {
    double v3_ms=0,v2_ms=0;
    process_memory after_v3{},after_v2{};
    std::uint64_t types=0,members=0,derived=0;
};
pair_results run_pair(world& w,bool v3_first,v319::phase_profile* optional_profile=nullptr){
    // Graph instances are fresh EACH run, otherwise memory/WHO/WHERE parity is invalid.
    // The timed operation is exactly V3::emit() / run_header_v2_semantic().
    // Input, graph construction and oracle comparison are excluded in both timings.
    v319::producer_state v3{w.strings};
    v3.profile=optional_profile;
    semantic_world v2;
    pair_results out;
    const auto run_v3=[&](){
        const auto began=clock_type::now();
        const bool ok=v319::emit(v3,w);
        out.v3_ms=elapsed_ms(began);
        out.after_v3=memory_now();
        if(!ok)throw std::runtime_error("V3 emit incomplete: "+
            std::to_string(v3.unsupported)+" unsupported operations");
    };
    const auto run_v2=[&](){
        run_header_v2_semantic(w,v2);
        out.v2_ms=v2.metrics.v2_parse_ms;
        out.after_v2=memory_now();
    };
    if(v3_first){run_v3();run_v2();}else{run_v2();run_v3();}
    // A count match is NOT sufficient: compare full type/member/bases/construction/
    // identity/WHERE/derived/nested domains after both time intervals.
    const auto c=v319::compare(v3,w.strings,v2);
    if(!full_parity(v3,v2,c)){
        std::ostringstream msg;
        msg<<"HEADER_GRAPH_PARITY_FAILED types="<<v3.G.type_count()<<'/'<<v2.G.type_count()
           <<" members="<<v3.G.member_count()<<'/'<<v2.G.member_count()
           <<" derived="<<v3.G.derived_type_count()<<'/'<<v2.G.derived_type_count()
           <<" who="<<c.identity_mismatch<<" where="<<c.type_where_mismatch
           <<" derived_where="<<c.derived_where_mismatch<<" member="<<c.member_mismatch;
        throw std::runtime_error(msg.str());
    }
    out.types=v3.G.type_count();out.members=v3.G.member_count();
    out.derived=v3.G.derived_type_count();
    return out;
}
} // namespace

int main(int argc,char** argv){
    try {
#ifdef CW_V322_ALLOCATION_PROFILE
        if(argc==4 && std::string_view{argv[1]}=="--alloc-only"){
            const fs::path out_path=argv[3];
            if(fs::exists(out_path))throw std::runtime_error("Result file must be new: "+out_path.string());
            world w;
            double compose=0,prepare=0,decode=0,capture=0;
            std::uint64_t checksum=0;
            build_inputs(w,fs::path{argv[2]},compose,prepare,decode,capture,checksum);
            // Stabilize any input-owned interning during untimed warmup.
            (void)run_pair(w,true);
            v319::producer_state v3{w.strings};
            semantic_world v2;
            v322_allocation::counts={};
            v322_allocation::enabled=true;
            const bool produced=v319::emit(v3,w);
            v322_allocation::enabled=false;
            const auto v3_alloc=v322_allocation::counts;
            if(!produced)throw std::runtime_error("V3 allocation profile incomplete");
            v322_allocation::counts={};
            v322_allocation::enabled=true;
            run_header_v2_semantic(w,v2);
            v322_allocation::enabled=false;
            const auto v2_alloc=v322_allocation::counts;
            const auto c=v319::compare(v3,w.strings,v2);
            if(!full_parity(v3,v2,c))throw std::runtime_error("Allocation profile parity failed");
            std::ofstream f;
            create_report(out_path,f);
            f<<"mode,v3_new_calls,v3_new_requested_bytes,v2_new_calls,v2_new_requested_bytes,"
                "coverage,header_graph_gate\n";
            f<<"v322_allocation,"<<v3_alloc.calls<<','<<v3_alloc.requested_bytes
             <<','<<v2_alloc.calls<<','<<v2_alloc.requested_bytes
             <<",current_thread_global_new_only,HEADER_GRAPH_PARITY_PASS\n";
            f.flush();
            if(!f)throw std::runtime_error("Allocation CSV write failed");
            std::cout<<"Allocation profile PASS V3 calls="<<v3_alloc.calls
                     <<" requested="<<v3_alloc.requested_bytes
                     <<" V2 calls="<<v2_alloc.calls
                     <<" requested="<<v2_alloc.requested_bytes<<"\n";
            return 0;
        }
#endif
        if(argc!=7){
            std::cerr<<"Usage: HeaderV322Perf <project.json> <new-pairs.csv> <new-phases.csv> <new-summary.csv> <warmups> <pairs>\n";
            return 2;
        }
        const auto warmups=parse_count(argv[5],"warmups",0,30);
        const auto pairs=parse_count(argv[6],"pairs",1,100);
        const fs::path paths[]{argv[2],argv[3],argv[4]};
        for(unsigned i=0;i<3;++i){
            if(fs::exists(paths[i]))throw std::runtime_error("Result file must be new: "+paths[i].string());
            for(unsigned j=0;j<i;++j)if(fs::absolute(paths[i])==fs::absolute(paths[j]))
                throw std::runtime_error("Output paths must differ");
        }
        world w;
        double compose=0,prepare=0,decode=0,capture=0;
        std::uint64_t origin_checksum=0;
        build_inputs(w,fs::path{argv[1]},compose,prepare,decode,capture,origin_checksum);
        const auto corpus=observe_corpus(w);
        if(!corpus.effective_tokens)throw std::runtime_error("Empty effective corpus");
        std::ofstream samples,phases,summary;
        create_report(paths[0],samples);
        samples<<"pair,phase,order,v3_ms,v2_ms,saved_ms,speedup_pct,types,members,derived,"
            "baseline_private_bytes,v3_private_bytes,v2_private_bytes,"
            "baseline_working_set_bytes,v3_working_set_bytes,v2_working_set_bytes,"
            "process_peak_working_set_bytes,header_graph_gate\n";
        samples<<std::fixed<<std::setprecision(6);
        std::vector<double> a,b,saved,speed;
        std::size_t wins=0;
        for(std::size_t iteration=0;iteration<warmups+pairs;++iteration){
            const bool warmup=iteration<warmups;
            // Alternate starting order within measured pairs, independently of warmup count.
            const auto i=warmup?iteration:iteration-warmups;
            const bool v3_first=i%2==0;
            const auto base=memory_now();
            auto r=run_pair(w,v3_first);
            const auto delta=r.v2_ms-r.v3_ms;
            const auto pct=r.v2_ms>0?100*delta/r.v2_ms:0;
            samples<<i+1<<','<<(warmup?"warmup":"measure")<<','
                <<(v3_first?"V3-V2":"V2-V3")<<','<<r.v3_ms<<','<<r.v2_ms
                <<','<<delta<<','<<pct<<','<<r.types<<','<<r.members<<','<<r.derived
                <<','<<base.private_bytes<<','<<r.after_v3.private_bytes
                <<','<<r.after_v2.private_bytes<<','<<base.working_set_bytes
                <<','<<r.after_v3.working_set_bytes<<','<<r.after_v2.working_set_bytes
                <<','<<std::max(r.after_v3.peak_working_set_bytes,r.after_v2.peak_working_set_bytes)
                <<",HEADER_GRAPH_PARITY_PASS\n";
            samples.flush();
            if(!warmup){
                a.push_back(r.v3_ms);b.push_back(r.v2_ms);
                saved.push_back(delta);speed.push_back(pct);wins+=delta>0;
            }
            std::cout<<(warmup?"Warmup ":"Pair ")<<i+1<<" order="
                <<(v3_first?"V3-V2":"V2-V3")<<" V3="<<r.v3_ms
                <<" V2="<<r.v2_ms<<" saved="<<delta
                <<" ms HEADER_GRAPH_PARITY_PASS\n"<<std::flush;
        }
        // Separate instrumented profile AFTER measured pairs. It cannot distort A/B.
        v319::phase_profile prof;
        const auto profiled=run_pair(w,true,&prof);
        const double explained=prof.syntax_ms+prof.identity_ms+prof.index_ms+
           prof.fields_ms+prof.constructors_ms+prof.inheritance_ms+
           prof.graph_define_ms+prof.nested_ms;
        const auto other=std::max(0.,profiled.v3_ms-explained);
        create_report(paths[1],phases);
        phases<<"mode,profiled_v3_ms,syntax_scan_ms,identity_registration_ms,"
                 "record_index_ms,field_and_method_ms,constructor_decode_ms,inheritance_ms,"
                 "graph_define_ms,nested_defaults_ms,unattributed_ms,profiled_records,"
                 "v3_types,v3_members,v3_derived,header_graph_gate\n";
        phases<<std::fixed<<std::setprecision(6)
              <<"v322_instrumented,"<<profiled.v3_ms<<','<<prof.syntax_ms<<','
              <<prof.identity_ms<<','<<prof.index_ms<<','<<prof.fields_ms<<','
              <<prof.constructors_ms<<','<<prof.inheritance_ms<<','
              <<prof.graph_define_ms<<','<<prof.nested_ms<<','<<other<<','
              <<prof.records<<','<<profiled.types<<','<<profiled.members<<','
              <<profiled.derived<<",HEADER_GRAPH_PARITY_PASS\n";
        create_report(paths[2],summary);
        summary<<"mode,warmups,pairs,effective_tokens,types,members,derived,"
               "median_v3_ms,median_v2_ms,median_paired_saved_ms,median_paired_speedup_pct,"
               "v3_p10_ms,v3_p90_ms,v2_p10_ms,v2_p90_ms,paired_wins,"
               "compose_ms,physical_prepare_ms,physical_decode_ms,effective_capture_ms,"
               "profiled_v3_ms,header_graph_gate,full_publish_gate\n";
        summary<<std::fixed<<std::setprecision(6)
               <<"v322,"<<warmups<<','<<pairs<<','<<corpus.effective_tokens<<','
               <<profiled.types<<','<<profiled.members<<','<<profiled.derived<<','
               <<v322_median(a)<<','<<v322_median(b)<<','<<v322_median(saved)<<','<<v322_median(speed)<<','
               <<percentile(a,0.1)<<','<<percentile(a,0.9)<<','
               <<percentile(b,0.1)<<','<<percentile(b,0.9)<<','<<wins<<','
               <<compose<<','<<prepare<<','<<decode<<','<<capture<<','
               <<profiled.v3_ms<<",HEADER_GRAPH_PARITY_PASS,PARTIAL\n";
        samples.flush();phases.flush();summary.flush();
        if(!samples||!phases||!summary)throw std::runtime_error("CSV write failed");
        std::cout<<"V3-22 PASSED: pairs="<<pairs<<" wins="<<wins
                 <<" median_v3_ms="<<v322_median(a)<<" median_v2_ms="<<v322_median(b)
                 <<" median_saved_ms="<<v322_median(saved)
                 <<" median_paired_speedup_pct="<<v322_median(speed)
                 <<" header_graph_gate=HEADER_GRAPH_PARITY_PASS full_PUBLISH_gate=PARTIAL\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"V3-22 FAIL: "<<e.what()<<'\n';
        return 1;
    }
}
