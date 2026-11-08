/* One real SimTask process, selectable role. Shared counters prove survivor execution. */
#include <SimTask.hpp>
#include <Windows.h>

#ifndef TASK_ID
#define TASK_ID 0
#endif

struct shared_counters {
    volatile LONG init[2]{};
    volatile LONG entered[2]{};
    volatile LONG main[2]{};
    volatile LONG done[2]{};
};

static shared_counters* shared() {
    static HANDLE mapping = nullptr;
    static shared_counters* view = nullptr;
    if (view) return view;
    wchar_t name[256]{};
    if (!GetEnvironmentVariableW(L"SEV4_SINGLE_TASK_TEST_MAP", name, 256)) return nullptr;
    mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!mapping) return nullptr;
    view = static_cast<shared_counters*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shared_counters)));
    return view;
}

void TASKINIT() {
    if (auto* counters = shared()) InterlockedIncrement(&counters->init[TASK_ID]);
}

int TASKMAIN() {
    if (auto* counters = shared()) InterlockedIncrement(&counters->entered[TASK_ID]);
    if (TASK_ID == 0 && GetEnvironmentVariableW(L"SEV4_SINGLE_TASK_HANG", nullptr, 0))
        Sleep(INFINITE); // KILL_TASK must interrupt this without killing the dependent.
    auto* counters = shared();
    if (!counters) return TASK_FAIL;
    InterlockedIncrement(&counters->main[TASK_ID]);
    return TASK_OK;
}

void TASKDONE() {
    if (auto* counters = shared()) InterlockedIncrement(&counters->done[TASK_ID]);
}
