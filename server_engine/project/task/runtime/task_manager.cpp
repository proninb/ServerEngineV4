#include "task_manager.hpp"
#include <future>
#include <unordered_map>
#include <utility>

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
    started = true;
    return true;
}

bool manager::tick(std::uint64_t cycle, bool frozen, std::string& error) {
    if (!started) { error = "Task Manager not started"; return false; }
    const auto count = configuration.tasks.size();
    std::vector<bool> scheduled(count, false);
    std::unordered_map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < count; ++i) {
        ids.emplace(configuration.tasks[i].id, i);
        const auto& task = configuration.tasks[i];
        scheduled[i] = task.enabled && task_is_scheduled(task.settings, cycle, frozen);
    }
    // Each wave contains independent tasks. Join every worker before the next wave.
    std::vector<bool> completed(count, false);
    std::size_t remaining = 0;
    for (const bool run : scheduled) if (run) ++remaining;
    while (remaining) {
        std::vector<std::size_t> ready;
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
        struct outcome { bool ok; std::string error; };
        std::vector<std::future<outcome>> pending;
        pending.reserve(ready.size());
        for (const auto index : ready) {
            pending.push_back(std::async(std::launch::async, [this, index, cycle] {
                outcome result{false, {}};
                result.ok = workers[index].child->run(
                    cycle, configuration.tasks[index].settings.calls, result.error);
                return result;
            }));
        }
        bool success = true;
        for (std::size_t i = 0; i < pending.size(); ++i) {
            const auto result = pending[i].get();
            if (!result.ok && success) {
                error = "Task '" + configuration.tasks[ready[i]].id + "': " + result.error;
                success = false;
            }
        }
        // All in-flight requests are finished before failure is reported.
        if (!success) return false;
        for (const auto index : ready) { completed[index] = true; --remaining; }
    }
    return true;
}

bool manager::stop(std::string& error) {
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
    for (auto& worker : workers) if (worker.child) worker.child->terminate();
    workers.clear();
    configuration = {};
    started = false;
}
}
