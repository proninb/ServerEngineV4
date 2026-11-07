#include <SimTask.hpp>
#include <Windows.h>
#include <cstdint>
struct shared_state {
    volatile LONG init[32], calls[32], done[32], sequence, last_call[32];
    volatile LONGLONG begin_qpc[32][4], end_qpc[32][4];
    volatile LONG max_task_us[32];
};
#ifndef TASK_ID
#define TASK_ID 0
#endif
static shared_state* state() {
    if (SimTask::SharedMemorySize() < sizeof(shared_state)) return nullptr;
    return static_cast<shared_state*>(SimTask::SharedMemory());
}
void TASKINIT() { if (auto* p = state()) InterlockedIncrement(&p->init[TASK_ID]); }
int TASKMAIN() {
    auto* p = state(); if (!p) return TASK_FAIL;
    // QPC-paced work of approximately 0.2 ms; never call Sleep(1).
    LARGE_INTEGER frequency{}, begin{}, now{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    const LONGLONG duration = (frequency.QuadPart * 200) / 1000000;
    do { QueryPerformanceCounter(&now); } while (now.QuadPart - begin.QuadPart < duration);
    const LONG invocation = InterlockedIncrement(&p->calls[TASK_ID]);
    const int slot = (invocation - 1) % 4;
    InterlockedExchange64(&p->begin_qpc[TASK_ID][slot], begin.QuadPart);
    InterlockedExchange64(&p->end_qpc[TASK_ID][slot], now.QuadPart);
    const LONG elapsed_us = static_cast<LONG>((now.QuadPart - begin.QuadPart) * 1000000 / frequency.QuadPart);
    LONG old = p->max_task_us[TASK_ID];
    while (elapsed_us > old) {
        const LONG seen = InterlockedCompareExchange(&p->max_task_us[TASK_ID], elapsed_us, old);
        if (seen == old) break;
        old = seen;
    }
    const LONG order = InterlockedIncrement(&p->sequence);
    InterlockedExchange(&p->last_call[TASK_ID], order);
    return TASK_OK;
}
void TASKDONE() { if (auto* p = state()) InterlockedIncrement(&p->done[TASK_ID]); }
