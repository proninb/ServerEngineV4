/* SimTask executable entry point. No user-defined main() is required. */
#include <SimTask.hpp>
#include "../../server_engine/project/task/runtime/task_host.hpp"

namespace {
bool initialize() {
    TASKINIT();
    return true;
}
bool execute() {
    return TASKMAIN() == 0;
}
void finalize() {
    TASKDONE();
}
}

int main() {
    return cw::server::task_runtime::run_host({
        &initialize, &execute, &finalize
    });
}

namespace SimTask {
void* SharedMemory() noexcept {
    return cw::server::task_runtime::shared_memory_data();
}
std::size_t SharedMemorySize() noexcept {
    return cw::server::task_runtime::shared_memory_size();
}
}
