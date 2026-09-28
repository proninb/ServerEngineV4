/*
 * Whole-Project Runtime IC snapshot traversal.
 *
 * Traversal reads current G + resident Runtime bindings directly. It stores
 * only command-local semantic paths and scalar values; no Graph slots or
 * Runtime offsets become persistent IC identity.
 */
#pragma once

#include "runtime_ic.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {

enum class runtime_ic_snapshot_result : std::uint8_t {
    success = 0,
    invalid_input,
    invalid_runtime,
    overflow,
    failed,
};

struct runtime_ic_snapshot_stats final {
    std::uint32_t objects = 0;
    std::uint32_t scalars = 0;
    std::uint32_t skipped_structural = 0;
    std::uint32_t skipped_unaddressable = 0;
};

// Command-local SNAP workspace. Path components are string_views into the
// current compiled G mapping; the snapshot must not outlive that mapping.
class runtime_ic_snapshot final {
public:
    runtime_ic_snapshot() = default;

    runtime_ic_snapshot(
        const runtime_ic_snapshot&) = delete;
    runtime_ic_snapshot& operator=(
        const runtime_ic_snapshot&) = delete;

    runtime_ic_snapshot(
        runtime_ic_snapshot&&) = delete;
    runtime_ic_snapshot& operator=(
        runtime_ic_snapshot&&) = delete;

    [[nodiscard]] std::span<
        const runtime_ic_record_source>
    records() const noexcept {
        return values;
    }

    [[nodiscard]] const runtime_ic_snapshot_stats&
    stats() const noexcept {
        return statistics;
    }

    void reset() noexcept;

private:
    std::vector<std::string_view> components;
    std::vector<runtime_ic_record_source> values;
    runtime_ic_snapshot_stats statistics;

    friend runtime_ic_snapshot_result
    snapshot_runtime_ic_project(
        const compiled_project_view&,
        const runtime_binding_index&,
        std::span<const std::byte>,
        runtime_ic_snapshot&) noexcept;
};

[[nodiscard]] runtime_ic_snapshot_result
snapshot_runtime_ic_project(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    runtime_ic_snapshot& output) noexcept;

}
