/*
 * ServerEngineV4 Task Runtime diagnostic SHM, version 1.
 * One system header, contiguous task records and dependency indices.
 * No process-local pointers or standard-library containers in the ABI.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cw::server::task_runtime {

inline constexpr std::uint32_t runtime_table_magic = 0x31544253u; // SBT1
inline constexpr std::uint32_t runtime_table_version = 1;

enum class runtime_timing_mode : std::uint32_t { normal = 0, real_time = 1 };
enum class runtime_task_state : std::uint32_t {
    idle = 0, running = 1, completed = 2, failed = 3, stopped = 4
};

struct alignas(64) runtime_diagnostics {
    std::uint32_t mode = 0;
    std::uint32_t period_us = 2000;
    std::uint32_t deadline_enabled = 0;
    std::uint32_t stop_pending = 0;

    std::int64_t warn_over_ns = 0;
    std::int64_t stop_over_ns = -1;
    std::int64_t warn_ahead_ns = -1;
    std::int64_t warn_behind_ns = -1;

    std::int64_t server_time_unix_ns = 0;
    std::uint64_t simulation_time_ns = 0;
    std::uint64_t elapsed_real_ns = 0;
    std::uint64_t speed_ppm = 0; // 1000000 = 1.0x

    std::uint64_t completed_cycles = 0;
    std::uint64_t last_cycle_ns = 0;
    std::uint64_t max_cycle_ns = 0;
    std::int64_t deadline_delta_ns = 0;
    std::int64_t time_drift_ns = 0;
    std::int64_t wakeup_jitter_ns = 0; // NORMAL: positive = late wakeup
    std::uint64_t max_wakeup_lateness_ns = 0;
    std::uint64_t deadline_misses = 0;
    std::uint64_t warnings = 0;
    std::uint64_t last_error_cycle = 0;
    std::uint32_t project_state = 0;
    std::uint32_t stop_reason = 0; // 1 = automatic deadline STOP
};

struct task_parameters {
    char id[96]{};
    char executable[512]{};
    std::uint32_t enabled = 1;
    std::uint32_t run_in_freeze = 0;
    std::uint32_t calls = 1;
    std::uint32_t dependency_offset = 0; // index in dependency array
    std::uint32_t dependency_count = 0;
    std::uint32_t reserved = 0;
    std::uint64_t cycles = 1;
    std::uint64_t starting_from_cycle = 1;
};

struct alignas(64) task_diagnostics {
    std::uint32_t state = 0;
    std::uint32_t last_error_code = 0;
    std::uint64_t last_cycle = 0;
    std::uint64_t scheduled = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t total_calls = 0;
    std::uint64_t last_duration_ns = 0;
    std::uint64_t min_duration_ns = 0;
    std::uint64_t max_duration_ns = 0;
    std::uint64_t total_duration_ns = 0;
    std::int64_t deadline_delta_ns = 0;
    std::uint64_t deadline_misses = 0;
    std::uint64_t warnings = 0;
    std::uint64_t last_error_cycle = 0;
};

struct alignas(64) task_record {
    task_parameters parameters;
    task_diagnostics diagnostics;
};

struct alignas(64) runtime_table {
    std::uint32_t magic = runtime_table_magic;
    std::uint32_t version = runtime_table_version;
    char name[32] = "system";
    std::uint32_t task_count = 0;
    std::uint32_t dependency_count = 0;
    std::uint32_t tasks_offset = 0;
    std::uint32_t dependencies_offset = 0;
    std::uint64_t total_size = 0;
    std::uint64_t generation = 0;
    runtime_diagnostics system;
};

constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) / alignment * alignment;
}

static_assert(std::is_standard_layout_v<runtime_table>);
static_assert(std::is_trivially_copyable_v<runtime_table>);
static_assert(std::is_standard_layout_v<task_record>);
static_assert(std::is_trivially_copyable_v<task_record>);
static_assert(alignof(runtime_table) == 64);
static_assert(alignof(task_record) == 64);

} // namespace cw::server::task_runtime
