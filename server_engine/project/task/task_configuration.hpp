/*
 * Task configuration and deterministic dependency scheduling.
 * No Project G or SHM ownership.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cw::server {

struct task_settings {
    std::uint32_t calls = 1;
    std::uint64_t cycles = 1;
    std::uint64_t starting_from_cycle = 1;
    bool run_in_freeze = false;
};

struct task_definition {
    std::string id;
    std::string executable;
    std::vector<std::string> dependencies;
    task_settings settings;
    bool enabled = true;
};

enum class task_timing_mode : std::uint32_t { normal = 0, real_time = 1 };

struct task_deadline_settings {
    bool enabled = false;
    std::int64_t warn_over_ns = 0;
    std::int64_t stop_over_ns = -1; // disabled unless explicitly specified
};

struct task_diagnostics_settings {
    bool enabled = true;
    task_deadline_settings deadline;
    bool time_drift_enabled = false;
    std::int64_t warn_ahead_ns = -1;
    std::int64_t warn_behind_ns = -1;
};

struct task_timing_settings {
    task_timing_mode mode = task_timing_mode::normal;
    // Zero: legacy TASK_SCHEDULER_PERIOD_MS or the default 2 ms.
    std::uint32_t period_ms = 0;
    task_diagnostics_settings diagnostics;
};

struct task_configuration {
    task_timing_settings timing;
    std::vector<task_definition> tasks;
    std::vector<std::size_t> execution_order;
};

bool task_is_scheduled(
    const task_settings& settings,
    std::uint64_t cycle,
    bool frozen) noexcept;

bool validate_task_configuration(
    task_configuration& configuration,
    std::string& error);

bool parse_task_configuration(
    std::string_view json,
    task_configuration& output,
    std::string& error);

}
