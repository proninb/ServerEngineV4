#include "task_host.hpp"
#include "task_control.hpp"
#include "task_trace.hpp"
#include "../../../fixed_shared_memory.hpp"
#include <Windows.h>
#include <cstring>
#include <limits>
#include <string>
#include <filesystem>
#include <system_error>
#include <cwchar>
#include <cstdio>
namespace cw::server::task_runtime {
namespace {
cw::server::fixed_shared_memory attached_mapping;

// TASK_DIAG_29_CPU: diagnostic QPC wall and thread CPU time per call.

// TASK_DIAG_30_CYCLES: thread cycle counts are raw hardware counts, NOT microseconds.
struct cycle_trace {
    FILE* file = nullptr;
    explicit cycle_trace(const std::wstring& name) {
        wchar_t directory[32768]{};
        const DWORD n = GetEnvironmentVariableW(L"TASK_SHM_PERF07_DIR", directory, 32768);
        if (!n || n >= 32768) return;
        CreateDirectoryW(directory, nullptr);
        std::wstring safe = name;
        for (auto& c : safe) if (c == L'\\' || c == L'/' || c == L':' || c == L' ') c = L'_';
        const auto path = std::wstring(directory) + L"\\cycles_" + safe + L".csv";
        if (_wfopen_s(&file, path.c_str(), L"wb") || !file) return;
        std::fprintf(file, "cycle,call_index,thread_cycles,cycle_valid,ok\n");
        std::fflush(file);
    }
    ~cycle_trace() { if (file) std::fclose(file); }
    static bool sample(ULONG64& value) {
        return QueryThreadCycleTime(GetCurrentThread(), &value) != 0;
    }
    void record(unsigned long long cycle, unsigned index, ULONG64 begin,
                ULONG64 end, bool valid, bool ok) {
        if (!file) return;
        const bool good = valid && end >= begin;
        std::fprintf(file, "%llu,%u,%llu,%d,%d\n", cycle, index,
            static_cast<unsigned long long>(good ? end - begin : 0),
            good ? 1 : 0, ok ? 1 : 0);
        std::fflush(file);
    }
};

struct cpu_trace {
    FILE* file = nullptr;
    LARGE_INTEGER frequency{};
    explicit cpu_trace(const std::wstring& name) {
        wchar_t directory[32768]{};
        const DWORD n = GetEnvironmentVariableW(L"TASK_SHM_PERF07_DIR", directory, 32768);
        if (!n || n >= 32768) return;
        if (!QueryPerformanceFrequency(&frequency) || !frequency.QuadPart) return;
        CreateDirectoryW(directory, nullptr);
        std::wstring safe = name;
        for (auto& c : safe) if (c == L'\\' || c == L'/' || c == L':' || c == L' ') c = L'_';
        const auto path = std::wstring(directory) + L"\\cpu_" + safe + L".csv";
        if (_wfopen_s(&file, path.c_str(), L"wb") || !file) return;
        std::fprintf(file, "cycle,call_index,wall_us,cpu_us,off_cpu_us,cpu_valid,ok\n");
        std::fflush(file);
    }
    ~cpu_trace() { if (file) std::fclose(file); }
    static bool sample(ULONGLONG& ticks) {
        FILETIME a{}, b{}, k{}, u{};
        if (!GetThreadTimes(GetCurrentThread(), &a, &b, &k, &u)) return false;
        ULARGE_INTEGER x{}, y{};
        x.LowPart = k.dwLowDateTime; x.HighPart = k.dwHighDateTime;
        y.LowPart = u.dwLowDateTime; y.HighPart = u.dwHighDateTime;
        ticks = x.QuadPart + y.QuadPart;
        return true;
    }
    void record(unsigned long long cycle, unsigned index, LONGLONG t0,
                LONGLONG t1, ULONGLONG c0, ULONGLONG c1, bool valid, bool ok) {
        if (!file) return;
        const double wall = double(t1 - t0) * 1000000.0 / double(frequency.QuadPart);
        const double cpu = valid && c1 >= c0 ? double(c1 - c0) / 10.0 : 0.0;
        std::fprintf(file, "%llu,%u,%.3f,%.3f,%.3f,%d,%d\n",
            cycle, index, wall, cpu, valid ? (wall > cpu ? wall - cpu : 0.0) : 0.0,
            valid ? 1 : 0, ok ? 1 : 0);
        std::fflush(file);
    }
};

}
void* shared_memory_data() noexcept { return attached_mapping.data(); }
std::size_t shared_memory_size() noexcept { return attached_mapping.size(); }
int run_host(callbacks cb) {
    if(!cb.init || !cb.main || !cb.done) return 2;
    // The host library owns main() and passes the mapping name via argv.
    const wchar_t* command_line=GetCommandLineW();
    const std::wstring args(command_line?command_line:L"");
    const auto marker=args.find(L"--serverengine-task-shm");
    if(marker==std::wstring::npos) return 3;
    auto pos=args.find_first_not_of(L" \t",marker+wcslen(L"--serverengine-task-shm"));
    if(pos==std::wstring::npos) return 3;
    if(args[pos]==L'"') ++pos;
    auto end=args.find_first_of(L"\" \t",pos);
    const auto name=args.substr(pos,end==std::wstring::npos?std::wstring::npos:end-pos);
    if(name.empty()) return 3;
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
    if(!mapping) return 4;
    auto* control=static_cast<task_control*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(task_control)));
    if(!control) { CloseHandle(mapping); return 4; }
    const auto cmd_name=name+L"_command",done_name=name+L"_completion";
    HANDLE command_event=OpenEventW(SYNCHRONIZE,FALSE,cmd_name.c_str());
    HANDLE completion_event=OpenEventW(EVENT_MODIFY_STATE,FALSE,done_name.c_str());
    if(!command_event || !completion_event || control->signature!=control_magic || control->version!=control_version) {
        if(command_event) CloseHandle(command_event);
        if(completion_event) CloseHandle(completion_event);
        UnmapViewOfFile(control); CloseHandle(mapping); return 4;
    }
    perf07::trace perf_trace;
    perf_trace.open(name,L"child");
    cpu_trace cpu(name);
    cycle_trace cycles(name);
    // Diagnostic-only per-invocation trace; no protocol or SHM ABI changes.
    FILE* call_file = nullptr;
    wchar_t trace_directory[32768]{};
    DWORD trace_length = GetEnvironmentVariableW(L"TASK_SHM_PERF07_DIR", trace_directory, 32768);
    if (trace_length && trace_length < 32768) {
        std::error_code ec;
        std::filesystem::create_directories(trace_directory,ec);
        std::wstring safe = name;
        for (auto& c : safe) if (c == L'\\' || c == L'/' || c == L':' || c == L' ' || c == L'"') c = L'_';
        const auto path = std::wstring(trace_directory) + L"\\calls_" + safe + L".csv";
        if (_wfopen_s(&call_file, path.c_str(), L"wb") == 0 && call_file) {
            setvbuf(call_file, nullptr, _IOLBF, 4096);
            std::fprintf(call_file, "seq,cycle,call_index,call_count,start_qpc,end_qpc,ok,frequency\n");
        }
    }
    // Per-call probes and CSV writes are enabled only by TASK_SHM_PERF07_DIR.
    const bool diagnostics = trace_length && trace_length < 32768;
    int result=0;
    LONG last=0;
    bool initialized=false;
    while(true) {
        if(WaitForSingleObject(command_event,INFINITE)!=WAIT_OBJECT_0) { result=5; break; }
        const LONG seq=InterlockedCompareExchange(&control->request_sequence,0,0);
        if(seq==last) continue;
        last=seq;
        const auto trace_received=perf07::qpc();
        const message req=control->request;
        long long trace_main_start=0,trace_main_end=0;
        command response=command::error;
        std::uint32_t status=0;
        bool exit_after=false;
        if(req.signature!=magic || req.protocol_version!=version) { status=4; exit_after=true; }
        else if(req.action==command::init && !initialized) {
            const auto& a=control->attachment;
            if(a.name[0]) {
                if(a.name[128]!='\0' || !std::memchr(a.name,'\0',sizeof(a.name)) || !a.size || !a.address ||
                   a.size>static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) ||
                   a.address>static_cast<std::uint64_t>((std::numeric_limits<std::uintptr_t>::max)()) ||
                   attached_mapping.open(a.name,static_cast<std::size_t>(a.size),
                       static_cast<std::uintptr_t>(a.address))!=cw::server::fixed_shared_memory_result::success) status=12;
            }
            if(!status) { try { initialized=cb.init(); } catch(...) { initialized=false; } if(!initialized) status=1; }
            response=status?command::error:command::ready;
            exit_after=status!=0;
        } else if(req.action==command::run && initialized && req.calls) {
            trace_main_start=perf07::qpc();
            unsigned cpu_call_index = 0;
            bool ok=true;
            try {
                for (std::uint32_t i = 0; i < req.calls; ++i) {
                    const auto call_start = call_file ? perf07::qpc() : 0;
                    bool call_ok = false;
                    try { call_ok = ([&]() -> bool {
                const unsigned index = ++cpu_call_index;
                LARGE_INTEGER t0{}, t1{};
                ULONGLONG c0 = 0, c1 = 0;
                const bool valid0 = diagnostics && cpu.sample(c0);
                ULONG64 cycles0 = 0, cycles1 = 0;
                const bool cycles_valid0 = diagnostics && cycles.sample(cycles0);
                if (diagnostics) QueryPerformanceCounter(&t0);
                bool call_ok = false;
                try { call_ok = cb.main(); }
                catch (...) {
                    if (diagnostics) QueryPerformanceCounter(&t1);
                    const bool valid1 = diagnostics && cpu.sample(c1);
                const bool cycles_valid1 = diagnostics && cycles.sample(cycles1);
                    if (diagnostics) cpu.record(req.cycle, index, t0.QuadPart, t1.QuadPart,
                               c0, c1, valid0 && valid1, false);
                    if (diagnostics) cycles.record(req.cycle, index, cycles0, cycles1,
                                  cycles_valid0 && cycles_valid1, false);
                    throw;
                }
                if (diagnostics) QueryPerformanceCounter(&t1);
                const bool valid1 = diagnostics && cpu.sample(c1);
                const bool cycles_valid1 = diagnostics && cycles.sample(cycles1);
                if (diagnostics) cpu.record(req.cycle, index, t0.QuadPart, t1.QuadPart,
                           c0, c1, valid0 && valid1, call_ok);
                if (diagnostics) cycles.record(req.cycle, index, cycles0, cycles1,
                              cycles_valid0 && cycles_valid1, call_ok);
                return call_ok;
            }()); } catch (...) { call_ok = false; }
                    if (call_file) {
                        const auto call_end = perf07::qpc();
                        LARGE_INTEGER frequency{};
                        QueryPerformanceFrequency(&frequency);
                        std::fprintf(call_file, "%ld,%llu,%u,%u,%lld,%lld,%d,%lld\n",
                            static_cast<long>(seq), static_cast<unsigned long long>(req.cycle),
                            static_cast<unsigned>(i + 1), static_cast<unsigned>(req.calls),
                            call_start, call_end, call_ok ? 1 : 0,
                            static_cast<long long>(frequency.QuadPart));
                    }
                    if (call_file) std::fflush(call_file);
                    if (!call_ok) { ok = false; break; }
                }
            } catch (...) { ok = false; }
            trace_main_end=perf07::qpc();
            response=ok?command::done:command::error;
            status=ok?0:2;
            exit_after=!ok;
        } else if(req.action==command::stop && initialized) {
            try { cb.done(); response=command::stopped; }
            catch(...) { response=command::error; status=3; }
            exit_after=true;
        } else { status=4; exit_after=true; }
        control->response=message{magic,version,response,0,req.cycle,0,status};
        InterlockedExchange(&control->response_sequence,seq);
        const auto trace_signaled=perf07::qpc();
        if(req.action==command::run) perf_trace.write(seq,req.cycle,trace_received,trace_main_start,trace_main_end,trace_signaled,status==0);
        SetEvent(completion_event);
        if(exit_after) { result=status?static_cast<int>(status):0; break; }
    }
    if (call_file) { std::fflush(call_file); std::fclose(call_file); }
    CloseHandle(command_event); CloseHandle(completion_event);
    UnmapViewOfFile(control); CloseHandle(mapping);
    attached_mapping.reset();
    return result;
}
}
