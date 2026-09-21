/*
 * Project persisted-artifact layout boundary.
 *
 * The layout defines two persistence slots plus one authoritative baseline
 * selector path. Slot choice is crash-safe persistence mechanics, not Project
 * or Graph state.
 */
#pragma once

#include "../file/file_identity.hpp"
#include "../../configuration/server_configuration.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cw::server {

enum class project_artifact_slot : std::uint8_t {
    slot0 = 0,
    slot1 = 1,
};

struct project_artifact_image final {
    std::vector<std::byte> bytes;
    file_content_hash hash{};
};

struct project_artifact_slot_layout final {
    std::filesystem::path directory;
    std::filesystem::path manifest;
    std::filesystem::path source_save;
    std::filesystem::path database;
    std::filesystem::path compiled;
};

struct project_artifact_layout final {
    std::filesystem::path root;
    std::filesystem::path baseline;
    std::array<project_artifact_slot_layout, 2> slots;
};

enum class project_artifact_layout_result : std::uint8_t {
    success,
    failed,
};

[[nodiscard]] project_artifact_layout_result make_project_artifact_layout(
    const std::filesystem::path& root_project_path,
    const server_files_configuration& files,
    project_artifact_layout& output) noexcept;

void finalize_project_artifact_image(
    project_artifact_image& image) noexcept;

}

