#pragma once
/* DIAG-45: bounded asynchronous Task Host CSV. Diagnostic loss never stalls LOCKSTEP. */
#include <Windows.h>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace cw::server::task_runtime {
class task_async_csv final {
public:
    enum class kind { cpu, cycles, calls };
    struct entry {
        kind type{};
        unsigned long long cycle{}, a{}, b{};
        unsigned index{}, count{};
        long long t0{}, t1{};
        bool valid{}, ok{};
    };
    explicit task_async_csv(const std::wstring& name) {
        wchar_t dir[32768]{};
        const DWORD n=GetEnvironmentVariableW(L"TASK_SHM_PERF07_DIR",dir,32768);
        if (!n || n>=32768 || !QueryPerformanceFrequency(&frequency) || !frequency.QuadPart) return;
        CreateDirectoryW(dir,nullptr);
        std::wstring safe=name;
        for (auto& c:safe) if (c==L'\\'||c==L'/'||c==L':'||c==L' '||c==L'"') c=L'_';
        auto open=[&](FILE*& f,const wchar_t* prefix,const char* title) {
            auto path=std::wstring(dir)+L"\\"+prefix+safe+L".csv";
            if (_wfopen_s(&f,path.c_str(),L"wb") || !f) return;
            setvbuf(f,nullptr,_IOFBF,1024*1024);
            std::fputs(title,f);
        };
        open(cpu,L"cpu_","cycle,call_index,wall_us,cpu_us,off_cpu_us,cpu_valid,ok\n");
        open(cycles,L"cycles_","cycle,call_index,thread_cycles,cycle_valid,ok\n");
        open(calls,L"calls_","seq,cycle,call_index,call_count,start_qpc,end_qpc,ok,frequency\n");
        if (!cpu && !cycles && !calls) return;
        queue=std::make_unique<std::array<entry,65536>>();
        try { writer=std::thread([this]{ drain(); }); }
        catch (...) { queue.reset(); }
    }
    ~task_async_csv() {
        { std::lock_guard<std::mutex> lock(mutex); stop=true; }
        cv.notify_one();
        if (writer.joinable()) writer.join();
        if (cpu) std::fclose(cpu);
        if (cycles) std::fclose(cycles);
        if (calls) std::fclose(calls);
    }
    bool enabled() const noexcept { return queue!=nullptr; }
    void record_cpu(unsigned long long cycle,unsigned index,long long t0,long long t1,
                    unsigned long long c0,unsigned long long c1,bool valid,bool ok) noexcept {
        if (cpu) push({kind::cpu,cycle,c0,c1,index,0,t0,t1,valid,ok});
    }
    void record_cycles(unsigned long long cycle,unsigned index,unsigned long long c0,
                       unsigned long long c1,bool valid,bool ok) noexcept {
        if (cycles) push({kind::cycles,cycle,c0,c1,index,0,0,0,valid,ok});
    }
    void record_call(long seq,unsigned long long cycle,unsigned index,unsigned count,
                     long long t0,long long t1,bool ok) noexcept {
        if (calls) push({kind::calls,cycle,static_cast<unsigned long long>(seq),0,index,count,t0,t1,true,ok});
    }
    static bool sample_cpu(unsigned long long& ticks) noexcept {
        FILETIME a{},b{},k{},u{};
        if (!GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u)) return false;
        ULARGE_INTEGER x{},y{};
        x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;
        y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;
        ticks=x.QuadPart+y.QuadPart;
        return true;
    }
    static bool sample_cycles(unsigned long long& ticks) noexcept {
        return QueryThreadCycleTime(GetCurrentThread(),&ticks)!=0;
    }
private:
    void push(const entry& e) noexcept {
        if (!queue) return;
        { std::lock_guard<std::mutex> lock(mutex);
          if (used==65536) { ++dropped; return; }
          (*queue)[tail]=e;tail=(tail+1)%65536;++used;
        }
        cv.notify_one();
    }
    void drain() noexcept {
        for (;;) {
            entry e{};
            { std::unique_lock<std::mutex> lock(mutex);
              cv.wait(lock,[this]{return stop||used;});
              if (!used && stop) break;
              e=(*queue)[head];head=(head+1)%65536;--used;
            }
            switch(e.type) {
            case kind::cpu: {
                if (!cpu) break;
                const double wall=double(e.t1-e.t0)*1e6/double(frequency.QuadPart);
                const bool valid=e.valid&&e.b>=e.a;
                const double used_cpu=valid?double(e.b-e.a)/10.0:0.0;
                std::fprintf(cpu,"%llu,%u,%.3f,%.3f,%.3f,%d,%d\n",e.cycle,e.index,
                    wall,used_cpu,valid?(wall>used_cpu?wall-used_cpu:0.0):0.0,valid?1:0,e.ok?1:0);
                break;
            }
            case kind::cycles: {
                if (cycles) std::fprintf(cycles,"%llu,%u,%llu,%d,%d\n",e.cycle,e.index,
                    e.valid&&e.b>=e.a?e.b-e.a:0ULL,e.valid&&e.b>=e.a?1:0,e.ok?1:0);
                break;
            }
            case kind::calls: {
                if (calls) std::fprintf(calls,"%lld,%llu,%u,%u,%lld,%lld,%d,%lld\n",
                    static_cast<long long>(e.a),e.cycle,e.index,e.count,e.t0,e.t1,
                    e.ok?1:0,static_cast<long long>(frequency.QuadPart));
                break;
            }
            }
        }
        if (dropped) {
            // Side-channel warning: never contaminate machine-readable CSV rows.
            OutputDebugStringA("TASK DIAG-45: async diagnostic queue overflow\n");
        }
    }
    FILE* cpu=nullptr;FILE* cycles=nullptr;FILE* calls=nullptr;
    LARGE_INTEGER frequency{};
    std::unique_ptr<std::array<entry,65536>> queue;
    std::mutex mutex;std::condition_variable cv;std::thread writer;
    size_t head=0,tail=0,used=0;unsigned long long dropped=0;bool stop=false;
};
}
