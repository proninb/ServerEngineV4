/*
 * Platform-specific Project filesystem path identity boundary.
 *
 * Composition uses this key only for path-equivalence decisions such as
 * deduplication and recursion-cycle detection. Manifest paths themselves remain
 * normalized root-relative locators.
 */
#pragma once

#include <cstddef>
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

[[nodiscard]] project_path_key make_project_path_key(
    const std::filesystem::path& path);

}
