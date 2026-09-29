/*
 * Transport-neutral observable Server state.
 *
 * Runtime execution and transient IC operations share this one project_state
 * domain; no second lifecycle/runtime state machine exists.
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace cw::server {

enum class project_state : std::uint8_t {
    unloaded = 0,
    loaded = 1,
    run = 2,
    freeze = 3,
    resetting_ic = 4,
    snapping_ic = 5,
};

[[nodiscard]] constexpr bool project_state_can_run(
    project_state state) noexcept {

    return state == project_state::loaded ||
        state == project_state::freeze;
}

[[nodiscard]] constexpr bool project_state_can_freeze(
    project_state state) noexcept {

    return state == project_state::run;
}

[[nodiscard]] constexpr bool project_state_can_unload(
    project_state state) noexcept {

    return state == project_state::loaded ||
        state == project_state::freeze;
}

[[nodiscard]] constexpr bool project_state_can_ic(
    project_state state) noexcept {

    return state == project_state::loaded ||
        state == project_state::freeze;
}

struct server_state_snapshot final {
    project_state project = project_state::unloaded;
};

static_assert(std::is_trivially_copyable_v<server_state_snapshot>);
static_assert(std::is_standard_layout_v<server_state_snapshot>);

}
