#pragma once
/* TASK-IPC-LATENCY-05: opt-in per-process QPC diagnostics; no protocol/SHM ABI changes. */
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
namespace cw::server::task_runtime::ipc_trace {
inline long long now() noexcept {
    LARGE_INTEGER v{};
    QueryPerformanceCounter(&v);
    return v.QuadPart;
}
inline long long frequency() noexcept {
    LARGE_INTEGER v{};
    QueryPerformanceFrequency(&v);
    return v.QuadPart;
}
inline bool enabled() noexcept {
    const char* value = std::getenv("TASK_IPC_TRACE_DIR");
    return value && *value;
}
inline void write(const char* role, unsigned long pid, unsigned long long cycle,
                  unsigned calls, long long t0, long long t1, long long t2, long long t3,
                  int status) {
    const char* directory = std::getenv("TASK_IPC_TRACE_DIR");
    if(!directory || !*directory) return;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::string path = std::string(directory) + "\\" + role + "_" + std::to_string(pid) + ".csv";
    FILE* file = nullptr;
    if(fopen_s(&file, path.c_str(), "a") != 0 || !file) return;
    std::fseek(file, 0, SEEK_END);
    if(std::ftell(file) == 0)
        std::fprintf(file, "pid,cycle,calls,t0,t1,t2,t3,qpc_frequency,status\n");
    std::fprintf(file, "%lu,%llu,%u,%lld,%lld,%lld,%lld,%lld,%d\n",
                 pid, cycle, calls, t0, t1, t2, t3, frequency(), status);
    std::fclose(file);
}
}
