/* ServerEngineV4 timing E2E Task fixture. C++20, one external process per Task. */
#include <SimTask.hpp>
#include <Windows.h>
#include <cstdio>

#ifndef TASK_ID
#define TASK_ID 0
#endif
#ifndef TASK_DELAY_US
#define TASK_DELAY_US 0
#endif

namespace {
void record(const char* event) {
    char path[32768]{};
    const DWORD length = GetEnvironmentVariableA("TASK_TIMING_E2E_LOG", path, sizeof(path));
    if (length == 0 || length >= sizeof(path)) return;
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[128]{};
    const int n = std::snprintf(line, sizeof(line), "%d %s %lu\n", TASK_ID, event,
                                static_cast<unsigned long>(GetCurrentProcessId()));
    if (n > 0 && n < static_cast<int>(sizeof(line))) {
        DWORD written = 0;
        (void)WriteFile(file, line, static_cast<DWORD>(n), &written, nullptr);
    }
    CloseHandle(file);
}

void delay() {
    if constexpr (TASK_DELAY_US > 0) {
        LARGE_INTEGER frequency{}, start{}, now{};
        if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&start)) return;
        const auto ticks = (frequency.QuadPart * TASK_DELAY_US) / 1000000LL;
        do {
            YieldProcessor();
            QueryPerformanceCounter(&now);
        } while (now.QuadPart - start.QuadPart < ticks);
    }
}
}

void TASKINIT() { record("INIT"); }
int TASKMAIN() {
    if (!SimTask::SharedMemory() || SimTask::SharedMemorySize() == 0) {
        record("NO_SHM");
        return TASK_FAIL;
    }
    delay();
    return TASK_OK;
}
void TASKDONE() { record("DONE"); }
