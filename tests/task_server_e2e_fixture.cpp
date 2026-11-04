/* Server-level Task callback probe; writes only to private test log files. */
#include <SimTask.hpp>
#include <Windows.h>
#include <cstdio>
#include <cstring>

#ifndef TASK_ID
#define TASK_ID 0
#endif

static void record(const char* event) {
    char path[32768]{};
    const DWORD count = GetEnvironmentVariableA("TASK_SERVER_E2E_LOG", path, sizeof(path));
    if (!count || count >= sizeof(path)) return;
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[96]{};
    const int n = std::snprintf(line, sizeof(line), "%d %s %lu\n", TASK_ID, event,
                                static_cast<unsigned long>(GetCurrentProcessId()));
    if (n > 0 && n < static_cast<int>(sizeof(line))) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(n), &written, nullptr);
    }
    CloseHandle(file);
}

void TASKINIT() { record("INIT"); }
int TASKMAIN() {
    if (SimTask::SharedMemory() == nullptr || SimTask::SharedMemorySize() == 0) {
        record("NO_SHM");
        return TASK_FAIL;
    }
    record("MAIN");
#ifdef TASK_FORCE_FAIL
    return TASK_FAIL;
#else
    return TASK_OK;
#endif
}
void TASKDONE() { record("DONE"); }
