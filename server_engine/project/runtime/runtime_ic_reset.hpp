/*
 * Whole-Project Runtime IC reset.
 *
 * RESET resolves every persisted semantic path against the current G and
 * current Runtime bindings before changing Runtime. The transient write plan
 * contains only current offsets plus scalar bytes and is discarded after the
 * command. Validation failure leaves Runtime unchanged.
 */
#pragma once

#include "runtime_ic.hpp"
#include "runtime_ic_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cw::server {

enum class runtime_ic_reset_result : std::uint8_t {
    success = 0,
    invalid_input,
    invalid_image,
    not_found,
    unsupported_type,
    type_mismatch,
    invalid_runtime,
    failed,
};

struct runtime_ic_reset_stats final {
    std::uint32_t records = 0;
    std::uint64_t bytes = 0;
};

[[nodiscard]] runtime_ic_reset_result
reset_runtime_ic_records(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    std::span<const runtime_ic_record_source> records,
    runtime_ic_reset_stats* stats = nullptr) noexcept;

[[nodiscard]] runtime_ic_reset_result
reset_runtime_ic_binary(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    const runtime_ic_binary_view& image,
    runtime_ic_reset_stats* stats = nullptr) noexcept;

[[nodiscard]] runtime_ic_reset_result
reset_runtime_ic_text(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    std::string_view text,
    runtime_ic_reset_stats* stats = nullptr,
    std::size_t* error_line = nullptr) noexcept;

}
