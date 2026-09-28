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

struct runtime_ic_scalar_target final {
    runtime_offset offset = 0;
    intrinsic_type type = intrinsic_type::none;
    std::uint8_t size = 0;
    std::uint8_t flags = 0;

    [[nodiscard]] constexpr bool constant() const noexcept {
        return (flags & runtime_ic_target_const) != 0;
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
