// Isolated Windows SHM + Events task-control latency probe. No ServerEngineV4 changes.
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>
#include <numeric>
#include <cmath>

struct alignas(64) Control {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint64_t cycle;
    std::uint32_t calls;
    std::uint32_t status;
    std::uint64_t completed_cycle;
};
static constexpr std::uint32_t magic = 0x32534b54; // TKS2
static constexpr std::uint32_t version = 2;
static std::wstring name(const std::wstring& root, const wchar_t* suffix, int i) {
    return root + L"_" + suffix + L"_" + std::to_wstring(i);
}
static void die(const char* what) { std::fprintf(stderr, "%s: Win32=%lu\n", what, GetLastError()); std::exit(2); }
static double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil(p * values.size()) - 1);
    return values[(std::min)(index, values.size()-1)];
}
static int child(const std::wstring& root, int index) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name(root,L"map",index).c_str());
    if (!mapping) die("OpenFileMapping");
    auto* control = static_cast<Control*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS,0,0,sizeof(Control)));
    if (!control) die("MapViewOfFile");
    HANDLE run = OpenEventW(SYNCHRONIZE,FALSE,name(root,L"run",index).c_str());
    HANDLE done = OpenEventW(EVENT_MODIFY_STATE,FALSE,name(root,L"done",index).c_str());
    if (!run || !done) die("OpenEvent");
    if (control->magic != magic || control->version != version) return 3;
    for (;;) {
        if (WaitForSingleObject(run, INFINITE) != WAIT_OBJECT_0) return 4;
        const auto cycle = control->cycle;
        if (cycle == UINT64_MAX) break;
        // Stand-in for TASKMAIN: no user work, measure control transport alone.
        control->status = 0;
        control->completed_cycle = cycle;
        if (!SetEvent(done)) die("SetEvent(done)");
    }
    UnmapViewOfFile(control); CloseHandle(mapping); CloseHandle(run); CloseHandle(done);
    return 0;
}
struct Worker {
    HANDLE mapping = nullptr, run = nullptr, done = nullptr, process = nullptr;
    Control* control = nullptr;
};
static int parent(int count, int warmup, int samples, DWORD timeout) {
    if (count < 1 || count > 60 || warmup < 0 || samples < 1) return 2;
    const std::wstring root = L"Local\\SEV4Probe_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) die("GetModuleFileName");
    std::vector<Worker> workers(count);
    std::vector<HANDLE> done_handles;
    done_handles.reserve(count);
    for (int i=0;i<count;++i) {
        auto& w=workers[i];
        w.mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,4096,name(root,L"map",i).c_str());
        if (!w.mapping) die("CreateFileMapping");
        w.control=static_cast<Control*>(MapViewOfFile(w.mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Control)));
        if (!w.control) die("MapViewOfFile");
        w.control->magic=magic; w.control->version=version;
        w.run=CreateEventW(nullptr,FALSE,FALSE,name(root,L"run",i).c_str());
        w.done=CreateEventW(nullptr,FALSE,FALSE,name(root,L"done",i).c_str());
        if (!w.run || !w.done) die("CreateEvent");
        std::wstring command=L"\"" + std::wstring(executable) + L"\" --child \"" + root + L"\" " + std::to_wstring(i);
        std::vector<wchar_t> mutable_cmd(command.begin(),command.end()); mutable_cmd.push_back(0);
        STARTUPINFOW startup{}; startup.cb=sizeof(startup);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(executable,mutable_cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi)) die("CreateProcess");
        w.process=pi.hProcess; CloseHandle(pi.hThread);
        done_handles.push_back(w.done);
    }
    std::vector<double> latency;
    latency.reserve(samples);
    const int total=warmup+samples;
    for (int iteration=0;iteration<total;++iteration) {
        const auto cycle=static_cast<std::uint64_t>(iteration+1);
        const auto begin=std::chrono::steady_clock::now();
        for (auto& w:workers) { w.control->cycle=cycle; w.control->calls=1; w.control->status=UINT32_MAX; }
        std::atomic_thread_fence(std::memory_order_release);
        for (auto& w:workers) if (!SetEvent(w.run)) die("SetEvent(run)");
        const DWORD result=WaitForMultipleObjects(static_cast<DWORD>(done_handles.size()),done_handles.data(),TRUE,timeout);
        if (result != WAIT_OBJECT_0) {
            std::fprintf(stderr,"Barrier failure at cycle %llu: result=%lu\n",static_cast<unsigned long long>(cycle),result);
            for (auto& w:workers) TerminateProcess(w.process,1);
            return 5;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        for (const auto& w:workers) if (w.control->completed_cycle!=cycle || w.control->status!=0) {
            std::fprintf(stderr,"Sequence/status mismatch at cycle %llu\n",static_cast<unsigned long long>(cycle));
            return 6;
        }
        const auto end=std::chrono::steady_clock::now();
        if (iteration>=warmup) latency.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
    }
    for (auto& w:workers) { w.control->cycle=UINT64_MAX; SetEvent(w.run); }
    for (auto& w:workers) {
        if (WaitForSingleObject(w.process,timeout)!=WAIT_OBJECT_0) TerminateProcess(w.process,1);
        CloseHandle(w.process); CloseHandle(w.run); CloseHandle(w.done);
        UnmapViewOfFile(w.control); CloseHandle(w.mapping);
    }
    const double sum=std::accumulate(latency.begin(),latency.end(),0.0);
    std::printf("count=%d samples=%d mean_ms=%.6f p50_ms=%.6f p95_ms=%.6f p99_ms=%.6f max_ms=%.6f\n",
        count,samples,sum/latency.size(),percentile(latency,.50),percentile(latency,.95),percentile(latency,.99),*std::max_element(latency.begin(),latency.end()));
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    if (argc==4 && std::wstring(argv[1])==L"--child") return child(argv[2],_wtoi(argv[3]));
    if (argc==4) return parent(_wtoi(argv[1]),_wtoi(argv[2]),_wtoi(argv[3]),5000);
    std::fprintf(stderr,"Usage: control_probe.exe <tasks:1..60> <warmup_cycles> <measured_cycles>\n");
    return 2;
}
