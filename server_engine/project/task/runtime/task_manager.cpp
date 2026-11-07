#include "task_manager.hpp"
#include "task_trace_buffer.hpp"
#include "task_perf_probe.hpp"
#include <future>
#include <algorithm>
#include <Windows.h>
#include <chrono>
#include <unordered_map>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace cw::server::task_runtime {
manager::~manager() { terminate(); }

bool manager::start(task_configuration value,
                    const std::filesystem::path& project_directory,
                    std::uint32_t timeout_ms, const shm_attachment& attachment,
                    std::string& error) {
    terminate();
    if (!validate_task_configuration(value, error)) return false;
    configuration = std::move(value);
    dependency_indices.clear();
    dependency_indices.resize(configuration.tasks.size());
    std::unordered_map<std::string, std::size_t> dependency_ids;
    dependency_ids.reserve(configuration.tasks.size());
    for (std::size_t i = 0; i < configuration.tasks.size(); ++i)
        dependency_ids.emplace(configuration.tasks[i].id, i);
    for (std::size_t i = 0; i < configuration.tasks.size(); ++i) {
        auto& indices = dependency_indices[i];
        indices.reserve(configuration.tasks[i].dependencies.size());
        for (const auto& name : configuration.tasks[i].dependencies)
            indices.push_back(dependency_ids.at(name));
    }
    workers.resize(configuration.tasks.size());
    for (const auto index : configuration.execution_order) {
        const auto& task = configuration.tasks[index];
        if (!task.enabled) continue;
        auto executable = std::filesystem::path(task.executable);
        if (executable.is_relative()) executable = project_directory / executable;
        workers[index].child = std::make_unique<process>();
        if (!workers[index].child->start(executable.lexically_normal().wstring(),
                timeout_ms, attachment, error)) {
            error = "Task '" + task.id + "': " + error;
            terminate();
            return false;
        }
    }
    try {
        trace = std::make_unique<task_trace_buffer>();
    } catch (const std::exception& ex) {
        error = std::string("Task runtime initialization failed: ") + ex.what();
        terminate();
        return false;
    } catch (...) {
        error = "Task runtime initialization failed: unknown exception";
        terminate();
        return false;
    }
    completion_diagnostics.assign(workers.size(), {});
    completion_busy.assign(workers.size(), 0);
    completion_deadline.assign(workers.size(), 0);
    completion_cycle.assign(workers.size(), 0);
    completion_error.clear();
    completion_stop = false;
    completion_stopping = false;
    completion_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!completion_wake) {
        error = "NO_BARRIER wake event creation failed";
        terminate();
        return false;
    }
    started = true;
    return true;
}

bool manager::start(task_configuration value,
                    const std::filesystem::path& project_directory,
                    std::uint32_t timeout_ms, std::string& error) {
    terminate();
    if (!validate_task_configuration(value, error)) return false;
    configuration = std::move(value);
    dependency_indices.clear();
    dependency_indices.resize(configuration.tasks.size());
    std::unordered_map<std::string, std::size_t> dependency_ids;
    dependency_ids.reserve(configuration.tasks.size());
    for (std::size_t i = 0; i < configuration.tasks.size(); ++i)
        dependency_ids.emplace(configuration.tasks[i].id, i);
    for (std::size_t i = 0; i < configuration.tasks.size(); ++i) {
        auto& indices = dependency_indices[i];
        indices.reserve(configuration.tasks[i].dependencies.size());
        for (const auto& name : configuration.tasks[i].dependencies)
            indices.push_back(dependency_ids.at(name));
    }
    workers.resize(configuration.tasks.size());
    for (const auto index : configuration.execution_order) {
        const auto& task = configuration.tasks[index];
        if (!task.enabled) continue;
        auto executable = std::filesystem::path(task.executable);
        if (executable.is_relative()) executable = project_directory / executable;
        workers[index].child = std::make_unique<process>();
        if (!workers[index].child->start(executable.wstring(), timeout_ms, error)) {
            error = "Task '" + task.id + "': " + error;
            terminate();
            return false;
        }
    }
    try {
        trace = std::make_unique<task_trace_buffer>();
    } catch (const std::exception& ex) {
        error = std::string("Task runtime initialization failed: ") + ex.what();
        terminate();
        return false;
    } catch (...) {
        error = "Task runtime initialization failed: unknown exception";
        terminate();
        return false;
    }
    completion_diagnostics.assign(workers.size(), {});
    completion_busy.assign(workers.size(), 0);
    completion_deadline.assign(workers.size(), 0);
    completion_cycle.assign(workers.size(), 0);
    completion_error.clear();
    completion_stop = false;
    completion_stopping = false;
    completion_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!completion_wake) {
        error = "NO_BARRIER wake event creation failed";
        terminate();
        return false;
    }
    started = true;
    return true;
}


// PERF52_NO_BARRIER: completion handling is owned by a dedicated server thread.
// One request per Task process; no SHM protocol changes.
bool manager::tick_no_barrier(std::uint64_t cycle, bool frozen, std::string& error) {
    if (!started) { error = "Task Manager not started"; return false; }
    for (const auto& task : configuration.tasks) {
        if (task.enabled && !task.dependencies.empty()) {
            error = "PERF52_NO_BARRIER requires independent Tasks (DAG is not supported in this diagnostic mode)";
            return false;
        }
    }
    if (!completion_thread.joinable()) {
        try {
            completion_thread = std::thread([this] { completion_loop(); });
        } catch (const std::exception& ex) {
            error = std::string("Completion thread startup failed: ") + ex.what();
            return false;
        }
    }
    LARGE_INTEGER lock_begin{}, lock_end{}, qpc_frequency{};
    QueryPerformanceFrequency(&qpc_frequency);
    QueryPerformanceCounter(&lock_begin);
    std::lock_guard<std::mutex> guard(completion_mutex);
    QueryPerformanceCounter(&lock_end);
    const auto lock_wait_us = static_cast<std::uint64_t>(
        (lock_end.QuadPart - lock_begin.QuadPart) * 1000000 / qpc_frequency.QuadPart);
    for (auto& d : completion_diagnostics)
        d.max_tick_lock_wait_us = (std::max)(d.max_tick_lock_wait_us, lock_wait_us);
    if (completion_stopping) { error = "Task Manager is stopping"; return false; }
    if (!completion_error.empty()) { error = completion_error; return false; }
    LARGE_INTEGER dispatch_begin{}, dispatch_end{};
    QueryPerformanceCounter(&dispatch_begin);
    bool dispatched_any = false;
    for (std::size_t i = 0; i < configuration.tasks.size(); ++i) {
        const auto& task = configuration.tasks[i];
        if (!task.enabled || !task_is_scheduled(task.settings, cycle, frozen)) continue;
        ++completion_diagnostics[i].scheduled;
        // FIX-06: The completion thread may not have been scheduled yet.
        // Under completion_mutex, consume an already-signalled reply before
        // treating this Task as busy. Only one thread can consume the event.
        if (completion_busy[i]) {
            auto& child = *workers[i].child;
            const DWORD ready = WaitForSingleObject(child.completion_handle(), 0);
            if (ready == WAIT_OBJECT_0) {
                std::string local;
                const bool ok = child.complete_run(local);
                trace->record("NO_BARRIER", completion_cycle[i], task.id,
                              ok ? "completed_on_tick" : "failed_on_tick");
                completion_busy[i] = 0;
                if (ok) {
                    ++completion_diagnostics[i].completed;
                    ++completion_diagnostics[i].reaped_on_tick;
                } else {
                    ++completion_diagnostics[i].failed;
                    if (completion_error.empty())
                        completion_error = "Task '" + task.id + "': " + local;
                }
                completion_cv.notify_all();
            } else if (ready == WAIT_FAILED) {
                completion_error = "Task '" + task.id +
                    "': completion probe failed (Win32 " +
                    std::to_string(GetLastError()) + ")";
            }
            if (!completion_error.empty()) { error = completion_error; return false; }
        }
        if (completion_busy[i]) {
            ++completion_diagnostics[i].skipped_busy;
            if (!completion_diagnostics[i].first_skipped_cycle) {
                completion_diagnostics[i].first_skipped_cycle = cycle;
                LARGE_INTEGER stamp{};
                QueryPerformanceCounter(&stamp);
                completion_diagnostics[i].first_skipped_qpc = static_cast<std::uint64_t>(stamp.QuadPart);
            }
            trace->record("NO_BARRIER", cycle, task.id, "skipped_busy");
            continue;
        }
        std::string local;
        if (!workers[i].child->begin_run(cycle, task.settings.calls, local)) {
            error = "Task '" + task.id + "': " + local;
            return false;
        }
        completion_busy[i] = 1;
        ++completion_diagnostics[i].dispatched;
        dispatched_any = true;
        completion_deadline[i] = GetTickCount64() + workers[i].child->timeout_ms();
        completion_cycle[i] = cycle;
        trace->record("NO_BARRIER", cycle, task.id, "dispatched");
    }
    QueryPerformanceCounter(&dispatch_end);
    const auto dispatch_us = static_cast<std::uint64_t>(
        (dispatch_end.QuadPart - dispatch_begin.QuadPart) * 1000000 / qpc_frequency.QuadPart);
    for (auto& d : completion_diagnostics)
        d.max_dispatch_us = (std::max)(d.max_dispatch_us, dispatch_us);
    if (dispatched_any) SetEvent(completion_wake);
    return true;
}

void manager::completion_loop() noexcept {
    // Consume every ready reply before waiting. The wake event is last, so it
    // cannot starve a completion when both are signalled.
    std::size_t offset = 0;
    for (;;) {
        HANDLE handles[MAXIMUM_WAIT_OBJECTS]{};
        std::size_t indices[31]{};
        DWORD count = 0;
        DWORD wait_ms = INFINITE;
        {
            std::lock_guard<std::mutex> guard(completion_mutex);
            if (completion_stop) return;
            const auto now = GetTickCount64();
            const auto total = completion_busy.size();
            std::size_t active = 0;
            for (const auto busy : completion_busy) active += busy ? 1 : 0;

            // A completed reply may already be signalled when the tick thread
            // releases the mutex. Drain all such replies in one pass.
            for (std::size_t i = 0; i < total; ++i) {
                if (!completion_busy[i]) continue;
                auto& child = *workers[i].child;
                if (WaitForSingleObject(child.completion_handle(), 0) == WAIT_OBJECT_0) {
                    std::string local;
                    const bool ok = child.complete_run(local);
                    trace->record("NO_BARRIER", completion_cycle[i], configuration.tasks[i].id,
                                  ok ? "completed" : "failed");
                    completion_busy[i] = 0;
                    if (ok) ++completion_diagnostics[i].completed;
                    else ++completion_diagnostics[i].failed;
                    if (!ok && completion_error.empty())
                        completion_error = "Task '" + configuration.tasks[i].id + "': " + local;
                } else if (WaitForSingleObject(child.process_handle(), 0) == WAIT_OBJECT_0) {
                    completion_busy[i] = 0;
                    ++completion_diagnostics[i].failed;
                    if (completion_error.empty())
                        completion_error = "Task '" + configuration.tasks[i].id + "' exited before reply";
                } else if (now >= completion_deadline[i]) {
                    completion_busy[i] = 0;
                    ++completion_diagnostics[i].failed;
                    if (completion_error.empty())
                        completion_error = "Task '" + configuration.tasks[i].id + "' timed out";
                }
            }
            completion_cv.notify_all();
            if (!completion_error.empty()) return;

            // Build a bounded wait set. Completion handles precede the wake
            // event; process-exit handles are paired with each completion.
            if (total) {
                const auto start = offset % total;
                for (std::size_t n = 0; n < total && count < 62; ++n) {
                    const auto i = (start + n) % total;
                    if (!completion_busy[i]) continue;
                    indices[count / 2] = i;
                    handles[count++] = workers[i].child->completion_handle();
                    handles[count++] = workers[i].child->process_handle();
                    const auto remaining = completion_deadline[i] > now ?
                        completion_deadline[i] - now : 0;
                    const auto timeout = static_cast<DWORD>(
                        (std::min<ULONGLONG>)(remaining, 0xFFFFFFFEull));
                    wait_ms = (std::min)(wait_ms, timeout);
                }
                offset = (start + 31) % total;
                if (active > 31) wait_ms = (std::min)(wait_ms, DWORD{1});
            }
        }
        handles[count++] = completion_wake;
        const DWORD result = WaitForMultipleObjects(count, handles, FALSE, wait_ms);
        // Completion events are auto-reset. WaitForMultipleObjects consumes the
        // signal, so complete the corresponding request here, not in a later poll.
        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count - 1) {
            const DWORD slot = result - WAIT_OBJECT_0;
            const std::size_t task_index = indices[slot / 2];
            std::lock_guard<std::mutex> guard(completion_mutex);
            if (completion_busy[task_index]) {
                if ((slot % 2) == 0) {
                    std::string local;
                    const bool ok = workers[task_index].child->complete_run(local);
                    trace->record("NO_BARRIER", completion_cycle[task_index],
                        configuration.tasks[task_index].id, ok ? "completed" : "failed");
                    completion_busy[task_index] = 0;
                    if (ok) ++completion_diagnostics[task_index].completed;
                    else ++completion_diagnostics[task_index].failed;
                    if (!ok && completion_error.empty())
                        completion_error = "Task '" + configuration.tasks[task_index].id + "': " + local;
                } else {
                    completion_busy[task_index] = 0;
                    ++completion_diagnostics[task_index].failed;
                    if (completion_error.empty())
                        completion_error = "Task '" + configuration.tasks[task_index].id +
                            "' exited before reply";
                }
                completion_cv.notify_all();
            }
            continue;
        }
        // WAIT_TIMEOUT is normal: recheck completions and deadlines.
        if (result == WAIT_TIMEOUT) continue;
        if (result == WAIT_FAILED || result < WAIT_OBJECT_0 ||
            result >= WAIT_OBJECT_0 + count) {
            std::lock_guard<std::mutex> guard(completion_mutex);
            if (completion_error.empty())
                completion_error = "NO_BARRIER completion wait failed (Win32 " +
                    std::to_string(GetLastError()) + ")";
            completion_cv.notify_all();
            return;
        }
        // Do not complete a reply using stale indices: the next pass obtains
        // the mutex and drains the current state, including any new dispatch.
        // A timeout also triggers the next pass to check all deadlines.
    }
}

std::vector<manager::no_barrier_diagnostics> manager::no_barrier_snapshot() {
    std::lock_guard<std::mutex> guard(completion_mutex);
    return completion_diagnostics;
}

void manager::stop_completion() noexcept {
    {
        std::lock_guard<std::mutex> guard(completion_mutex);
        completion_stop = true;
    }
    if (completion_wake) SetEvent(completion_wake);
    if (completion_thread.joinable()) completion_thread.join();
    if (completion_wake) {
        CloseHandle(completion_wake);
        completion_wake = nullptr;
    }
}

bool manager::tick(std::uint64_t cycle, bool frozen, std::string& error) {
    if (std::getenv("TASK_PERF52_NO_BARRIER") &&
        std::string(std::getenv("TASK_PERF52_NO_BARRIER")) == "1")
        return tick_no_barrier(cycle, frozen, error);
    const auto perf_start = perf::clock::now();
    if (!started) { error = "Task Manager not started"; return false; }
    const auto count = configuration.tasks.size();
    // Fixed-size scratch: allocated once after SHM/Task configuration is fixed.
    struct state {
        std::vector<unsigned char> scheduled, completed;
        std::vector<std::size_t> ready;
        std::vector<HANDLE> handles;
        std::vector<std::size_t> handle_tasks;
        std::vector<ULONGLONG> deadlines;
        std::vector<unsigned char> done;
        std::vector<HANDLE> waiting;
        std::vector<std::size_t> mapping;
    };
    static thread_local state scratch;
    auto& s = scratch;
    s.scheduled.assign(count, 0);
    s.completed.assign(count, 0);
    s.ready.clear();
    s.ready.reserve(count);
    s.handles.clear();
    s.handles.reserve(62);
    s.handle_tasks.clear();
    s.handle_tasks.reserve(31);
    s.deadlines.assign(count, 0);
    s.done.reserve(31);
    s.waiting.reserve(62);
    s.mapping.reserve(31);
    std::size_t remaining = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& task = configuration.tasks[i];
        s.scheduled[i] = task.enabled && task_is_scheduled(task.settings, cycle, frozen);
        if (s.scheduled[i]) {
            ++remaining;
            trace->record("LOCKSTEP", cycle, task.id, "scheduled");
        }
    }
    // The configuration's execution_order is already topologically sorted.
    // Dispatch ready nodes directly to child processes. No executor/futures.
    while (remaining) {
        s.ready.clear();
        for (const auto index : configuration.execution_order) {
            if (!s.scheduled[index] || s.completed[index]) continue;
            bool blocked = false;
            for (const auto& name : configuration.tasks[index].dependencies) {
                // PERF-47 keeps configuration compatibility; compile this lookup
                // into indices in a separate, independently measured change.
                const auto it = std::find_if(configuration.tasks.begin(), configuration.tasks.end(),
                    [&](const auto& t) { return t.id == name; });
                if (it == configuration.tasks.end()) { error = "Unknown Task dependency"; return false; }
                const auto dep = static_cast<std::size_t>(it - configuration.tasks.begin());
                if (s.scheduled[dep] && !s.completed[dep]) { blocked = true; break; }
            }
            if (!blocked) s.ready.push_back(index);
        }
        if (s.ready.empty()) { error = "Task dependency scheduling stalled"; return false; }
        // Windows WaitForMultipleObjects has a 64-handle limit. Reserve two
        // handles per Task (completion + process exit); bounded batches of 31.
        for (std::size_t offset = 0; offset < s.ready.size(); offset += 31) {
            const auto last = (std::min)(offset + 31, s.ready.size());
            s.handles.clear();
            s.handle_tasks.clear();
            bool dispatched_ok = true;
            for (std::size_t j = offset; j < last; ++j) {
                const auto index = s.ready[j];
                auto& child = *workers[index].child;
                trace->record("LOCKSTEP", cycle, configuration.tasks[index].id, "dispatched");
                std::string local;
                if (!child.begin_run(cycle, configuration.tasks[index].settings.calls, local)) {
                    error = "Task '" + configuration.tasks[index].id + "': " + local;
                    dispatched_ok = false;
                    break;
                }
                s.deadlines[index] = GetTickCount64() + child.timeout_ms();
                s.handle_tasks.push_back(index);
                s.handles.push_back(child.completion_handle());
                s.handles.push_back(child.process_handle());
            }
            // Always drain successfully dispatched children, even on dispatch failure.
            const auto barrier_start = perf::clock::now();
            std::size_t outstanding = s.handle_tasks.size();
            s.done.assign(s.handle_tasks.size(), 0);
            while (outstanding) {
                ULONGLONG nearest = ~ULONGLONG{0};
                for (std::size_t k = 0; k < s.done.size(); ++k)
                    if (!s.done[k]) nearest = (std::min)(nearest, s.deadlines[s.handle_tasks[k]]);
                const auto now = GetTickCount64();
                const auto wait_ms = now >= nearest ? DWORD{0} :
                    static_cast<DWORD>((std::min<ULONGLONG>)(nearest - now, 0xFFFFFFFEull));
                // Remove completed handles to prevent a signalled process handle
                // from repeatedly winning the wait.
                s.waiting.clear();
                s.mapping.clear();
                for (std::size_t k = 0; k < s.done.size(); ++k) if (!s.done[k]) {
                    s.waiting.push_back(s.handles[2*k]);
                    s.waiting.push_back(s.handles[2*k+1]);
                    s.mapping.push_back(k);
                }
                const DWORD result = WaitForMultipleObjects(
                    static_cast<DWORD>(s.waiting.size()), s.waiting.data(), FALSE, wait_ms);
                if (result == WAIT_TIMEOUT || result == WAIT_FAILED) {
                    if (dispatched_ok) error = result == WAIT_TIMEOUT ? "Task reply timeout" : "Task completion wait failed";
                    dispatched_ok = false;
                    break;
                }
                const auto position = static_cast<std::size_t>(result - WAIT_OBJECT_0);
                if (position >= s.waiting.size()) {
                    if (dispatched_ok) error = "Unexpected Task wait result";
                    dispatched_ok = false;
                    break;
                }
                const auto k = s.mapping[position / 2];
                const auto index = s.handle_tasks[k];
                if (position % 2) {
                    if (dispatched_ok) error = "Task '" + configuration.tasks[index].id + "' exited before reply";
                    dispatched_ok = false;
                    break;
                }
                std::string local;
                const bool ok = workers[index].child->complete_run(local);
                trace->record("LOCKSTEP", cycle, configuration.tasks[index].id,
                    ok ? "completed" : "failed");
                s.done[k] = 1;
                --outstanding;
                if (!ok && dispatched_ok) {
                    error = "Task '" + configuration.tasks[index].id + "': " + local;
                    dispatched_ok = false;
                }
            }
            perf::record("barrier_wait", cycle, barrier_start);
            if (!dispatched_ok) {
                // A failed cycle cannot continue. terminate() stops any still
                // outstanding child before the project SHM can be unloaded.
                terminate();
                return false;
            }
            for (const auto index : s.handle_tasks) {
                s.completed[index] = 1;
                --remaining;
            }
        }
    }
    perf::record("manager_tick", cycle, perf_start);
    return true;
}

bool manager::reap(std::string& error) {
    using namespace std::chrono_literals;
    bool success = true;
    for (std::size_t i = 0; i < workers.size(); ++i) {
        auto& worker = workers[i];
        if (!worker.busy || worker.pending.wait_for(0ms) != std::future_status::ready) continue;
        const auto result = worker.pending.get();
        worker.busy = false;
        if (!result.first && success) {
            error = "Task '" + configuration.tasks[i].id + "': " + result.second;
            success = false;
        }
    }
    return success;
}

bool manager::tick_overlap(std::uint64_t cycle, bool frozen, std::string& error) {
    if (!started) { error = "Task Manager not started"; return false; }
    if (!reap(error)) return false;
    if (!executor) {
        try { executor = std::make_unique<task_executor>(workers.size()); }
        catch (const std::exception& ex) { error = ex.what(); return false; }
    }

    const auto count = configuration.tasks.size();
    std::unordered_map<std::string, std::size_t> ids;
    std::vector<bool> scheduled(count, false);
    std::vector<bool> dispatched(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        ids.emplace(configuration.tasks[i].id, i);
        scheduled[i] = configuration.tasks[i].enabled &&
            task_is_scheduled(configuration.tasks[i].settings, cycle, frozen);
    }

    // Admit each scheduled dependency component atomically. If any member is
    // still busy, skip the entire component for this cycle; never dispatch A
    // without its scheduled dependent B (or vice versa).
    std::vector<bool> eligible = scheduled;
    for (std::size_t i = 0; i < count; ++i) {
        if (workers[i].busy) eligible[i] = false;
        if (!scheduled[i]) continue;
        for (const auto& name : configuration.tasks[i].dependencies) {
            const auto dep = ids.at(name);
            if (!scheduled[dep] && workers[dep].busy) eligible[i] = false;
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (!scheduled[i]) continue;
            for (const auto& name : configuration.tasks[i].dependencies) {
                const auto dep = ids.at(name);
                if (!scheduled[dep]) continue;
                if (!eligible[i] || !eligible[dep]) {
                    if (eligible[i] || eligible[dep]) changed = true;
                    eligible[i] = false;
                    eligible[dep] = false;
                }
            }
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (!scheduled[i]) continue;
        trace->record("OVERLAP", cycle, configuration.tasks[i].id, "scheduled");
        if (!eligible[i]) trace->record("OVERLAP", cycle, configuration.tasks[i].id, "skipped_busy");
    }

    // Retain the futures for this cycle. A dependent waits inside its own
    // worker, not on the server scheduler thread.
    std::vector<std::shared_future<std::pair<bool, std::string>>> current(count);
    for (const auto index : configuration.execution_order) {
        if (!eligible[index]) continue;
        auto& worker = workers[index];
        trace->record("OVERLAP", cycle, configuration.tasks[index].id, "dispatched");

        bool blocked = false;
        std::vector<std::shared_future<std::pair<bool, std::string>>> dependencies;
        for (const auto& name : configuration.tasks[index].dependencies) {
            const auto dep = ids.at(name);
            if (scheduled[dep]) {
                // A same-cycle dependency must be dispatched in this cycle.
                if (!dispatched[dep]) { blocked = true; break; }
                dependencies.push_back(current[dep]);
            } else if (workers[dep].busy) {
                // Do not consume an incomplete prior-cycle dependency.
                blocked = true;
                break;
            }
        }
        if (blocked) continue;

        try {
            worker.pending = executor->submit(
                [this, index, cycle, dependencies = std::move(dependencies)]() {
                    const auto dependency_start = std::chrono::steady_clock::now();
                    for (const auto& dependency : dependencies) {
                        const auto result = dependency.get();
                        if (!result.first)
                            return std::make_pair(false,
                                std::string("Dependency failed: ") + result.second);
                    }
                    const auto dependency_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - dependency_start).count();
                    trace->record("OVERLAP", cycle, configuration.tasks[index].id,
                               "dependency_wait", dependency_us);
                    std::string local;
                    const auto run_start = std::chrono::steady_clock::now();
                    const bool ok = workers[index].child->run(
                        cycle, configuration.tasks[index].settings.calls, local);
                    const auto run_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - run_start).count();
                    trace->record("OVERLAP", cycle, configuration.tasks[index].id,
                               ok ? "completed" : "failed", run_us);
                    return std::make_pair(ok, std::move(local));
                }).share();
            worker.busy = true;
            current[index] = worker.pending;
            dispatched[index] = true;
        } catch (const std::exception& ex) {
            error = "Failed to dispatch Task '" + configuration.tasks[index].id +
                "': " + ex.what();
            return false;
        } catch (...) {
            error = "Failed to dispatch Task '" + configuration.tasks[index].id + "'";
            return false;
        }
    }
    return true;
}

void manager::drain() noexcept {
    for (auto& worker : workers) {
        if (worker.busy) {
            try { (void)worker.pending.get(); } catch (...) {}
            worker.busy = false;
        }
    }
}

bool manager::stop(std::string& error) {
    // Prevent new NO_BARRIER dispatches before waiting for outstanding replies.
    // The completion thread must remain running until all replies are consumed.
    {
        std::unique_lock<std::mutex> lock(completion_mutex);
        completion_stopping = true;
        const auto all_done = [this] {
            return !completion_error.empty() ||
                std::none_of(completion_busy.begin(), completion_busy.end(),
                    [](unsigned char busy) { return busy != 0; });
        };
        ULONGLONG latest_deadline = GetTickCount64();
        for (std::size_t i = 0; i < completion_busy.size(); ++i)
            if (completion_busy[i])
                latest_deadline = (std::max)(latest_deadline, completion_deadline[i]);
        // Bound shutdown even if a child never acknowledges its last request.
        const auto now = GetTickCount64();
        const auto wait_ms = latest_deadline > now ? latest_deadline - now : 0;
        const auto limit = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(wait_ms + 100);
        while (!all_done() && completion_cv.wait_until(lock, limit) !=
                                  std::cv_status::timeout) {}
        if (!completion_error.empty()) error = completion_error;
        else if (!all_done()) error = "NO_BARRIER drain timeout";
    }
    if (!error.empty()) {
        terminate();
        return false;
    }
    stop_completion();
    drain();
    bool success = true;
    for (auto it = configuration.execution_order.rbegin();
         it != configuration.execution_order.rend(); ++it) {
        auto& child = workers[*it].child;
        if (!child) continue;
        std::string local;
        if (!child->stop(local) && success) {
            error = "Task '" + configuration.tasks[*it].id + "': " + local;
            success = false;
        }
    }
    terminate();
    return success;
}

void manager::terminate() noexcept {
    stop_completion();
    drain();
    for (auto& worker : workers) if (worker.child) worker.child->terminate();
    executor.reset();
    trace.reset(); // flush and join before unloading Project state
    workers.clear();
    dependency_indices.clear();
    configuration = {};
    completion_diagnostics.clear();
    completion_busy.clear();
    completion_deadline.clear();
    completion_cycle.clear();
    completion_error.clear();
    completion_stop = false;
    completion_stopping = false;
    started = false;
}
}
