/*
 * Persistent Project configuration contract.
 *
 * project.json describes one Project tree. Composition of nested Project
 * references is a separate stage and is not performed by this loader.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cw::server {

enum class project_item_type : std::uint8_t {
    group,
    header,
    source,
    project,
};

struct project_item_configuration {
    project_item_type type = project_item_type::group;
    std::string name;
    std::filesystem::path path;
    std::vector<project_item_configuration> children;
};

enum class abi_target : std::uint8_t {
    windows_x64,
    posix_x64,
};

struct abi_configuration {
    abi_target target = abi_target::windows_x64;
    std::uint32_t pack = 8;
};

struct project_configuration {
    std::uint32_t version = 0;
    std::string name;
    std::vector<project_item_configuration> project;
    abi_configuration abi;
};

inline constexpr std::uint32_t current_project_configuration_version = 1;

}
