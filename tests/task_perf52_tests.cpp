#include "fixed_shared_memory.hpp"
#include "project/task/runtime/task_manager.hpp"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
using namespace cw::server;
using namespace cw::server::task_runtime;
struct shared_state { volatile LONG init[3], calls[3], done[3], sequence, last_call[3]; };
static bool require(bool ok, const char* what) {
    if (!ok) std::cerr << "FAIL: " << what << '\n';
    return ok;
}
static task_definition make_task(const char* id, const std::filesystem::path& exe) {
    task_definition task; task.id = id; task.executable = exe.string(); return task;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto a = std::filesystem::absolute(argv[1]);
    const auto b = std::filesystem::absolute(argv[2]);
    const auto size = (std::max)(fixed_shared_memory::size_alignment(), std::size_t{65536});
    const std::string name = "Perf52_Test_" + std::to_string(GetCurrentProcessId());
    fixed_shared_memory shm;
    bool created = false;
    for (int i = 0; i < 32 && !created; ++i) {
        void* p = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
        if (!p) break;
        const auto address = reinterpret_cast<std::uintptr_t>(p);
        VirtualFree(p, 0, MEM_RELEASE);
        created = shm.create(name, size, address) == fixed_shared_memory_result::success;
    }
    if (!require(created, "create SHM")) return 3;
    auto* state = reinterpret_cast<shared_state*>(shm.data());
    shm_attachment attachment{};
    attachment.size = shm.size(); attachment.address = shm.address();
    if (name.size() >= sizeof(attachment.name)) return 4;
    std::memcpy(attachment.name, name.c_str(), name.size() + 1);
    _putenv_s("TASK_PERF52_NO_BARRIER", "1");
    task_configuration config;
    config.tasks = {make_task("A", a), make_task("B", b)};
    manager runtime;
    std::string error;
    if (!runtime.start(config, std::filesystem::current_path(), 5000, attachment, error)) {
        std::cerr << "start: " << error << '\n'; return 5;
    }
    if (!require(state->init[0] == 1 && state->init[1] == 1, "TASKINIT")) return 6;
    LARGE_INTEGER frequency{}, start{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (std::uint64_t cycle = 1; cycle <= 100; ++cycle) {
        if (!runtime.tick(cycle, false, error)) {
            std::cerr << "tick: " << error << '\n'; return 7;
        }
    }
    QueryPerformanceCounter(&end);
    const double dispatch_ms = 1000.0 * double(end.QuadPart - start.QuadPart) / double(frequency.QuadPart);
    std::cout << "NO_BARRIER 100 ticks dispatch_ms=" << dispatch_ms << '\n';
    if (!require(dispatch_ms < 2000.0, "dispatch time bound")) return 8;
    // Do not call STOP while requests are outstanding; this test verifies safe successful STOP.
    const auto deadline = GetTickCount64() + 3000;
    while ((state->calls[0] < 1 || state->calls[1] < 1) && GetTickCount64() < deadline) Sleep(1);
    if (!require(state->calls[0] >= 1 && state->calls[1] >= 1, "both tasks executed")) return 9;
    Sleep(100);
    if (!runtime.stop(error)) { std::cerr << "stop: " << error << '\n'; return 10; }
    if (!require(state->done[0] == 1 && state->done[1] == 1, "TASKDONE")) return 11;
    // Existing implementation explicitly refuses DAG in NO_BARRIER mode.
    task_configuration dag;
    dag.tasks = {make_task("A", a), make_task("B", b)};
    dag.tasks[1].dependencies = {"A"};
    error.clear();
    if (!runtime.start(dag, std::filesystem::current_path(), 5000, attachment, error)) {
        std::cerr << "DAG start: " << error << '\n'; return 12;
    }
    const bool accepted = runtime.tick(1, false, error);
    if (!require(!accepted && error.find("DAG") != std::string::npos,
                 "NO_BARRIER rejects DAG")) return 13;
    runtime.terminate();
    std::cout << "TASK-PERF-52 TEST PASS\n";
    return 0;
}
