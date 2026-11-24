#pragma once
#include <Windows.h>
struct task_cleanup_fault_state {
    volatile LONG pid;
    volatile LONG calls;
    volatile LONG done;
    volatile LONG hang_done;
};
