#include "fixed_shared_memory.hpp"
#include "project/task/runtime/task_manager.hpp"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <cstdint>
using namespace cw::server;
using namespace cw::server::task_runtime;
struct shared_state {
    volatile LONG init[32], calls[32], done[32], sequence, last_call[32];
    volatile LONGLONG begin_qpc[32][4], end_qpc[32][4];
    volatile LONG max_task_us[32];
};
static task_definition make_task(const char* id, const std::filesystem::path& exe) {
    task_definition t; t.id=id; t.executable=exe.string(); return t;
}
int wmain(int argc, wchar_t** argv) {
    // Legacy: <A> <B> <mode> <ticks> <period>; scalable: <exe...> <mode> <ticks> <period>.
    if(argc < 6 || argc > 36) { std::cerr << "usage: benchmark <fixture...> <DIRECT_WAIT|NO_BARRIER> <ticks> <period_ms>\n"; return 2; }
    const int taskCount = argc - 4;
    if(taskCount < 2 || taskCount > 32) return 2;
    const std::wstring mode=argv[argc-3];
    if(mode!=L"DIRECT_WAIT" && mode!=L"NO_BARRIER") return 2;
    const int ticks=_wtoi(argv[argc-2]);
    const int periodMs=_wtoi(argv[argc-1]);
    if(periodMs!=1 && periodMs!=2 && periodMs!=5) return 2;
    if(ticks<1 || ticks>100000) return 2;
    if(_putenv_s("TASK_PERF52_NO_BARRIER", mode==L"NO_BARRIER"?"1":"0")!=0) return 2;
    const auto size=(std::max)(fixed_shared_memory::size_alignment(),std::size_t{65536});
    const std::string name="Perf52AB_"+std::to_string(GetCurrentProcessId());
    fixed_shared_memory shm;
    bool created=false;
    for(int i=0;i<32&&!created;++i){
        void* p=VirtualAlloc(nullptr,size,MEM_RESERVE,PAGE_NOACCESS);
        if(!p) break;
        const auto address=reinterpret_cast<std::uintptr_t>(p);
        VirtualFree(p,0,MEM_RELEASE);
        created=shm.create(name,size,address)==fixed_shared_memory_result::success;
    }
    if(!created){std::cerr<<"SHM create failed\n";return 3;}
    auto* state=reinterpret_cast<shared_state*>(shm.data());
    shm_attachment attachment{};
    attachment.size=shm.size();attachment.address=shm.address();
    if(name.size()>=sizeof(attachment.name))return 3;
    std::memcpy(attachment.name,name.c_str(),name.size()+1);
    task_configuration config;
    for (int i=0; i<taskCount; ++i) {
        config.tasks.push_back(make_task(("Task"+std::to_string(i)).c_str(), std::filesystem::absolute(argv[i+1])));
    }
    manager runtime;
    std::string error;
    if(!runtime.start(config,std::filesystem::current_path(),5000,attachment,error)){
        std::cerr<<"start: "<<error<<'\n';return 4;
    }
    LARGE_INTEGER freq{},start{},end{};
    QueryPerformanceFrequency(&freq);
    FILETIME create{},exit{},kernel0{},user0{},kernel1{},user1{};
    GetProcessTimes(GetCurrentProcess(),&create,&exit,&kernel0,&user0);
    QueryPerformanceCounter(&start);
    const LONGLONG interval=(freq.QuadPart*periodMs)/1000;
    LONGLONG next=start.QuadPart;
    double maxLatenessMs=0;
    for(int i=1;i<=ticks;++i){
        // QPC-paced absolute deadlines. Windows scheduling jitter is measured, not hidden.
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        while(now.QuadPart<next){
            const LONGLONG remaining=next-now.QuadPart;
            // Uniform QPC pacing for 1/2/5 ms. Do not use Sleep(1):
            // Windows timer granularity can delay the 5 ms case by >1 tick.
            YieldProcessor();
            QueryPerformanceCounter(&now);
        }
        const double lateness=1000.0*double(now.QuadPart-next)/double(freq.QuadPart);
        maxLatenessMs=(std::max)(maxLatenessMs,lateness);
        next+=interval;
        if(!runtime.tick(static_cast<std::uint64_t>(i),false,error)){
            std::cerr<<"tick "<<i<<": "<<error<<'\n';runtime.terminate();return 5;
        }
    }
    QueryPerformanceCounter(&end);
    GetProcessTimes(GetCurrentProcess(),&create,&exit,&kernel1,&user1);
    const auto cpu=[](const FILETIME& f)->unsigned long long{
        ULARGE_INTEGER u{};u.LowPart=f.dwLowDateTime;u.HighPart=f.dwHighDateTime;return u.QuadPart;
    };
    const double dispatchMs=1000.0*double(end.QuadPart-start.QuadPart)/double(freq.QuadPart);
    const double cpuMs=double(cpu(kernel1)+cpu(user1)-cpu(kernel0)-cpu(user0))/10000.0;
    // Allow the last dispatched call to finish. This wait is excluded from loop elapsed.
    Sleep(50);
    const auto diag=runtime.no_barrier_snapshot();
    if(!runtime.stop(error)){std::cerr<<"stop: "<<error<<'\n';runtime.terminate();return 6;}
    long long totalCalls=0;
    long long totalMissing=0;
    bool good=true;
    for(int i=0;i<taskCount;++i) {
        const LONG calls=state->calls[i];
        totalCalls+=calls;
        totalMissing+=ticks-calls;
        if(calls!=ticks || state->done[i]!=1 || state->init[i]!=1) good=false;
    }
    const double wallMs=1000.0*double(end.QuadPart-start.QuadPart)/double(freq.QuadPart);
    const double taskRate=1000.0*double(totalCalls)/wallMs;
    std::cout<<"PERF52_RESULT mode="<<(mode==L"NO_BARRIER"?"NO_BARRIER":"DIRECT_WAIT")
             <<" ticks="<<ticks<<" period_ms="<<periodMs<<" wall_ms="<<wallMs
             <<" max_lateness_ms="<<maxLatenessMs<<" calls_per_sec="<<taskRate
             <<" missing_calls_proxy="<<totalMissing<<" parent_cpu_ms="<<cpuMs
             <<" tasks="<<taskCount<<" total_calls="<<totalCalls
             <<" calls_a="<<state->calls[0]<<" calls_b="<<state->calls[1]
             <<" done_a="<<state->done[0]<<" done_b="<<state->done[1];
    for(int i=0;i<taskCount;++i) {
        std::cout<<" calls_"<<i<<"="<<state->calls[i]
                 <<" done_"<<i<<"="<<state->done[i]
                 <<" init_"<<i<<"="<<state->init[i];
    }
    if(mode==L"NO_BARRIER") {
        if(diag.size()!=static_cast<std::size_t>(taskCount)) good=false;
        for(int i=0;i<taskCount && i<static_cast<int>(diag.size());++i) {
            const auto& d=diag[i];
            std::cout<<" scheduled_"<<i<<"="<<d.scheduled
                     <<" dispatched_"<<i<<"="<<d.dispatched
                     <<" completed_"<<i<<"="<<d.completed
                     <<" reaped_on_tick_"<<i<<"="<<d.reaped_on_tick
                     <<" skipped_busy_"<<i<<"="<<d.skipped_busy
                     <<" failed_"<<i<<"="<<d.failed
                     <<" first_skipped_cycle_"<<i<<"="<<d.first_skipped_cycle
                     <<" first_skipped_qpc_"<<i<<"="<<d.first_skipped_qpc
                     <<" max_tick_lock_wait_us_"<<i<<"="<<d.max_tick_lock_wait_us
                     <<" max_dispatch_us_"<<i<<"="<<d.max_dispatch_us
                     <<" max_task_us_"<<i<<"="<<state->max_task_us[i];
            if(d.scheduled!=static_cast<std::uint64_t>(ticks) ||
               d.dispatched!=static_cast<std::uint64_t>(ticks) ||
               d.completed!=static_cast<std::uint64_t>(ticks) ||
               d.skipped_busy!=0 || d.failed!=0) good=false;
        }
    }
    std::cout<<'\n';
    if (!good) {
        // Last four task invocations help correlate common stalls across workers.
        for (int i=0; i<taskCount; ++i) {
            std::cerr << "SCALE02_TASK task=" << i << " calls=" << state->calls[i]
                      << " max_task_us=" << state->max_task_us[i];
            for (int j=0; j<4; ++j)
                std::cerr << " begin_qpc_" << j << "=" << state->begin_qpc[i][j]
                          << " end_qpc_" << j << "=" << state->end_qpc[i][j];
            std::cerr << '\n';
        }
    }
    return good ? 0 : 9;
}
