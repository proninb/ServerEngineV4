#pragma once
#include <cstddef>
#include <cstdint>
namespace cw::server::detail {
#ifdef CW_RUNTIME_OBJECT_LANES_AB
// Set before lifecycle invocation; worker functions never mutate these controls.
inline std::size_t object_lane_cap = 0;
inline std::size_t last_object_lanes = 0;
inline std::uint64_t last_object_phase_ns = 0;
inline bool object_pretouch = false;
inline std::uint64_t last_pretouch_ns = 0;
inline std::size_t last_pretouch_bytes = 0;
#endif
}
