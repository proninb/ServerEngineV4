/*
 * Runtime IC semantic bridge.
 *
 * IC paths are resolved against the current mmap-native compiled G and the
 * resident Runtime binding sidecar. No IC lookup/index is resident in Project
 * state and no persisted IC identity depends on Graph slots or Runtime offsets.
 */
#pragma once

#include "runtime_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cw::server {

enum class snap_ic_options : std::uint8_t {
    none = 0x00,
};

// RESET option bits exclude categories from update.
// A set bit preserves that category in the current Runtime.
enum class reset_ic_options : std::uint8_t {
    none = 0x00,
    constants = 0x01,
    variables = 0x02,
    defaults = 0x04,
};

inline constexpr std::uint8_t reset_ic_options_mask =
    static_cast<std::uint8_t>(
        reset_ic_options::constants) |
    static_cast<std::uint8_t>(
        reset_ic_options::variables) |
    static_cast<std::uint8_t>(
        reset_ic_options::defaults);

[[nodiscard]] constexpr reset_ic_options operator|(
    reset_ic_options left,
    reset_ic_options right) noexcept {

    return static_cast<reset_ic_options>(
        static_cast<std::uint8_t>(left) |
        static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr reset_ic_options operator&(
    reset_ic_options left,
    reset_ic_options right) noexcept {

    return static_cast<reset_ic_options>(
        static_cast<std::uint8_t>(left) &
        static_cast<std::uint8_t>(right));
}

constexpr reset_ic_options& operator|=(
    reset_ic_options& left,
    reset_ic_options right) noexcept {

    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_option(
    reset_ic_options value,
    reset_ic_options option) noexcept {

    return (value & option) !=
        reset_ic_options::none;
}

[[nodiscard]] constexpr bool valid_reset_ic_options(
    reset_ic_options value) noexcept {

    return (
        static_cast<std::uint8_t>(value) &
        ~reset_ic_options_mask) == 0;
}

enum class runtime_ic_result : std::uint8_t {
    success = 0,
    invalid_input,
    not_found,
    unsupported_type,
    type_mismatch,
    invalid_runtime,
};

struct runtime_ic_path_view final {
    std::span<const std::string_view> object;
    std::span<const std::string_view> members;
};

inline constexpr std::uint8_t runtime_ic_target_const =
    0x01u;

inline constexpr std::uint8_t runtime_ic_target_default =
    0x02u;

struct runtime_ic_scalar_target final {
    runtime_offset offset = 0;
    intrinsic_type type = intrinsic_type::none;
    std::uint8_t size = 0;
    std::uint8_t flags = 0;

    [[nodiscard]] constexpr bool constant() const noexcept {
        return (flags & runtime_ic_target_const) != 0;
    }

    [[nodiscard]] constexpr bool default_value() const noexcept {
        return (flags & runtime_ic_target_default) != 0;
    }
};

// Transient scalar value used between the Runtime semantic bridge and an IC
// codec. It is not the persisted binary IC record format.
struct runtime_ic_scalar_value final {
    intrinsic_type type = intrinsic_type::none;
    std::uint8_t size = 0;
    std::uint16_t reserved = 0;
    std::array<std::byte, 16> bytes{};
};

struct runtime_ic_record_source final {
    runtime_ic_path_view path;
    runtime_ic_scalar_value value;
};

// Command-local native SNAP representation. G-local string IDs are transient
// keys only; binary codecs translate them to IC-local IDs before persistence.
struct runtime_ic_native_record_source final {
    std::uint32_t object_begin = 0;
    std::uint16_t object_count = 0;
    std::uint16_t member_count = 0;
    std::uint32_t member_begin = 0;
    runtime_ic_scalar_value value;
};

struct runtime_ic_native_source_view final {
    std::span<const string_id> components;
    std::span<const runtime_ic_native_record_source> records;
};

[[nodiscard]] runtime_ic_result resolve_runtime_ic_scalar(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::uint64_t runtime_size,
    runtime_ic_path_view path,
    runtime_ic_scalar_target& output) noexcept;

[[nodiscard]] runtime_ic_result snapshot_runtime_ic_scalar(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    runtime_ic_path_view path,
    runtime_ic_scalar_value& output) noexcept;

[[nodiscard]] runtime_ic_result reset_runtime_ic_scalar(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    runtime_ic_path_view path,
    const runtime_ic_scalar_value& value) noexcept;

}
