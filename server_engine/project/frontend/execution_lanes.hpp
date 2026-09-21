/*
 * Persistent construction execution lanes.
 *
 * start() creates the fixed worker set once. run() reuses those workers for
 * independent physical-file frontiers. Lane zero is the calling owner thread;
 * platform workers own lanes 1..N-1. There is no work queue, mutex, atomic work
 * index, or per-file task object.
 */
#pragma once

#include "../../server_status.hpp"

#include <cstddef>

namespace cw::server {

using execution_lane_function =
    void (*)(void*, std::size_t) noexcept;

[[nodiscard]] std::size_t execution_lane_capacity() noexcept;

class execution_lanes final {
public:
    execution_lanes() = default;
    ~execution_lanes() noexcept;

    execution_lanes(const execution_lanes&) = delete;
    execution_lanes& operator=(const execution_lanes&) = delete;

    [[nodiscard]] server_status start(
        std::size_t lane_count) noexcept;

    [[nodiscard]] server_status run(
        std::size_t active_lanes,
        execution_lane_function function,
        void* context) noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    void* state = nullptr;
};

}
