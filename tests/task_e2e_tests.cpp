#include "fixed_shared_memory.hpp"
#include "project/task/runtime/task_manager.hpp"
#include "project/task/runtime/task_protocol.hpp"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace cw::server;
using namespace cw::server::task_runtime;
struct shared_state {
    volatile LONG init[3];
    volatile LONG calls[3];
    volatile LONG done[3];
    volatile LONG sequence;
    volatile LONG last_call[3];
};

static bool expect(bool ok, const char* what) {
    if (!ok) std::cerr << "FAIL: " << what << '\n';
    return ok;
}
static task_definition make_task(const char* id, const std::filesystem::path& exe,
                                 std::vector<std::string> deps = {}) {
    task_definition result;
    result.id = id;
    result.executable = exe.string();
    result.dependencies = std::move(deps);
    return result;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    const auto a = std::filesystem::absolute(argv[1]);
    const auto b = std::filesystem::absolute(argv[2]);
    const auto fail = std::filesystem::absolute(argv[3]);
    const auto size = (std::max)(fixed_shared_memory::size_alignment(), std::size_t{65536});
    const std::string name = "ServerEngineTaskE2E_" + std::to_string(GetCurrentProcessId());
    fixed_shared_memory shm;
    bool created = false;
    for (int i = 0; i < 32 && !created; ++i) {
        void* candidate = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
        if (!candidate) break;
        auto address = reinterpret_cast<std::uintptr_t>(candidate);
        VirtualFree(candidate, 0, MEM_RELEASE);
        created = shm.create(name, size, address) == fixed_shared_memory_result::success;
    }
    if (!expect(created, "create exact-address SHM")) return 3;
    auto* state = reinterpret_cast<shared_state*>(shm.data());
    shm_attachment attachment{};
    attachment.size = shm.size();
    attachment.address = shm.address();
    if (name.size() >= sizeof(attachment.name)) return 4;
    std::memcpy(attachment.name, name.c_str(), name.size() + 1);
    std::string error;
    manager runtime;
    task_configuration config;
    config.tasks = {make_task("A", a), make_task("B", b, {"A"})};
    if (!runtime.start(config, std::filesystem::current_path(), 5000, attachment, error)) {
        std::cerr << "start: " << error << '\n'; return 5;
    }
    if (!expect(state->init[0] == 1 && state->init[1] == 1, "INIT + SHM attachment")) return 6;
    if (!runtime.tick(1, false, error)) { std::cerr << "tick: " << error << '\n'; return 7; }
    if (!expect(state->calls[0] == 1 && state->calls[1] == 1 &&
                state->last_call[0] < state->last_call[1], "dependency barrier")) return 8;
    if (!runtime.tick(2, true, error)) { std::cerr << "freeze: " << error << '\n'; return 9; }
    if (!expect(state->calls[0] == 1 && state->calls[1] == 1, "freeze skips normal tasks")) return 10;
    if (!runtime.stop(error)) { std::cerr << "stop: " << error << '\n'; return 11; }
    if (!expect(state->done[0] == 1 && state->done[1] == 1, "DONE before SHM release")) return 12;

    task_configuration failed;
    failed.tasks = {make_task("FAIL", fail)};
    if (!runtime.start(failed, std::filesystem::current_path(), 5000, attachment, error)) {
        std::cerr << "failure start: " << error << '\n'; return 13;
    }
    if (runtime.tick(3, false, error)) return 14;
    if (!expect(!error.empty() && state->calls[2] == 1, "TASK_FAIL propagated")) return 15;
    runtime.terminate();
    std::cout << "TASK-E2E-01 PASS: SHM / INIT / dependency / freeze / DONE / FAIL\n";
    return 0;
}
