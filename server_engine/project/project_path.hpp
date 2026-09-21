/*
 * Project filesystem path boundary.
 *
 * Portable resolution establishes absolute normalized locators. Platform
 * implementations derive filesystem-equivalence keys used at Project
 * construction identity boundaries without changing the physical I/O path.
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
