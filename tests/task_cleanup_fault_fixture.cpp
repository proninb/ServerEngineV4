#include "task_cleanup_fault_shared.hpp"
#include "task_host.hpp"
namespace {
HANDLE mapping = nullptr;
task_cleanup_fault_state* state = nullptr;
bool init() {
    wchar_t name[256]{};
    if (!GetEnvironmentVariableW(L"SEV4_CLEANUP_TEST_MAPPING", name, 256)) return false;
    mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!mapping) return false;
    state = static_cast<task_cleanup_fault_state*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(task_cleanup_fault_state)));
    if (!state) return false;
    InterlockedExchange(&state->pid, static_cast<LONG>(GetCurrentProcessId()));
    return true;
}
bool main_call() { InterlockedIncrement(&state->calls); return true; }
void done() {
    InterlockedIncrement(&state->done);
    if (InterlockedCompareExchange(&state->hang_done, 0, 0)) Sleep(INFINITE);
    UnmapViewOfFile(state); state = nullptr;
    CloseHandle(mapping); mapping = nullptr;
}
}
int main() { return cw::server::task_runtime::run_host({init, main_call, done}); }
