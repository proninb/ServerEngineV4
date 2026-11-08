/* Exercise KILL_TASK/START_TASK internals with a hung dependency and a live survivor. */
#include "project/task/runtime/task_manager.hpp"
#include "project/task/task_configuration.hpp"
#include <Windows.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>

using namespace cw::server;
using namespace cw::server::task_runtime;

struct shared_counters {
    volatile LONG init[2]{};
    volatile LONG entered[2]{};
    volatile LONG main[2]{};
    volatile LONG done[2]{};
};

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAILED line " << __LINE__ << ": " << #condition << "\n"; \
    std::abort(); } } while (false)

int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 3);
    // Attach this test process to a kill-on-close job BEFORE starting any Task.
    // Inherited membership kills children if the test aborts or CTest times out.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    CHECK(job != nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CHECK(SetInformationJobObject(job, JobObjectExtendedLimitInformation,
        &limits, sizeof(limits)));
    CHECK(AssignProcessToJobObject(job, GetCurrentProcess()));
    // Do not CloseHandle(job) before process exit: parent is also in this job.
    const std::wstring name = L"Local\\SEV4_SingleTask_" +
        std::to_wstring(GetCurrentProcessId());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, sizeof(shared_counters), name.c_str());
    CHECK(mapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS);
    auto* counters = static_cast<shared_counters*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shared_counters)));
    CHECK(counters != nullptr);
    *counters = {};
    CHECK(SetEnvironmentVariableW(L"SEV4_SINGLE_TASK_TEST_MAP", name.c_str()));
    CHECK(SetEnvironmentVariableW(L"SEV4_SINGLE_TASK_HANG", L"1"));

    task_configuration config;
    task_definition input;
    input.id = "Input";
    input.executable = std::filesystem::path(argv[1]).string();
    input.settings.calls = 1;
    input.settings.cycles = 1;
    input.settings.starting_from_cycle = 1;
    config.tasks.push_back(input);
    task_definition dependent;
    dependent.id = "Controller";
    dependent.executable = std::filesystem::path(argv[2]).string();
    dependent.dependencies = {"Input"};
    dependent.settings.calls = 1;
    dependent.settings.cycles = 1;
    dependent.settings.starting_from_cycle = 1;
    config.tasks.push_back(dependent);

    manager tasks;
    std::string error;
    runtime_shm table;
    CHECK(table.create(config, error));
    tasks.set_system_table(&table);
    CHECK(tasks.start(config, std::filesystem::current_path(), 5000, error));
    CHECK(tasks.active_count() == 2);
    CHECK(counters->init[0] == 1 && counters->init[1] == 1);

    auto future = std::async(std::launch::async, [&tasks] {
        std::string tick_error;
        return tasks.tick(1, false, tick_error);
    });
    // Wait for the exact start of the blocked TASKMAIN instead of guessing a delay.
    const auto limit = GetTickCount64() + 3000;
    while (counters->entered[0] == 0 && GetTickCount64() < limit) Sleep(1);
    CHECK(counters->entered[0] >= 1);
    std::size_t killed = static_cast<std::size_t>(-1);
    CHECK(tasks.request_kill("Input", killed, error));
    CHECK(killed == 0);
    CHECK(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(future.get()); // The dependent Controller still completes this cycle.
    tasks.finish_kill(killed);
    CHECK(tasks.active_count() == 1);
    CHECK(counters->main[1] >= 1);
    CHECK(tasks.tick(2, false, error));
    CHECK(counters->main[1] >= 2); // No dependency block from killed Input.
    // Independent reader validates per-Task status in the named system table.
    const auto mutex_name = table.mapping_name() + L"_mutex";
    HANDLE gate = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE,
        FALSE, mutex_name.c_str());
    HANDLE system_map = OpenFileMappingW(FILE_MAP_READ, FALSE,
        table.mapping_name().c_str());
    CHECK(gate && system_map);
    const auto* view = static_cast<const std::byte*>(MapViewOfFile(
        system_map, FILE_MAP_READ, 0, 0, 0));
    CHECK(view);
    // This test drives manager::tick() directly, without server::run().
    // In production, server::run() calls cycle_completed() after LOCKSTEP,
    // and that method periodically publishes this SHM snapshot. The standalone
    // fixture must explicitly publish before verifying the independent reader.
    table.publish();
    CHECK(WaitForSingleObject(gate, 1000) == WAIT_OBJECT_0);
    const auto* header = reinterpret_cast<const runtime_table*>(view);
    CHECK(header->task_count == 2);
    const auto* records = reinterpret_cast<const task_record*>(view + header->tasks_offset);
    const auto published_generation = header->generation;
    const auto published_input_state = records[0].diagnostics.state;
    const auto published_completed = records[1].diagnostics.completed;
    CHECK(ReleaseMutex(gate));
    if (published_input_state != static_cast<std::uint32_t>(runtime_task_state::stopped) ||
        published_completed < 2) {
        std::cerr << "Published SHM mismatch: generation=" << published_generation
            << " Input state=" << published_input_state
            << " Controller completed=" << published_completed
            << " direct Controller calls=" << counters->main[1] << "\n";
    }
    CHECK(published_input_state == static_cast<std::uint32_t>(runtime_task_state::stopped));
    CHECK(published_completed >= 2);
    std::uint64_t next_cycle = 3;
    UnmapViewOfFile(view);
    CloseHandle(system_map);
    CloseHandle(gate);

    CHECK(SetEnvironmentVariableW(L"SEV4_SINGLE_TASK_HANG", nullptr));
    std::size_t restarted = static_cast<std::size_t>(-1);
    CHECK(tasks.restart_one("Input", restarted, error));
    CHECK(restarted == 0 && tasks.active_count() == 2);
    CHECK(counters->init[0] == 2); // TASKINIT called again only for Input.
    CHECK(counters->init[1] == 1); // Controller was never restarted.
    CHECK(tasks.tick(next_cycle++, false, error));
    CHECK(counters->main[0] >= 1 && counters->main[1] >= 3);

    CHECK(tasks.request_kill("Input", killed, error));
    tasks.finish_kill(killed);
    CHECK(tasks.active_count() == 1);
    CHECK(tasks.request_kill("Controller", killed, error));
    tasks.finish_kill(killed);
    CHECK(tasks.active_count() == 0);
    tasks.terminate();
    table.close();
    UnmapViewOfFile(counters);
    CloseHandle(mapping);
    return 0;
}
