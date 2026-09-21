/*
 * ABI-derived layout compatibility boundary.
 *
 * The key identifies the process-wide ABI configuration used to derive an
 * optional persisted Runtime layout from G. It contains no Graph or layout
 * data and does not make ABI-derived state part of G.
 */
#pragma once

#include "../../configuration/server_configuration.hpp"

#include <cstdint>
#include <type_traits>

namespace cw::server {

struct abi_layout_key final {
    abi_target target = abi_target::windows_x64;
    std::uint32_t pack = 8;

    friend constexpr bool operator==(
        const abi_layout_key&,
        const abi_layout_key&) noexcept = default;
};

[[nodiscard]] abi_layout_key make_abi_layout_key(
    const server_abi_configuration& configuration) noexcept;

[[nodiscard]] bool valid_abi_layout_key(
    const abi_layout_key& key) noexcept;

[[nodiscard]] bool abi_layout_compatible(
    const abi_layout_key& key,
    const server_abi_configuration& configuration) noexcept;

static_assert(sizeof(abi_layout_key) == 8);
static_assert(std::is_trivially_copyable_v<abi_layout_key>);
static_assert(std::is_standard_layout_v<abi_layout_key>);

}
