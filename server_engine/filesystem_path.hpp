/*
 * UTF-8/native filesystem path boundary.
 *
 * Textual and persisted paths use strict UTF-8. Internal filesystem paths use
 * std::filesystem::path in the platform-native representation.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace cw::server {

enum class filesystem_path_result : std::uint8_t {
    success,
    invalid_utf8,
    failed,
};

// Platform filesystem-equivalence identity used only for transient lookup and
// duplicate detection. It is never persisted as physical or semantic identity.
struct filesystem_path_key final {
    std::filesystem::path value;

    friend bool operator==(
        const filesystem_path_key&,
        const filesystem_path_key&) noexcept = default;
};

struct filesystem_path_key_hash final {
    [[nodiscard]] std::size_t operator()(
        const filesystem_path_key& key) const noexcept {

        return std::filesystem::hash_value(
            key.value);
    }
};

[[nodiscard]] filesystem_path_result filesystem_path_from_utf8(
    std::string_view value,
    std::filesystem::path& output) noexcept;

[[nodiscard]] filesystem_path_result filesystem_path_to_utf8(
    const std::filesystem::path& value,
    std::string& output) noexcept;

[[nodiscard]] filesystem_path_result make_filesystem_path_key(
    const std::filesystem::path& path,
    filesystem_path_key& output) noexcept;

}
