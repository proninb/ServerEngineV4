/* Intentionally non-returning TASKMAIN: isolated Task process shutdown test. */
#include <SimTask.hpp>
#include <Windows.h>

void TASKINIT() {}
int TASKMAIN() {
    Sleep(INFINITE);
    return TASK_OK; // unreachable unless test is broken
}
void TASKDONE() {}
