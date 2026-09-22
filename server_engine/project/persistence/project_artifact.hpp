/*
 * Project persisted-artifact path and image boundary.
 *
 * The four artifacts are independent files under one Project artifact
 * directory. LOAD requires only compiled.bin. BUILD uses the persisted
 * construction artifacts it needs; missing required BUILD state means REBUILD.
 */
#pragma once

#include "../file/file_identity.hpp"
#include "../../configuration/server_configuration.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cw::server {

struct project_artifact_image final {
    std::vector<std::byte> bytes;
    file_content_hash hash{};
};

struct project_artifact_layout final {
    std::filesystem::path root;
    std::filesystem::path manifest;
    std::filesystem::path source_save;
    std::filesystem::path database;
    std::filesystem::path compiled;
};

enum class project_artifact_layout_result : std::uint8_t {
    success,
    failed,
};

enum class project_artifact_io_result : std::uint8_t {
    success,
    failed,
};

[[nodiscard]] project_artifact_layout_result make_project_artifact_layout(
    const std::filesystem::path& root_project_path,
    const server_files_configuration& files,
    project_artifact_layout& output) noexcept;

void finalize_project_artifact_image(
    project_artifact_image& image) noexcept;

[[nodiscard]] project_artifact_io_result
ensure_project_artifact_directory(
    const project_artifact_layout& layout) noexcept;

[[nodiscard]] project_artifact_io_result
remove_project_artifacts(
    const project_artifact_layout& layout) noexcept;

}
