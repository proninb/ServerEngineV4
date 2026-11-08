/* TCP E2E: Task 0 reports entry and never returns from TASKMAIN. */
#include <SimTask.hpp>
#include <Windows.h>
#include <cstdio>

namespace {
void record(const char* event) {
    char path[32768]{};
    const DWORD size = GetEnvironmentVariableA("TASK_TIMING_E2E_LOG", path, sizeof(path));
    if (size == 0 || size >= sizeof(path)) return;
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[128]{};
    const int count = std::snprintf(line, sizeof(line), "0 %s %lu\n", event,
                                    static_cast<unsigned long>(GetCurrentProcessId()));
    if (count > 0 && count < static_cast<int>(sizeof(line))) {
        DWORD written = 0;
        (void)WriteFile(file, line, static_cast<DWORD>(count), &written, nullptr);
    }
    CloseHandle(file);
}
}

void TASKINIT() { record("INIT"); }
int TASKMAIN() {
    if (!SimTask::SharedMemory() || SimTask::SharedMemorySize() == 0) {
        record("NO_SHM");
        return TASK_FAIL;
    }
    record("ENTER");
    Sleep(INFINITE);
    return TASK_OK;
}
void TASKDONE() { record("DONE"); }
