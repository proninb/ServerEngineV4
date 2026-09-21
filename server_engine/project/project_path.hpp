/*
 * Project filesystem path boundary.
 *
 * Establishes absolute normalized Project locators. Platform filesystem
 * equivalence belongs to the common filesystem_path boundary.
 */
#pragma once

#include <cstdint>
#include <filesystem>

namespace cw::server {

enum class project_path_result : std::uint8_t {
    success,
    failed,
};

[[nodiscard]] project_path_result resolve_project_path(
    const std::filesystem::path& path,
    std::filesystem::path& output) noexcept;

}
