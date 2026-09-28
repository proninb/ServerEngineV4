/*
 * Read-only Runtime Query Service.
 *
 * Semantic lookup is performed through mmap-native compiled G. Runtime values
 * are read directly from the published FIXED_DIRECT Runtime image.
 */
#pragma once

#include "runtime_layout.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace cw::server {

enum class runtime_query_result : std::uint8_t {
    success = 0,
    invalid_input,
    not_found,
    unsupported_type,
    invalid_runtime,
};

struct runtime_value final {
    intrinsic_type type = intrinsic_type::none;
    std::uint8_t size = 0;
    std::uint16_t reserved16 = 0;
    std::uint32_t reserved32 = 0;
    std::uint64_t bits = 0;
};

static_assert(sizeof(runtime_value) == 16);
static_assert(std::is_trivially_copyable_v<runtime_value>);
static_assert(std::is_standard_layout_v<runtime_value>);

[[nodiscard]] runtime_query_result get_runtime_value(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    std::string_view name,
    runtime_value& output) noexcept;

}
