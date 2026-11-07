#include "fixed_shared_memory.hpp"
#include "project/task/runtime/task_manager.hpp"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <cstdint>
using namespace cw::server;
using namespace cw::server::task_runtime;
struct shared_state {
    volatile LONG init[32], calls[32], done[32], sequence, last_call[32];
    volatile LONGLONG begin_qpc[32][4], end_qpc[32][4];
    volatile LONG max_task_us[32];
};
static bool require(bool condition, const char* label) {
    std::cout << "INTEGRATION01 " << label << " " << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::cerr << "usage: task_lifecycle_integration fixture0 fixture1\n"; return 2; }
    const auto size = (std::max)(fixed_shared_memory::size_alignment(), std::size_t{65536});
    const std::string name = "TaskIntegration01_" + std::to_string(GetCurrentProcessId());
    fixed_shared_memory shm;
    bool created = false;
    for (int attempt = 0; attempt < 32 && !created; ++attempt) {
        void* reservation = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
        if (!reservation) break;
        const auto address = reinterpret_cast<std::uintptr_t>(reservation);
        VirtualFree(reservation, 0, MEM_RELEASE);
        created = shm.create(name, size, address) == fixed_shared_memory_result::success;
    }
    if (!require(created, "shm_create")) return 3;
    auto* state = reinterpret_cast<shared_state*>(shm.data());
    shm_attachment attachment{};
    attachment.size = shm.size();
    attachment.address = shm.address();
    if (name.size() >= sizeof(attachment.name)) return 3;
    std::memcpy(attachment.name, name.c_str(), name.size() + 1);
    auto config = [&]() {
        task_configuration c;
        for (int i = 0; i < 2; ++i) {
            task_definition t;
            t.id = "Task" + std::to_string(i);
            t.executable = std::filesystem::absolute(argv[i + 1]).string();
            c.tasks.push_back(std::move(t));
        }
        return c;
    };
    manager runtime;
    std::string error;
    bool ok = true;
    _putenv_s("TASK_PERF52_NO_BARRIER", "1");
    if (!runtime.start(config(), std::filesystem::current_path(), 5000, attachment, error)) {
        std::cerr << "start: " << error << '\n'; return 4;
    }
    for (std::uint64_t cycle = 1; cycle <= 10; ++cycle) {
        if (!runtime.tick(cycle, true, error)) { std::cerr << error << '\n'; return 5; }
    }
    ok &= require(state->calls[0] == 0 && state->calls[1] == 0, "freeze_no_dispatch");
    // STOP while the last tick can still be in flight; must drain before TASKDONE.
    if (!runtime.tick(11, false, error)) { std::cerr << error << '\n'; return 5; }
    if (!runtime.stop(error)) { std::cerr << "stop: " << error << '\n'; return 6; }
    ok &= require(state->calls[0] == 1 && state->calls[1] == 1 &&
                  state->done[0] == 1 && state->done[1] == 1, "stop_in_flight_drains");
    ok &= require(!runtime.active(), "stop_inactive");
    // Reuse the same Project SHM to model LOAD after UNLOAD.
    error.clear();
    if (!runtime.start(config(), std::filesystem::current_path(), 5000, attachment, error)) {
        std::cerr << "reload: " << error << '\n'; return 7;
    }
    if (!runtime.tick(1, false, error)) { std::cerr << error << '\n'; return 8; }
    if (!runtime.stop(error)) { std::cerr << error << '\n'; return 9; }
    ok &= require(state->calls[0] == 2 && state->calls[1] == 2 &&
                  state->init[0] == 2 && state->init[1] == 2 &&
                  state->done[0] == 2 && state->done[1] == 2, "reload_reinitializes");
    // NO_BARRIER must reject a dependent graph without dispatching any work.
    auto dag = config();
    dag.tasks[1].dependencies.push_back("Task0");
    error.clear();
    if (!runtime.start(dag, std::filesystem::current_path(), 5000, attachment, error)) {
        std::cerr << "dag start: " << error << '\n'; return 10;
    }
    const LONG before0 = state->calls[0], before1 = state->calls[1];
    const bool accepted = runtime.tick(1, false, error);
    ok &= require(!accepted && error.find("independent Tasks") != std::string::npos &&
                  state->calls[0] == before0 && state->calls[1] == before1,
                  "no_barrier_rejects_dependencies");
    runtime.terminate();
    // Same DAG must remain valid with strict LOCKSTEP.
    _putenv_s("TASK_PERF52_NO_BARRIER", "0");
    error.clear();
    if (!runtime.start(dag, std::filesystem::current_path(), 5000, attachment, error)) {
        std::cerr << "lockstep start: " << error << '\n'; return 11;
    }
    const bool tick_ok = runtime.tick(1, false, error);
    if (!tick_ok) std::cerr << "lockstep tick: " << error << '\n';
    ok &= require(tick_ok && state->calls[0] == before0 + 1 &&
                  state->calls[1] == before1 + 1, "lockstep_dependencies");
    error.clear();
    ok &= require(runtime.stop(error), "lockstep_stop");
    // terminate must be idempotent after STOP.
    runtime.terminate();
    runtime.terminate();
    ok &= require(!runtime.active(), "terminate_idempotent");
    std::cout << "INTEGRATION01_RESULT " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 12;
}
