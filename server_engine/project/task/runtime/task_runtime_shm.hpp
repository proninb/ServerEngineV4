/*
 * Task runtime system table (named, paging-file-backed SHM).
 * Server is the sole writer; readers acquire the named mutex before copying.
 * This table is independent of Project SHM v2 and task_control ABI.
 */
#pragma once

#include "task_runtime_table.hpp"
#include "../task_configuration.hpp"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace cw::server::task_runtime {

class runtime_shm final {
public:
    runtime_shm() = default;
    ~runtime_shm() { close(); }
    runtime_shm(const runtime_shm&) = delete;
    runtime_shm& operator=(const runtime_shm&) = delete;

    bool create(const task_configuration& configuration, std::string& error) {
        close();
        error.clear();
        const auto count = configuration.tasks.size();
        if (count > 65535) { error = "Too many Tasks for system SHM"; return false; }
        std::uint64_t dependencies = 0;
        for (const auto& task : configuration.tasks) dependencies += task.dependencies.size();
        if (dependencies > 0xFFFFFFFFull) { error = "Too many Task dependencies"; return false; }
        const auto offset = align_up(sizeof(runtime_table), alignof(task_record));
        const auto deps_offset = align_up(offset + count * sizeof(task_record), alignof(std::uint32_t));
        const auto bytes = deps_offset + dependencies * sizeof(std::uint32_t);
        if (bytes > 0xFFFFFFFFull || bytes < sizeof(runtime_table)) {
            error = "System SHM size is unsupported";
            return false;
        }
        std::vector<task_record> new_records(count);
        std::vector<std::uint32_t> new_dependencies;
        new_dependencies.reserve(static_cast<std::size_t>(dependencies));
        std::unordered_map<std::string, std::uint32_t> ids;
        ids.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            ids.emplace(configuration.tasks[i].id, static_cast<std::uint32_t>(i));
        for (std::size_t i = 0; i < count; ++i) {
            const auto& task = configuration.tasks[i];
            auto& p = new_records[i].parameters;
            if (task.id.size() >= sizeof(p.id) ||
                task.executable.size() >= sizeof(p.executable)) {
                error = "Task id or executable too long for system SHM: " + task.id;
                return false;
            }
            std::memcpy(p.id, task.id.data(), task.id.size());
            std::memcpy(p.executable, task.executable.data(), task.executable.size());
            p.enabled = task.enabled ? 1u : 0u;
            p.run_in_freeze = task.settings.run_in_freeze ? 1u : 0u;
            p.calls = task.settings.calls;
            p.cycles = task.settings.cycles;
            p.starting_from_cycle = task.settings.starting_from_cycle;
            p.dependency_offset = static_cast<std::uint32_t>(new_dependencies.size());
            p.dependency_count = static_cast<std::uint32_t>(task.dependencies.size());
            for (const auto& dependency : task.dependencies) {
                const auto found = ids.find(dependency);
                if (found == ids.end()) {
                    error = "Unknown Task dependency: " + dependency;
                    return false;
                }
                new_dependencies.push_back(found->second);
            }
        }
        // Every LOAD gets a new name: readers may retain the previous view after UNLOAD.
        static std::atomic<std::uint64_t> instance{0};
        const auto pid = std::to_wstring(GetCurrentProcessId());
        name = L"Local\\SEV4_System_" + pid + L"_" +
            std::to_wstring(GetTickCount64()) + L"_" +
            std::to_wstring(instance.fetch_add(1, std::memory_order_relaxed) + 1);
        const auto mutex_name = name + L"_mutex";
        mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
        if (!mutex) { error = "CreateMutexW(system) failed"; close(); return false; }
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(bytes), name.c_str());
        if (!mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
            error = "CreateFileMappingW(system) failed or name already exists";
            close(); return false;
        }
        view = static_cast<std::byte*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
        if (!view) { error = "MapViewOfFile(system) failed"; close(); return false; }
        records = std::move(new_records);
        dependency_indices = std::move(new_dependencies);
        header = new(view) runtime_table{};
        header->task_count = static_cast<std::uint32_t>(count);
        header->dependency_count = static_cast<std::uint32_t>(dependencies);
        header->tasks_offset = static_cast<std::uint32_t>(offset);
        header->dependencies_offset = static_cast<std::uint32_t>(deps_offset);
        header->total_size = bytes;
        system.mode = configuration.timing.mode == task_timing_mode::real_time ? 1u : 0u;
        system.period_us = configuration.timing.period_ms ?
            configuration.timing.period_ms * 1000u : 2000u;
        const auto& diagnostics = configuration.timing.diagnostics;
        system.deadline_enabled = diagnostics.enabled && diagnostics.deadline.enabled ? 1u : 0u;
        system.warn_over_ns = diagnostics.deadline.warn_over_ns;
        system.stop_over_ns = diagnostics.deadline.stop_over_ns;
        system.warn_ahead_ns = diagnostics.enabled && diagnostics.time_drift_enabled ?
            diagnostics.warn_ahead_ns : -1;
        system.warn_behind_ns = diagnostics.enabled && diagnostics.time_drift_enabled ?
            diagnostics.warn_behind_ns : -1;
        update_server_time();
        publish();
        return true;
    }

    void close() noexcept {
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
        if (mutex) CloseHandle(mutex);
        view = nullptr;
        header = nullptr;
        mapping = nullptr;
        mutex = nullptr;
        records.clear();
        dependency_indices.clear();
        name.clear();
        system = {};
        active_since = {};
        last_published = {};
        elapsed_before_pause_ns = 0;
    }

    void set_period_us(std::uint32_t period_us) noexcept {
        system.period_us = period_us;
        publish();
    }
    [[nodiscard]] bool valid() const noexcept { return header != nullptr; }
    [[nodiscard]] const std::wstring& mapping_name() const noexcept { return name; }
    [[nodiscard]] bool auto_stop_enabled() const noexcept {
        return system.deadline_enabled && system.stop_over_ns >= 0;
    }
    [[nodiscard]] bool stop_pending() const noexcept { return system.stop_pending != 0; }

    void on_run(bool restart) noexcept {
        if (restart) {
            for (auto& record : records) record.diagnostics = {};
            system.completed_cycles = 0;
            system.simulation_time_ns = 0;
            system.deadline_misses = 0;
            system.warnings = 0;
            system.max_cycle_ns = 0;
            system.last_cycle_ns = 0;
            system.max_wakeup_lateness_ns = 0;
            system.wakeup_jitter_ns = 0;
            elapsed_before_pause_ns = 0;
            system.stop_pending = 0;
            system.stop_reason = 0;
        }
        active_since = std::chrono::steady_clock::now();
        for (auto& record : records)
            record.diagnostics.state = static_cast<std::uint32_t>(runtime_task_state::idle);
        publish();
    }

    void on_pause() noexcept {
        if (active_since != std::chrono::steady_clock::time_point{}) {
            elapsed_before_pause_ns += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - active_since).count());
            active_since = {};
        }
        system.elapsed_real_ns = elapsed_before_pause_ns;
        update_server_time();
        publish();
    }

    void on_stop(std::uint32_t state) noexcept {
        on_pause();
        for (auto& record : records)
            record.diagnostics.state = static_cast<std::uint32_t>(runtime_task_state::stopped);
        system.project_state = state;
        system.stop_pending = 0;
        publish();
    }

    void on_project_state(std::uint32_t state) noexcept {
        system.project_state = state;
        update_server_time();
        publish();
    }

    // Per-Task control never clears other Task statistics or parameters.
    void task_killed(std::size_t index) noexcept {
        if (index >= records.size()) return;
        records[index].diagnostics.state =
            static_cast<std::uint32_t>(runtime_task_state::stopped);
        update_server_time();
        publish();
    }
    void task_restarted(std::size_t index) noexcept {
        if (index >= records.size()) return;
        records[index].diagnostics.state =
            static_cast<std::uint32_t>(runtime_task_state::idle);
        update_server_time();
        publish();
    }
    void resume_after_all_killed() noexcept {
        // Unlike on_run(true), preserve per-Task diagnostic history.
        active_since = std::chrono::steady_clock::now();
        system.stop_pending = 0;
        system.stop_reason = 0;
        update_server_time();
        publish();
    }

    void task_scheduled(std::size_t index, std::uint64_t cycle) noexcept {
        if (index >= records.size()) return;
        auto& d = records[index].diagnostics;
        d.scheduled++;
        d.last_cycle = cycle;
    }
    void task_started(std::size_t index) noexcept {
        if (index < records.size())
            records[index].diagnostics.state = static_cast<std::uint32_t>(runtime_task_state::running);
    }
    void task_completed(std::size_t index, std::uint64_t cycle, std::uint64_t duration_ns) noexcept {
        if (index >= records.size()) return;
        auto& d = records[index].diagnostics;
        d.last_cycle = cycle;
        d.completed++;
        d.total_calls += records[index].parameters.calls;
        d.last_duration_ns = duration_ns;
        d.total_duration_ns += duration_ns;
        if (d.min_duration_ns == 0 || duration_ns < d.min_duration_ns) d.min_duration_ns = duration_ns;
        if (duration_ns > d.max_duration_ns) d.max_duration_ns = duration_ns;
        d.state = static_cast<std::uint32_t>(runtime_task_state::completed);
        d.deadline_delta_ns = static_cast<std::int64_t>(duration_ns) -
            static_cast<std::int64_t>(system.period_us) * 1000;
        if (system.deadline_enabled && d.deadline_delta_ns > 0) {
            d.deadline_misses++;
            if (d.deadline_delta_ns > system.warn_over_ns) d.warnings++;
        }
    }
    void task_failed(std::size_t index, std::uint64_t cycle, std::uint32_t code) noexcept {
        if (index >= records.size()) return;
        auto& d = records[index].diagnostics;
        d.last_cycle = cycle;
        d.last_error_cycle = cycle;
        d.last_error_code = code;
        d.failed++;
        d.state = static_cast<std::uint32_t>(runtime_task_state::failed);
    }

    // Called after the full per-cycle LOCKSTEP barrier, never within TASKMAIN.
    // deadline_delta_ns includes wakeup lateness in NORMAL mode.
    void cycle_completed(std::uint64_t cycle, std::uint64_t duration_ns,
                         std::int64_t deadline_delta_ns,
                         std::int64_t wakeup_jitter_ns, bool frozen, bool manual_kill = false) noexcept {
        if (!valid()) return;
        system.last_cycle_ns = duration_ns;
        system.max_cycle_ns = (std::max)(system.max_cycle_ns, duration_ns);
        system.deadline_delta_ns = deadline_delta_ns;
        system.wakeup_jitter_ns = wakeup_jitter_ns;
        if (wakeup_jitter_ns > 0)
            system.max_wakeup_lateness_ns = (std::max)(system.max_wakeup_lateness_ns,
                static_cast<std::uint64_t>(wakeup_jitter_ns));
        if (!frozen && deadline_delta_ns > 0 && system.deadline_enabled) {
            ++system.deadline_misses;
            if (deadline_delta_ns > system.warn_over_ns) ++system.warnings;
            if (!manual_kill && system.stop_over_ns >= 0 && deadline_delta_ns > system.stop_over_ns) {
                system.stop_pending = 1;
                system.stop_reason = 1;
            }
            if (system.stop_pending) system.last_error_cycle = cycle;
        }
        if (!frozen) {
            ++system.completed_cycles;
            system.simulation_time_ns += static_cast<std::uint64_t>(system.period_us) * 1000u;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto active_ns = active_since != std::chrono::steady_clock::time_point{} ?
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - active_since).count()) : 0;
        system.elapsed_real_ns = elapsed_before_pause_ns + active_ns;
        system.speed_ppm = system.elapsed_real_ns
            ? static_cast<std::uint64_t>((static_cast<long double>(system.simulation_time_ns) * 1000000.0L) /
                static_cast<long double>(system.elapsed_real_ns)) : 0;
        system.time_drift_ns = static_cast<std::int64_t>(system.simulation_time_ns) -
            static_cast<std::int64_t>(system.elapsed_real_ns);
        // Drift is informational in REAL_TIME; it is only a warning in NORMAL.
        if (system.mode == 0 &&
            ((system.warn_ahead_ns >= 0 && system.time_drift_ns > system.warn_ahead_ns) ||
             (system.warn_behind_ns >= 0 && system.time_drift_ns < -system.warn_behind_ns)))
            ++system.warnings;
        if (system.stop_pending ||
            last_published == std::chrono::steady_clock::time_point{} ||
            now - last_published >= std::chrono::milliseconds(50)) {
            update_server_time();
            publish();
        }
    }

    // Readers must OpenMutexW(name + L"_mutex") and acquire it before copying.
    // Publishing never stalls the cycle on a slow reader; it may skip an update.
    void publish() noexcept {
        if (!header || !mutex) return;
        const DWORD locked = WaitForSingleObject(mutex, 0);
        if (locked != WAIT_OBJECT_0 && locked != WAIT_ABANDONED) return;
        header->system = system;
        if (!records.empty()) std::memcpy(view + header->tasks_offset,
            records.data(), records.size() * sizeof(task_record));
        if (!dependency_indices.empty()) std::memcpy(view + header->dependencies_offset,
            dependency_indices.data(), dependency_indices.size() * sizeof(std::uint32_t));
        ++header->generation;
        last_published = std::chrono::steady_clock::now();
        ReleaseMutex(mutex);
    }

private:
    void update_server_time() noexcept {
        system.server_time_unix_ns = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }
    HANDLE mapping = nullptr;
    HANDLE mutex = nullptr;
    std::byte* view = nullptr;
    runtime_table* header = nullptr;
    std::wstring name;
    std::vector<task_record> records;
    std::vector<std::uint32_t> dependency_indices;
    runtime_diagnostics system{};
    std::chrono::steady_clock::time_point active_since{};
    std::chrono::steady_clock::time_point last_published{};
    std::uint64_t elapsed_before_pause_ns = 0;
};

} // namespace cw::server::task_runtime
