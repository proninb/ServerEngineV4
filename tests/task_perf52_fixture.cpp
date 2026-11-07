#include <SimTask.hpp>
#include <Windows.h>
#include <cstdint>
struct shared_state { volatile LONG init[3], calls[3], done[3], sequence, last_call[3]; };
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
    Sleep(20); // deterministic busy window for the 1 ms dispatch experiment
    const LONG order = InterlockedIncrement(&p->sequence);
    InterlockedExchange(&p->last_call[TASK_ID], order);
    InterlockedIncrement(&p->calls[TASK_ID]);
    return TASK_OK;
}
void TASKDONE() { if (auto* p = state()) InterlockedIncrement(&p->done[TASK_ID]); }
