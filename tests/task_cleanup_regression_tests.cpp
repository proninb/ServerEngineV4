#include "project/task/runtime/task_manager.hpp"
#include "project/task/runtime/testing/task_process_faults.hpp"
#include "task_cleanup_fault_shared.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
using namespace cw::server;
using namespace cw::server::task_runtime;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAILED line " << __LINE__ << ": " #x "\n"; std::abort(); } } while(false)
int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 2);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CHECK(job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    CHECK(AssignProcessToJobObject(job, GetCurrentProcess()));
    const std::wstring name = L"Local\\TaskCleanupRegression_" + std::to_wstring(GetCurrentProcessId());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(task_cleanup_fault_state), name.c_str());
    CHECK(mapping && GetLastError() != ERROR_ALREADY_EXISTS);
    auto* state = static_cast<task_cleanup_fault_state*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(task_cleanup_fault_state)));
    CHECK(state && SetEnvironmentVariableW(L"SEV4_CLEANUP_TEST_MAPPING", name.c_str()));
    task_configuration config; task_definition t;
    t.id = "Task0"; t.executable = std::filesystem::absolute(argv[1]).string(); config.tasks.push_back(t);
    manager runtime; std::string error;
    const auto baseline = task_ipc_snapshot();
    CHECK(_putenv_s("TASK_PERF52_NO_BARRIER", "0") == 0);
    state->hang_done = 1;
    CHECK(runtime.start(config, std::filesystem::current_path(), 100, error));
    HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(state->pid));
    CHECK(child && runtime.tick(1, false, error));
    const auto owned = task_ipc_snapshot();
    CHECK(owned.handles > baseline.handles && owned.views > baseline.views);
    testing::deny_termination.store(true);
    CHECK(!runtime.stop(error));
    CHECK(runtime.active() && state->done == 1 && WaitForSingleObject(child, 0) == WAIT_TIMEOUT);
    CHECK(task_ipc_snapshot().handles == owned.handles && task_ipc_snapshot().views == owned.views);
    // A second UNLOAD-like cleanup must fail closed with ownership intact.
    CHECK(!runtime.stop(error)); CHECK(runtime.active());
    CHECK(!runtime.terminate()); CHECK(runtime.active());
    CHECK(!runtime.start(config, std::filesystem::current_path(), 100, error));
    CHECK(!runtime.tick(2, false, error));
    CHECK(WaitForSingleObject(child, 0) == WAIT_TIMEOUT);
    CHECK(task_ipc_snapshot().handles == owned.handles && task_ipc_snapshot().views == owned.views);
    testing::deny_termination.store(false);
    CHECK(runtime.stop(error)); CHECK(!runtime.active());
    CHECK(WaitForSingleObject(child, 0) == WAIT_OBJECT_0); CloseHandle(child);
    CHECK(task_ipc_snapshot().handles == baseline.handles && task_ipc_snapshot().views == baseline.views);
    std::cout << "PASS failed STOP retains child/IPC, rejects restart/dispatch, retries cleanup\n";
    // Same manager can be reused after successful cleanup.
    state->hang_done = 0;
    CHECK(runtime.start(config, std::filesystem::current_path(), 100, error));
    CHECK(runtime.tick(1, false, error)); CHECK(runtime.stop(error));
    CHECK(_putenv_s("TASK_PERF52_NO_BARRIER", "1") == 0);
    CHECK(runtime.start(config, std::filesystem::current_path(), 100, error));
    std::size_t index = 0;
    CHECK(!runtime.request_kill("Task0", index, error));
    CHECK(error.find("unsupported in NO_BARRIER") != std::string::npos);
    CHECK(!runtime.restart_one("Task0", index, error));
    error.clear(); CHECK(runtime.tick(1, false, error)); CHECK(runtime.quiesce(error));
    const LONG calls = state->calls;
    CHECK(!runtime.request_kill("Task0", index, error));
    error.clear(); CHECK(runtime.tick(2, false, error)); CHECK(runtime.quiesce(error));
    CHECK(state->calls == calls + 1 && runtime.active_count() == 1);
    CHECK(runtime.stop(error));
    CHECK(task_ipc_snapshot().handles == baseline.handles && task_ipc_snapshot().views == baseline.views);
    CHECK(SetEnvironmentVariableW(L"SEV4_CLEANUP_TEST_MAPPING", nullptr));
    CHECK(_putenv_s("TASK_PERF52_NO_BARRIER", "0") == 0);
    UnmapViewOfFile(state); CloseHandle(mapping);
    std::cout << "PASS NO_BARRIER rejects individual controls and keeps ticking\n";
}
