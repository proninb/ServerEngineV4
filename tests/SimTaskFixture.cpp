#include <SimTask.hpp>

namespace {
int initialized = 0;
int calls = 0;
}
void TASKINIT() { initialized = 1; }
int TASKMAIN() {
    if (initialized != 1) return 1;
    ++calls;
    return 0;
}
void TASKDONE() { initialized = 0; }
