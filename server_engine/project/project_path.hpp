/*
 * Project filesystem path boundary.
 *
 * Portable resolution establishes absolute normalized locators. Platform
 * implementations derive filesystem-equivalence keys used only for
 * deduplication and recursion-cycle detection.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace cw::server {

struct project_path_key final {
    std::filesystem::path value;

    friend bool operator==(
        const project_path_key&,
        const project_path_key&) noexcept = default;
};

struct project_path_key_hash final {
    [[nodiscard]] std::size_t operator()(
        const project_path_key& key) const noexcept {

        return std::filesystem::hash_value(
            key.value);
    }
};

enum class project_path_result : std::uint8_t {
    success,
    failed,
};

[[nodiscard]] project_path_result resolve_project_path(
    const std::filesystem::path& path,
    std::filesystem::path& output) noexcept;

[[nodiscard]] project_path_result make_project_path_key(
    const std::filesystem::path& path,
    project_path_key& output) noexcept;

}
