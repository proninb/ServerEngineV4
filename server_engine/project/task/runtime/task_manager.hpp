/*
 * Task process orchestration and strict per-tick barrier.
 * Owns child processes; never owns Project SHM or Graph.
 */
#pragma once
#include "../task_configuration.hpp"
#include "task_process.hpp"
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
    bool tick(std::uint64_t cycle, bool frozen, std::string& error);
    bool tick_overlap(std::uint64_t cycle, bool frozen, std::string& error);
    bool reap(std::string& error);
    void drain() noexcept;
    bool stop(std::string& error);
    void terminate() noexcept;
    [[nodiscard]] bool active() const noexcept { return started; }
private:
    struct worker {
        std::unique_ptr<process> child;
        std::shared_future<std::pair<bool, std::string>> pending;
        bool busy = false;
    };
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
