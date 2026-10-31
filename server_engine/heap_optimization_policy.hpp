#pragma once

#include <cstdint>

namespace cw::server {

// Workload-calibrated candidate threshold, not a universal server memory budget.
inline constexpr std::uint64_t unload_heap_threshold = 120635392;

enum class heap_optimization_state {
    disabled, below_threshold, optimized, unavailable,
    read_failed, optimize_failed, post_read_failed, still_above_threshold,
};

struct heap_optimization_result {
    heap_optimization_state state = heap_optimization_state::disabled;
    std::uint64_t before = 0;
    std::uint64_t after = 0;
    std::uint32_t error = 0;
};

// Backend methods are noexcept. No Project references, allocations or retries.
template<class Backend>
[[nodiscard]] heap_optimization_result optimize_unloaded_heap(
    bool enabled, Backend& backend) noexcept {
    heap_optimization_result result;
    if (!enabled) return result;
    if (!backend.available()) {
        result.state = heap_optimization_state::unavailable;
        return result;
    }
    if (!backend.read(result.before, result.error)) {
        result.state = heap_optimization_state::read_failed;
        return result;
    }
    if (result.before < unload_heap_threshold) {
        result.state = heap_optimization_state::below_threshold;
        return result;
    }
    if (!backend.optimize(result.error)) {
        result.state = heap_optimization_state::optimize_failed;
        return result;
    }
    if (!backend.read(result.after, result.error)) {
        result.state = heap_optimization_state::post_read_failed;
        return result;
    }
    result.state = result.after >= unload_heap_threshold
        ? heap_optimization_state::still_above_threshold
        : heap_optimization_state::optimized;
    return result;
}

}
