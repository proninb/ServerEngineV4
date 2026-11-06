from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path.cwd()
p = root/'server_engine/server.cpp'
s = p.read_text(encoding='utf-8-sig')
if 'TASK_SCHEDULER_V2_01' in s:
    print('ALREADY APPLIED: no changes'); sys.exit(0)
needle = '#include "server.hpp"'
assert s.count(needle)==1, 'server.hpp anchor mismatch'
insert = '''#include "server.hpp"
// TASK_SCHEDULER_V2_01: opt-in precision timer; no permanent system timer change.
#include <cstdlib>
#include <Windows.h>
'''
s=s.replace(needle,insert,1)
anchor='int server::run() {'
assert s.count(anchor)==1, 'run anchor mismatch'
helper='''namespace {
// Timer resolution is requested only while the server run loop is active.
class scheduler_timer_resolution final {
public:
    scheduler_timer_resolution() noexcept {
        const char* enabled = std::getenv("TASK_SCHEDULER_V2");
        if (!enabled || enabled[0] != '1' || enabled[1] != '\\0') return;
        module = LoadLibraryW(L"winmm.dll");
        if (!module) return;
        begin = reinterpret_cast<timer_function>(GetProcAddress(module, "timeBeginPeriod"));
        end = reinterpret_cast<timer_function>(GetProcAddress(module, "timeEndPeriod"));
        if (begin && end && begin(1) == 0) active = true;
    }
    ~scheduler_timer_resolution() noexcept {
        if (active) end(1);
        if (module) FreeLibrary(module);
    }
    scheduler_timer_resolution(const scheduler_timer_resolution&) = delete;
    scheduler_timer_resolution& operator=(const scheduler_timer_resolution&) = delete;
    bool enabled() const noexcept { return active; }
private:
    using timer_function = unsigned int (WINAPI*)(unsigned int);
    HMODULE module = nullptr;
    timer_function begin = nullptr;
    timer_function end = nullptr;
    bool active = false;
};
}

'''
s=s.replace(anchor,helper+anchor,1)
s=s.replace(anchor,anchor+'\n    const scheduler_timer_resolution precision_timer;\n    const auto scheduler_period = std::chrono::milliseconds(2);',1)
old='''        const auto deadline = ticking
            ? (std::min)(context.lease.expires_at(),
                std::chrono::system_clock::now() + std::chrono::milliseconds(2))
            : context.lease.expires_at();'''
new='''        // Convert the steady-clock deadline to the queue's system-clock domain.
        // Re-evaluate each iteration so wall-clock adjustments cannot accumulate.
        const auto steady_now = std::chrono::steady_clock::now();
        const auto remaining = next_task_tick > steady_now
            ? next_task_tick - steady_now
            : std::chrono::steady_clock::duration::zero();
        const auto deadline = ticking
            ? (std::min)(context.lease.expires_at(),
                std::chrono::system_clock::now() +
                    std::chrono::duration_cast<std::chrono::system_clock::duration>(remaining))
            : context.lease.expires_at();'''
assert s.count(old)==1, 'deadline anchor mismatch'
s=s.replace(old,new,1)
old='''                    next_task_tick = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(2);'''
new='''                    if (precision_timer.enabled()) {
                        // Absolute cadence; skip missed deadlines without catch-up bursts.
                        const auto completed = std::chrono::steady_clock::now();
                        next_task_tick += scheduler_period;
                        if (next_task_tick <= completed) {
                            const auto missed = (completed - next_task_tick) /
                                scheduler_period + 1;
                            next_task_tick += scheduler_period * missed;
                        }
                    } else {
                        next_task_tick = std::chrono::steady_clock::now() +
                            scheduler_period;
                    }'''
assert s.count(old)==1, 'tick advance anchor mismatch'
s=s.replace(old,new,1)
# Do not write partial modifications if any anchor is missing.
backup=p.with_suffix('.cpp.scheduler-v2-01.backup')
if backup.exists():
    raise RuntimeError(f'Backup already exists: {backup}; refusing to overwrite')
backup.write_bytes(p.read_bytes())
p.write_text(s,encoding='utf-8',newline='')
print('PASS: TASK-SCHEDULER-V2-01 applied; backup:',backup)
