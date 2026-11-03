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

struct task_configuration {
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
