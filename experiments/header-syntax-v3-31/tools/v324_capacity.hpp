// V3-24-FIX3: benchmark-only amortized growth for direct Graph member storage.
// This helper changes capacity only; it never changes logical Graph layout,
// member order, WHO, WHERE, or the persisted ABI.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cw::server::v324_capacity {

template<class Vector>
[[nodiscard]] bool ensure(Vector& values, std::size_t required) {
    constexpr std::size_t maximum_graph_members =
        static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)());
    const std::size_t maximum = (std::min)(values.max_size(), maximum_graph_members);
    if (required > maximum) return false;
    if (required <= values.capacity()) return true;

    // Old V3-24 called reserve(size + next_record_members) on EVERY record,
    // reallocating and copying the full Graph thousands of times. Geometric
    // growth amortizes the transfers. The starting capacity is intentionally
    // small for fixtures and grows to the corpus size without per-record realloc.
    std::size_t target = values.capacity();
    if (target < 1024) target = (std::min)(maximum, std::size_t{1024});
    while (target < required) {
        if (target > maximum / 2) {
            target = maximum;
            break;
        }
        target *= 2;
    }
    values.reserve(target);
    return true;
}

} // namespace cw::server::v324_capacity
