#pragma once
/* User-defined entry points. SimTask.lib owns main() and the control protocol. */
inline constexpr int TASK_OK = 0;
inline constexpr int TASK_FAIL = 1;

#include <cstddef>
namespace SimTask {
    void* SharedMemory() noexcept;
    std::size_t SharedMemorySize() noexcept;
}

void TASKINIT();
int TASKMAIN();
void TASKDONE();
