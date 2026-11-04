#pragma once
// Diagnostic-only QPC trace. No changes to SHM ABI or Task protocol.
#include <Windows.h>
#include <cstdio>
#include <string>
#include <cwchar>

namespace cw::server::task_runtime::perf07 {
struct trace {
    FILE* file = nullptr;
    trace() = default;
    trace(const trace&) = delete;
    trace& operator=(const trace&) = delete;
    ~trace() { close(); }
    void close() noexcept { if(file) { std::fflush(file); std::fclose(file); file=nullptr; } }
    void open(const std::wstring& mapping_name, const wchar_t* side) {
        close();
        wchar_t directory[32768]{};
        DWORD n=GetEnvironmentVariableW(L"TASK_SHM_PERF07_DIR",directory,32768);
        if(!n || n>=32768) return;
        CreateDirectoryW(directory,nullptr);
        std::wstring safe=mapping_name;
        for(auto& c:safe) if(c==L'\\' || c==L'/' || c==L':' || c==L' ' || c==L'"') c=L'_';
        std::wstring path=std::wstring(directory)+L"\\"+side+L"_"+safe+L".csv";
        if(_wfopen_s(&file,path.c_str(),L"wb") || !file) return;
        setvbuf(file,nullptr,_IOFBF,1024*1024);
        std::fprintf(file,"seq,cycle,qpc0,qpc1,qpc2,qpc3,ok,frequency\n");
    }
    void write(LONG seq, unsigned long long cycle, long long t0,long long t1,long long t2,long long t3,bool ok) noexcept {
        if(!file) return;
        LARGE_INTEGER freq{}; QueryPerformanceFrequency(&freq);
        std::fprintf(file,"%ld,%llu,%lld,%lld,%lld,%lld,%d,%lld\n",seq,cycle,t0,t1,t2,t3,ok?1:0,static_cast<long long>(freq.QuadPart));
    }
};
inline long long qpc() noexcept { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart; }
}
