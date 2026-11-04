#pragma once
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
