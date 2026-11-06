from pathlib import Path
import argparse, shutil

p=argparse.ArgumentParser()
p.add_argument('--root',default='.')
a=p.parse_args()
root=Path(a.root).resolve()
f=root/'server_engine'/'server.cpp'
if not f.is_file(): raise SystemExit(f'Missing {f}')
s=f.read_text(encoding='utf-8-sig')
if 'TASK_SCHEDULER_DEADLINE_01' in s: raise SystemExit('Already applied; no changes')

def replace_once(old,new):
 global s
 count=s.count(old)
 if count!=1: raise SystemExit(f'Expected exactly one anchor, got {count}: {old[:100]!r}. No changes.')
 s=s.replace(old,new,1)

replace_once('#include <cstdio>','#include <cstdio>\n#include <cstdint>')
replace_once('int server::run() {\n    const scheduler_timer_resolution precision_timer;', '''int server::run() {
    // TASK_SCHEDULER_DEADLINE_01: optional per-cycle diagnostics, no scheduling changes.
    const char* deadline_enabled = std::getenv("TASK_SCHEDULER_DEADLINE");
    const bool deadline_diagnostics = deadline_enabled &&
        deadline_enabled[0] == '1' && deadline_enabled[1] == '\\0';
    std::FILE* deadline_file = nullptr;
    if (deadline_diagnostics) {
        const char* output = std::getenv("TASK_SCHEDULER_DEADLINE_CSV");
        if (!output || !output[0]) output = "task-scheduler-deadline.csv";
        if (fopen_s(&deadline_file, output, "w") != 0 || !deadline_file) {
            std::fprintf(stderr, "Deadline diagnostics: cannot open %s\\n", output);
        } else {
            std::fputs("runtime_tick,scheduled_ns,wakeup_ns,dispatch_ns,barrier_complete_ns,cycle_complete_ns,wakeup_lateness_us,barrier_duration_us,cycle_duration_us,deadline_miss,overrun_us,skipped_periods\\n", deadline_file);
        }
    }
    const auto deadline_origin = std::chrono::steady_clock::now();
    const auto deadline_ns = [&](std::chrono::steady_clock::time_point point) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            point - deadline_origin).count();
    };
    const scheduler_timer_resolution precision_timer;''')
replace_once('''                if (now_tick >= next_task_tick) {
                    const auto perf_tick_start''','''                if (now_tick >= next_task_tick) {
                    const auto scheduled_tick = next_task_tick;
                    const auto actual_wakeup = now_tick;
                    const auto perf_tick_start''')
replace_once('''                    if (task_manager.active() && !task_manager.tick(cycle, frozen, task_error)) {''','''                    const auto task_dispatch = std::chrono::steady_clock::now();
                    const bool task_tick_ok = !task_manager.active() ||
                        task_manager.tick(cycle, frozen, task_error);
                    const auto barrier_complete = std::chrono::steady_clock::now();
                    if (!task_tick_ok) {''')
replace_once('''                    task_runtime::perf::record("server_tick", runtime_tick, perf_tick_start);
                    if (precision_timer.enabled()) {''','''                    task_runtime::perf::record("server_tick", runtime_tick, perf_tick_start);
                    const auto cycle_complete = std::chrono::steady_clock::now();
                    std::int64_t skipped_periods = 0;
                    if (precision_timer.enabled()) {''')
replace_once('''                            const auto missed = (completed - next_task_tick) /
                                scheduler_period + 1;
                            next_task_tick += scheduler_period * missed;''','''                            const auto missed = (completed - next_task_tick) /
                                scheduler_period + 1;
                            skipped_periods = static_cast<std::int64_t>(missed);
                            next_task_tick += scheduler_period * missed;''')
replace_once('''                        next_task_tick = std::chrono::steady_clock::now() +
                            scheduler_period;
                    }
                }
                continue;''','''                        next_task_tick = std::chrono::steady_clock::now() +
                            scheduler_period;
                    }
                    if (deadline_file) {
                        const auto budget_end = scheduled_tick + scheduler_period;
                        const auto lateness = actual_wakeup > scheduled_tick
                            ? actual_wakeup - scheduled_tick
                            : std::chrono::steady_clock::duration::zero();
                        const auto overrun = cycle_complete > budget_end
                            ? cycle_complete - budget_end
                            : std::chrono::steady_clock::duration::zero();
                        const auto us = [](std::chrono::steady_clock::duration value) {
                            return std::chrono::duration<double, std::micro>(value).count();
                        };
                        std::fprintf(deadline_file,
                            "%llu,%lld,%lld,%lld,%lld,%lld,%.3f,%.3f,%.3f,%d,%.3f,%lld\\n",
                            static_cast<unsigned long long>(runtime_tick),
                            static_cast<long long>(deadline_ns(scheduled_tick)),
                            static_cast<long long>(deadline_ns(actual_wakeup)),
                            static_cast<long long>(deadline_ns(task_dispatch)),
                            static_cast<long long>(deadline_ns(barrier_complete)),
                            static_cast<long long>(deadline_ns(cycle_complete)),
                            us(lateness), us(barrier_complete - task_dispatch),
                            us(cycle_complete - actual_wakeup),
                            cycle_complete > budget_end ? 1 : 0,
                            us(overrun), static_cast<long long>(skipped_periods));
                    }
                }
                continue;''')
replace_once('''    // SHUTDOWN result has already been presented to its originating endpoint.
    shutdown();''','''    if (deadline_file) std::fclose(deadline_file);
    // SHUTDOWN result has already been presented to its originating endpoint.
    shutdown();''')
backup=f.with_suffix('.cpp.deadline01.bak')
if backup.exists(): raise SystemExit(f'Backup already exists: {backup}. No changes.')
shutil.copy2(f,backup)
f.write_text(s,encoding='utf-8',newline='')
print('PATCH APPLIED:',f)
print('BACKUP:',backup)
