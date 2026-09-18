/*
 * Project configuration-file physical identity and stable acquisition boundary.
 *
 * Filesystem observation is only a cheap hint. file_change_token may prove O(1)
 * unchanged state on supported filesystems. SHA-256 identifies exact bytes.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace cw::server {

struct file_snapshot_observation final {
    std::int64_t write_time_ticks = 0;
    std::uintmax_t size = 0;

    friend constexpr bool operator==(
        const file_snapshot_observation&,
        const file_snapshot_observation&) noexcept = default;
};

struct file_change_token final {
    std::uint64_t volume_serial = 0;
    std::uint64_t file_reference = 0;
    std::int64_t file_usn = -1;

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return volume_serial != 0 &&
            file_reference != 0 &&
            file_usn >= 0;
    }
};

struct project_content_hash final {
    std::array<std::byte, 32> bytes{};

    friend constexpr bool operator==(
        const project_content_hash&,
        const project_content_hash&) noexcept = default;
};

// Reserved for future canonical semantic composition. It is deliberately not
// the aggregate configuration-manifest hash.
struct project_semantic_fingerprint final {
    std::array<std::byte, 32> bytes{};

    friend constexpr bool operator==(
        const project_semantic_fingerprint&,
        const project_semantic_fingerprint&) noexcept = default;
};

struct project_content_snapshot final {
    file_snapshot_observation observation{};
    project_content_hash content_hash{};
    file_change_token change_token{};
    std::string bytes;
    bool change_token_available = false;
};

enum class project_snapshot_result : std::uint8_t {
    acquired,
    missing,
    changed_during_read,
    failed,
    allocation_failed,
};

enum class project_token_result : std::uint8_t {
    available,
    unavailable,
    missing,
    failed,
};

[[nodiscard]] project_content_hash hash_project_content(
    std::string_view bytes) noexcept;

[[nodiscard]] project_token_result capture_file_change_token(
    const std::filesystem::path& path,
    file_change_token& output) noexcept;

[[nodiscard]] project_token_result prove_file_unchanged(
    const std::filesystem::path& path,
    const file_change_token& token,
    bool& unchanged) noexcept;

[[nodiscard]] project_snapshot_result acquire_project_content(
    const std::filesystem::path& path,
    project_content_snapshot& output) noexcept;

}
