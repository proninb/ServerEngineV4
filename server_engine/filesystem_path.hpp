/*
 * UTF-8/native filesystem path boundary.
 *
 * Textual and persisted paths use strict UTF-8. Internal filesystem paths use
 * std::filesystem::path in the platform-native representation.
 */
#pragma once

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

[[nodiscard]] filesystem_path_result filesystem_path_from_utf8(
    std::string_view value,
    std::filesystem::path& output) noexcept;

[[nodiscard]] filesystem_path_result filesystem_path_to_utf8(
    const std::filesystem::path& value,
    std::string& output) noexcept;

}
