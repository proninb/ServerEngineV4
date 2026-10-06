/*
 * Project persisted-artifact path and image boundary.
 *
 * Persisted Project artifacts are independent files under one Project
 * directory. LOAD requires compiled.bin (G) plus runtime.bin (ABI Runtime).
 * BUILD additionally uses its source/database/manifest acceleration state.
 */
#pragma once

#include "../file/file_identity.hpp"
#include "../../configuration/server_configuration.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace cw::server {

struct project_artifact_layout final {
    std::filesystem::path root;
    std::filesystem::path manifest;
    std::filesystem::path source_save;
    std::filesystem::path database;
    std::filesystem::path compiled;
    std::filesystem::path runtime;
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

[[nodiscard]] project_artifact_io_result
ensure_project_artifact_directory(
    const project_artifact_layout& layout) noexcept;

[[nodiscard]] project_artifact_io_result
remove_project_artifacts(
    const project_artifact_layout& layout) noexcept;

}
