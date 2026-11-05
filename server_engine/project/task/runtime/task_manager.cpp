#include "task_manager.hpp"
#include "task_trace_buffer.hpp"
#include "task_perf_probe.hpp"
#include <future>
#include <chrono>
#include <unordered_map>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace cw::server::task_runtime {
manager::~manager() { terminate(); }

bool manager::start(task_configuration value,
                    const std::filesystem::path& project_directory,
                    std::uint32_t timeout_ms, const shm_attachment& attachment,
                    std::string& error) {
    terminate();
    if (!validate_task_configuration(value, error)) return false;
    configuration = std::move(value);
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
        executor = std::make_unique<task_executor>(workers.size());
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
    started = true;
    return true;
}

bool manager::start(task_configuration value,
                    const std::filesystem::path& project_directory,
                    std::uint32_t timeout_ms, std::string& error) {
    terminate();
    if (!validate_task_configuration(value, error)) return false;
    configuration = std::move(value);
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
        executor = std::make_unique<task_executor>(workers.size());
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
    started = true;
    return true;
}

bool manager::tick(std::uint64_t cycle, bool frozen, std::string& error) {
    const auto perf_start = perf::clock::now();
    if (!started) { error = "Task Manager not started"; return false; }
    const auto count = configuration.tasks.size();
    // TASK_RUNTIME_PERF_34: retain scratch capacity between LOCKSTEP ticks.
    // Only the scheduler thread calls tick(); no state escapes the invocation.
    static thread_local std::vector<bool> scheduled;
    static thread_local std::vector<bool> completed;
    static thread_local std::vector<std::size_t> ready;
    static thread_local std::vector<std::future<std::pair<bool, std::string>>> pending;
    static thread_local std::unordered_map<std::string, std::size_t> ids;
    scheduled.assign(count, false);
    completed.assign(count, false);
    ids.clear();
    ids.reserve(count);
    ready.clear();
    pending.clear();
    ready.reserve(count);
    pending.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        ids.emplace(configuration.tasks[i].id, i);
        const auto& task = configuration.tasks[i];
        scheduled[i] = task.enabled && task_is_scheduled(task.settings, cycle, frozen);
    }
    for (std::size_t i = 0; i < count; ++i)
        if (scheduled[i]) trace->record("LOCKSTEP", cycle, configuration.tasks[i].id, "scheduled");
    // Each wave contains independent tasks. Join every worker before the next wave.
    std::size_t remaining = 0;
    for (const bool run : scheduled) if (run) ++remaining;
    while (remaining) {
        ready.clear();
        for (const auto index : configuration.execution_order) {
            if (!scheduled[index] || completed[index]) continue;
            bool blocked = false;
            for (const auto& dependency : configuration.tasks[index].dependencies) {
                const auto dep = ids.at(dependency);
                if (scheduled[dep] && !completed[dep]) { blocked = true; break; }
            }
            if (!blocked) ready.push_back(index);
        }
        if (ready.empty()) { error = "Task dependency scheduling stalled"; return false; }
        // A single ready Task does not need an executor handoff. Keep the
        // scheduler thread in the strict barrier until this Task completes.
        if (ready.size() == 1) {
            const auto index = ready.front();
            trace->record("LOCKSTEP", cycle, configuration.tasks[index].id, "dispatched");
            std::string local;
            const auto run_start = std::chrono::steady_clock::now();
            const auto barrier_start = perf::clock::now();
            const bool ok = workers[index].child->run(
                cycle, configuration.tasks[index].settings.calls, local);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - run_start).count();
            trace->record("LOCKSTEP", cycle, configuration.tasks[index].id,
                          ok ? "completed" : "failed", elapsed);
            perf::record("barrier_wait", cycle, barrier_start);
            if (!ok) {
                error = "Task '" + configuration.tasks[index].id + "': " + local;
                return false;
            }
            completed[index] = true;
            --remaining;
            continue;
        }
        pending.clear();
        for (const auto index : ready) {
            trace->record("LOCKSTEP", cycle, configuration.tasks[index].id, "dispatched");
            pending.push_back(executor->submit( [this, index, cycle] {
                std::pair<bool, std::string> result{false, {}};
                const auto start = std::chrono::steady_clock::now();
                result.first = workers[index].child->run(
                    cycle, configuration.tasks[index].settings.calls, result.second);
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                trace->record("LOCKSTEP", cycle, configuration.tasks[index].id,
                           result.first ? "completed" : "failed", elapsed);
                return result;
            }));
        }
        const auto barrier_start = perf::clock::now();
        bool success = true;
        for (std::size_t i = 0; i < pending.size(); ++i) {
            const auto result = pending[i].get();
            if (!result.first && success) {
                error = "Task '" + configuration.tasks[ready[i]].id + "': " + result.second;
                success = false;
            }
        }
        perf::record("barrier_wait", cycle, barrier_start);
        // All in-flight requests are finished before failure is reported.
        if (!success) return false;
        for (const auto index : ready) { completed[index] = true; --remaining; }
        pending.clear();
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
    drain();
    for (auto& worker : workers) if (worker.child) worker.child->terminate();
    executor.reset();
    trace.reset(); // flush and join before unloading Project state
    workers.clear();
    configuration = {};
    started = false;
}
}
