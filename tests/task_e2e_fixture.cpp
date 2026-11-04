#include <SimTask.hpp>
#include <Windows.h>
#include <cstdint>

struct shared_state {
    volatile LONG init[3];
    volatile LONG calls[3];
    volatile LONG done[3];
    volatile LONG sequence;
    volatile LONG last_call[3];
};

#ifndef TASK_ID
#define TASK_ID 0
#endif

static shared_state* state() {
    if (SimTask::SharedMemorySize() < sizeof(shared_state)) return nullptr;
    return static_cast<shared_state*>(SimTask::SharedMemory());
}

void TASKINIT() {
    if (auto* p = state()) InterlockedIncrement(&p->init[TASK_ID]);
}
int TASKMAIN() {
    auto* p = state();
    if (!p) return TASK_FAIL;
    const LONG order = InterlockedIncrement(&p->sequence);
    InterlockedExchange(&p->last_call[TASK_ID], order);
    InterlockedIncrement(&p->calls[TASK_ID]);
#ifdef TASK_FORCE_FAIL
    return TASK_FAIL;
#else
    return TASK_OK;
#endif
}
void TASKDONE() {
    if (auto* p = state()) InterlockedIncrement(&p->done[TASK_ID]);
}
