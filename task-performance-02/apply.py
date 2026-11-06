from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
header=root/'server_engine/project/task/runtime/task_perf_probe.hpp'
if header.exists():
    raise SystemExit('Already installed: '+str(header))
header.write_text(r'''#pragma once
/* Optional diagnostic timing; no runtime or SHM ABI changes. */
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
namespace cw::server::task_runtime::perf {
using clock = std::chrono::steady_clock;
struct entry { const char* stage; unsigned long long cycle; double microseconds; };
class recorder {
public:
    recorder() {
        char path[32768]{};
        const DWORD n = GetEnvironmentVariableA("TASK_PERF02_CSV", path, sizeof(path));
        if (n && n < sizeof(path)) destination = path;
    }
    ~recorder() {
        if (destination.empty()) return;
        FILE* file = nullptr;
        if (fopen_s(&file, destination.c_str(), "a") != 0 || !file) return;
        for (const auto& item : samples)
            std::fprintf(file, "%s,%llu,%.3f\n", item.stage, item.cycle, item.microseconds);
        std::fclose(file);
    }
    void add(const char* stage, unsigned long long cycle, clock::time_point start) {
        if (destination.empty()) return;
        const auto elapsed = std::chrono::duration<double, std::micro>(clock::now()-start).count();
        std::lock_guard<std::mutex> lock(mutex);
        samples.push_back({stage,cycle,elapsed});
    }
    bool enabled() const noexcept { return !destination.empty(); }
private:
    std::string destination;
    std::mutex mutex;
    std::vector<entry> samples;
};
inline recorder& instance() { static recorder value; return value; }
inline void record(const char* stage, unsigned long long cycle, clock::time_point start) {
    instance().add(stage, cycle, start);
}
}
''',encoding='utf-8')

def edit(rel, old, new):
    p=root/rel
    data=p.read_text(encoding='utf-8-sig')
    if old not in data: raise SystemExit('Anchor not found: '+rel+' '+repr(old[:90]))
    p.write_text(data.replace(old,new,1),encoding='utf-8')

edit('server_engine/project/task/runtime/task_process.cpp','#include "task_process.hpp"','#include "task_process.hpp"\n#include "task_perf_probe.hpp"')
edit('server_engine/project/task/runtime/task_process.cpp','bool process::exchange(message request,command expected,std::string& error) {','bool process::exchange(message request,command expected,std::string& error) {\n    const auto perf_start = perf::clock::now();')
edit('server_engine/project/task/runtime/task_process.cpp','    return receive(expected,request.cycle,error);','    const bool result = receive(expected,request.cycle,error);\n    if (request.action == command::run) perf::record("ipc_roundtrip", request.cycle, perf_start);\n    return result;')
edit('server_engine/project/task/runtime/task_manager.cpp','#include "task_manager.hpp"','#include "task_manager.hpp"\n#include "task_perf_probe.hpp"')
edit('server_engine/project/task/runtime/task_manager.cpp','bool manager::tick(std::uint64_t cycle, bool frozen, std::string& error) {','bool manager::tick(std::uint64_t cycle, bool frozen, std::string& error) {\n    const auto perf_start = perf::clock::now();')
edit('server_engine/project/task/runtime/task_manager.cpp','        bool success = true;\n        for (std::size_t i = 0; i < pending.size(); ++i) {','        const auto barrier_start = perf::clock::now();\n        bool success = true;\n        for (std::size_t i = 0; i < pending.size(); ++i) {')
edit('server_engine/project/task/runtime/task_manager.cpp','        // All in-flight requests are finished before failure is reported.','        perf::record("barrier_wait", cycle, barrier_start);\n        // All in-flight requests are finished before failure is reported.')
edit('server_engine/project/task/runtime/task_manager.cpp','    return true;\n}\n\nbool manager::stop','    perf::record("manager_tick", cycle, perf_start);\n    return true;\n}\n\nbool manager::stop')
edit('server_engine/server.cpp','#include "server.hpp"','#include "server.hpp"\n#include "project/task/runtime/task_perf_probe.hpp"')
edit('server_engine/server.cpp','                    ++runtime_tick;','                    const auto perf_tick_start = task_runtime::perf::clock::now();\n                    static auto perf_previous_tick = perf_tick_start;\n                    task_runtime::perf::record("cycle_interval", runtime_tick, perf_previous_tick);\n                    perf_previous_tick = perf_tick_start;\n                    ++runtime_tick;')
edit('server_engine/server.cpp','                    next_task_tick = std::chrono::steady_clock::now() +','                    task_runtime::perf::record("server_tick", runtime_tick, perf_tick_start);\n                    next_task_tick = std::chrono::steady_clock::now() +')
print('TASK-PERFORMANCE-02 instrumentation installed (opt-in TASK_PERF02_CSV).')
