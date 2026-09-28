/*
 * Transport-neutral observable Server state.
 *
 * Runtime execution states extend this same project_state domain later.
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace cw::server {

enum class project_state : std::uint8_t {
    unloaded = 0,
    loaded = 1,
};

struct server_state_snapshot final {
    project_state project = project_state::unloaded;
};

static_assert(std::is_trivially_copyable_v<server_state_snapshot>);
static_assert(std::is_standard_layout_v<server_state_snapshot>);

}
