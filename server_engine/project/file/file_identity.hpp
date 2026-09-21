/*
 * Physical-file stable acquisition and exact byte-identity boundary.
 *
 * Per-file change tokens are transient construction identity. A persisted
 * volume-journal checkpoint may accelerate BUILD change discovery, but exact
 * content equality is always SHA-256.
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

enum class file_change_backend : std::uint32_t {
    none = 0,
    windows_usn = 1,
};

struct file_change_checkpoint final {
    file_change_backend backend =
        file_change_backend::none;

    std::uint64_t volume_serial = 0;
    std::uint64_t journal_id = 0;
    std::int64_t next_usn = 0;

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return backend !=
            file_change_backend::none;
    }
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

struct file_content_hash final {
    std::array<std::byte, 32> bytes{};

    friend constexpr bool operator==(
        const file_content_hash&,
        const file_content_hash&) noexcept = default;
};

struct file_content_snapshot final {
    file_snapshot_observation observation{};
    file_content_hash content_hash{};
    file_change_token change_token{};
    std::string bytes;
    bool change_token_available = false;
};

// Stable physical proof when callers need byte identity but not the bytes.
struct file_content_proof final {
    file_snapshot_observation observation{};
    file_content_hash content_hash{};
    file_change_token change_token{};
    bool change_token_available = false;
};

enum class file_content_result : std::uint8_t {
    acquired,
    missing,
    changed_during_read,
    failed,
    allocation_failed,
};

enum class file_token_result : std::uint8_t {
    available,
    unavailable,
    missing,
    failed,
};

[[nodiscard]] file_content_hash hash_file_content(
    std::string_view bytes) noexcept;

[[nodiscard]] file_token_result capture_file_change_token(
    const std::filesystem::path& path,
    file_change_token& output) noexcept;

[[nodiscard]] file_token_result prove_file_unchanged(
    const std::filesystem::path& path,
    const file_change_token& token,
    bool& unchanged) noexcept;

// Captures the volume USN position used by source.bin. unavailable is a normal
// acceleration fallback and never makes Project construction fail.
[[nodiscard]] file_token_result capture_file_change_checkpoint(
    const std::filesystem::path& anchor,
    file_change_checkpoint& output) noexcept;

[[nodiscard]] file_content_result acquire_file_content(
    const std::filesystem::path& path,
    file_content_snapshot& output) noexcept;

[[nodiscard]] file_content_result acquire_file_content_proof(
    const std::filesystem::path& path,
    file_content_proof& output) noexcept;

}
