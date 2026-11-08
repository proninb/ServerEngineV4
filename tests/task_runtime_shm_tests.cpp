#include "project/task/runtime/task_runtime_shm.hpp"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (false)

int main() {
    using namespace cw::server;
    using namespace cw::server::task_runtime;
    task_configuration configuration;
    configuration.timing.mode = task_timing_mode::real_time;
    configuration.timing.period_ms = 2;
    configuration.timing.diagnostics.deadline.enabled = true;
    configuration.timing.diagnostics.deadline.warn_over_ns = 200000;
    configuration.timing.diagnostics.deadline.stop_over_ns = 1000000;
    task_definition input;
    input.id = "Input";
    input.executable = "Input.exe";
    input.settings.calls = 3;
    task_definition controller;
    controller.id = "Controller";
    controller.executable = "Controller.exe";
    controller.dependencies = {"Input"};
    configuration.tasks = {input, controller};

    runtime_shm table;
    std::string error;
    CHECK(table.create(configuration, error));
    CHECK(table.valid());
    CHECK(table.auto_stop_enabled());
    table.on_project_state(1);
    table.on_run(true);
    table.task_scheduled(0, 1);
    table.task_started(0);
    table.task_completed(0, 1, 250000);
    table.task_scheduled(1, 1);
    table.task_started(1);
    table.task_completed(1, 1, 150000);
    // 2 ms period + 1.5 ms overrun => automatic STOP after completed barrier.
    table.cycle_completed(1, 3500000, 1500000, 0, false);
    CHECK(table.stop_pending());
    table.on_stop(6);

    const auto mutex_name = table.mapping_name() + L"_mutex";
    HANDLE mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutex_name.c_str());
    CHECK(mutex != nullptr);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, table.mapping_name().c_str());
    CHECK(mapping != nullptr);
    const DWORD lock_result = WaitForSingleObject(mutex, 1000);
    CHECK(lock_result == WAIT_OBJECT_0 || lock_result == WAIT_ABANDONED);
    const auto* view = static_cast<const std::byte*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    CHECK(view != nullptr);
    const auto* header = reinterpret_cast<const runtime_table*>(view);
    CHECK(header->magic == runtime_table_magic);
    CHECK(header->version == runtime_table_version);
    CHECK(std::strcmp(header->name, "system") == 0);
    CHECK(header->task_count == 2);
    CHECK(header->dependency_count == 1);
    CHECK(header->system.mode == 1);
    CHECK(header->system.period_us == 2000);
    CHECK(header->system.completed_cycles == 1);
    CHECK(header->system.simulation_time_ns == 2000000);
    CHECK(header->system.stop_reason == 1);
    CHECK(header->system.project_state == 6);
    CHECK(header->system.server_time_unix_ns > 0);
    CHECK(header->system.deadline_misses == 1);
    const auto* tasks = reinterpret_cast<const task_record*>(view + header->tasks_offset);
    CHECK(std::strcmp(tasks[0].parameters.id, "Input") == 0);
    CHECK(tasks[0].parameters.calls == 3);
    CHECK(tasks[0].diagnostics.total_calls == 3);
    CHECK(tasks[0].diagnostics.last_duration_ns == 250000);
    CHECK(std::strcmp(tasks[1].parameters.id, "Controller") == 0);
    const auto* dependencies = reinterpret_cast<const std::uint32_t*>(view + header->dependencies_offset);
    CHECK(dependencies[tasks[1].parameters.dependency_offset] == 0);

    UnmapViewOfFile(view);
    ReleaseMutex(mutex);
    CloseHandle(mapping);
    CloseHandle(mutex);
    table.close();
    CHECK(!table.valid());
    return 0;
}
