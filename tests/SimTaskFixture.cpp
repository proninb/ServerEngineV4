#include <SimTask.hpp>

namespace {
int initialized = 0;
int calls = 0;
}
void TASKINIT() { initialized = 1; }
int TASKMAIN() {
    if (initialized != 1) return 1;
    ++calls;
    const auto alias = SimTask::getvar("aaa");
    const auto direct = SimTask::getvar("int1.1");
    if (alias || direct) {
        const auto scalar = SimTask::getvar("int1");
        bool value = true;
        if (!scalar || alias.address != scalar.address || alias.address != direct.address ||
            !alias.is_bit() || alias.bit != direct.bit || alias.bit != 1 ||
            !alias.write_bit(true) || !direct.read_bit(value) || !value ||
            !direct.write_bit(false) || !alias.read_bit(value) || value ||
            !alias.write_bit(true)) return 1;
    }
    return 0;
}
void TASKDONE() { initialized = 0; }
