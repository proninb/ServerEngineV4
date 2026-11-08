/*
 * Task process orchestration and strict per-tick barrier.
 * Owns child processes; never owns Project SHM or Graph.
 */
#pragma once
#include "../task_configuration.hpp"
#include "task_process.hpp"
#include "task_runtime_shm.hpp"
#include "task_trace_buffer.hpp"
#include "task_executor.hpp"
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <optional>

namespace cw::server::task_runtime {
class manager final {
public:
    manager() = default;
    ~manager();
    manager(const manager&) = delete;
    manager& operator=(const manager&) = delete;

    bool start(task_configuration configuration,
               const std::filesystem::path& project_directory,
               std::uint32_t timeout_ms, std::string& error);
    bool start(task_configuration configuration,
               const std::filesystem::path& project_directory,
               std::uint32_t timeout_ms, const shm_attachment& attachment,
               std::string& error);
    struct no_barrier_diagnostics {
        std::uint64_t scheduled = 0;
        std::uint64_t dispatched = 0;
        std::uint64_t completed = 0;
        std::uint64_t reaped_on_tick = 0;
        std::uint64_t skipped_busy = 0;
        std::uint64_t failed = 0;
        std::uint64_t first_skipped_cycle = 0;
        std::uint64_t first_skipped_qpc = 0;
        std::uint64_t max_tick_lock_wait_us = 0;
        std::uint64_t max_dispatch_us = 0;
    };
    [[nodiscard]] std::vector<no_barrier_diagnostics> no_barrier_snapshot();
    void set_system_table(runtime_shm* value) noexcept { system_table = value; }
    // KILL is preemptive for one child only. Caller commits after the current
    // scheduler barrier. A killed dependency never blocks an active successor.
    bool request_kill(const std::string& id, std::size_t& index, std::string& error);
    void finish_kill(std::size_t index) noexcept;
    // Caller holds Server scheduler_mutex. TASKINIT executes at this boundary.
    bool restart_one(const std::string& id, std::size_t& index, std::string& error);
    [[nodiscard]] std::size_t active_count() const noexcept;
    [[nodiscard]] bool take_manual_kill() noexcept {
        return manual_kill.exchange(false, std::memory_order_acq_rel);
    }
    bool tick(std::uint64_t cycle, bool frozen, std::string& error);
    bool tick_overlap(std::uint64_t cycle, bool frozen, std::string& error);
    bool reap(std::string& error);
    void drain() noexcept;
    bool quiesce(std::string& error);
    bool stop(std::string& error);
    void terminate() noexcept;
    [[nodiscard]] bool active() const noexcept { return started; }
private:
    struct worker {
        std::unique_ptr<process> child;
        std::shared_future<std::pair<bool, std::string>> pending;
        bool busy = false;
    };
    runtime_shm* system_table = nullptr; // non-owning; Server owns mapping
    mutable std::mutex supervisor_mutex;
    std::unique_ptr<std::atomic_bool[]> kill_requested;
    std::atomic_bool manual_kill{false};
    std::filesystem::path project_directory;
    std::optional<shm_attachment> saved_attachment;
    std::uint32_t task_timeout_ms = 5000;
    task_configuration configuration;
    std::vector<worker> workers;
    // Immutable indices resolved at start; avoids string lookup per tick.
    std::vector<std::vector<std::size_t>> dependency_indices;
    std::unique_ptr<task_executor> executor;
    std::unique_ptr<task_trace_buffer> trace;
    bool tick_no_barrier(std::uint64_t cycle, bool frozen, std::string& error);
    void completion_loop() noexcept;
    void stop_completion() noexcept;
    std::thread completion_thread;
    std::mutex completion_mutex;
    HANDLE completion_wake = nullptr;
    std::vector<no_barrier_diagnostics> completion_diagnostics;
    std::vector<unsigned char> completion_busy;
    std::vector<ULONGLONG> completion_deadline;
    std::vector<std::uint64_t> completion_cycle;
    std::string completion_error;
    std::condition_variable completion_cv;
    bool completion_stopping = false;
    bool completion_stop = false;
    bool started = false;
};
}
