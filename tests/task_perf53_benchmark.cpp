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
struct shared_state { volatile LONG init[3], calls[3], done[3], sequence, last_call[3]; };
static task_definition make_task(const char* id, const std::filesystem::path& exe) {
    task_definition t; t.id=id; t.executable=exe.string(); return t;
}
int wmain(int argc, wchar_t** argv) {
    if(argc != 6) { std::cerr << "usage: benchmark <fixtureA> <fixtureB> <DIRECT_WAIT|NO_BARRIER> <ticks> <period_ms>\n"; return 2; }
    const std::wstring mode=argv[3];
    if(mode!=L"DIRECT_WAIT" && mode!=L"NO_BARRIER") return 2;
    const int ticks=_wtoi(argv[4]);
    const int periodMs=_wtoi(argv[5]);
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
    config.tasks={make_task("A",std::filesystem::absolute(argv[1])),make_task("B",std::filesystem::absolute(argv[2]))};
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
            if(remaining>freq.QuadPart/500) Sleep(1);
            else SwitchToThread();
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
    Sleep(100);
    const LONG a=state->calls[0],b=state->calls[1];
    if(a<1 || b<1 || a>ticks || b>ticks){
        std::cerr<<"invalid task counts\n";runtime.terminate();return 8;
    }
    const double wallMs=1000.0*double(end.QuadPart-start.QuadPart)/double(freq.QuadPart);
    const double taskRate=1000.0*double(a+b)/wallMs;
    const int missingCalls=2*ticks-int(a+b); // proxy only, NOT manager skipped_busy counter
    if(!runtime.stop(error)){std::cerr<<"stop: "<<error<<'\n';runtime.terminate();return 6;}
    if(state->done[0]!=1||state->done[1]!=1){std::cerr<<"TASKDONE missing\n";return 7;}
    std::cout<<"PERF52_RESULT mode="<<(mode==L"NO_BARRIER"?"NO_BARRIER":"DIRECT_WAIT")
             <<" ticks="<<ticks<<" period_ms="<<periodMs<<" wall_ms="<<wallMs
             <<" max_lateness_ms="<<maxLatenessMs<<" calls_per_sec="<<taskRate
             <<" missing_calls_proxy="<<missingCalls
             <<" parent_cpu_ms="<<cpuMs<<" calls_a="<<a<<" calls_b="<<b
             <<" done_a="<<state->done[0]<<" done_b="<<state->done[1]<<'\n';
    return 0;
}
