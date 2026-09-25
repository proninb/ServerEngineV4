/*
 * Temporary ABI-derived Runtime layout.
 *
 * The layout is derived directly from immutable compiled_project_view slots.
 * It never reconstructs/copies G and never uses names, hashes, or sorting.
 * Dense slot-indexed workspace exists only until final Runtime/SHM
 * materialization and is not resident Project state.
 */
#pragma once

#include "../abi/abi_layout.hpp"
#include "../persistence/compiled_project.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cw::server {

enum class runtime_layout_result : std::uint8_t {
    success = 0,
    invalid_input,
    unsupported_type,
    overflow,
    failed,
};

struct runtime_value_layout final {
    std::uint64_t size = 0;
    std::uint32_t alignment = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(runtime_value_layout) == 16);

// Construction-only dense layout workspace.
//
// Handles remain Graph slots; the workspace therefore uses direct array
// indexing only. It is discarded after final Runtime/SHM materialization.
class runtime_layout final {
public:
    runtime_layout() = default;

    runtime_layout(const runtime_layout&) = delete;
    runtime_layout& operator=(const runtime_layout&) = delete;

    runtime_layout(runtime_layout&&) noexcept = default;
    runtime_layout& operator=(runtime_layout&&) noexcept = default;

    [[nodiscard]] std::uint64_t size() const noexcept {
        return size_value;
    }

    [[nodiscard]] std::uint32_t alignment() const noexcept {
        return alignment_value;
    }

    [[nodiscard]] bool type(
        type_handle type,
        runtime_value_layout& output) const noexcept;

    // index is type_entry.members.begin + local member_index.
    [[nodiscard]] bool member_offset(
        std::size_t index,
        std::uint64_t& output) const noexcept;

    [[nodiscard]] bool object_offset(
        object_handle object,
        std::uint64_t& output) const noexcept;

private:
    enum class slot_state : std::uint8_t {
        empty = 0,
        visiting,
        ready,
    };

    struct layout_slot final {
        std::uint64_t size = 0;
        std::uint32_t alignment = 0;
        slot_state state = slot_state::empty;
        std::uint8_t reserved[3]{};
    };

    static_assert(sizeof(layout_slot) == 16);

    void reset() noexcept;

    std::vector<layout_slot> type_slots;
    std::vector<layout_slot> derived_slots;
    std::vector<std::uint64_t> member_offsets;
    std::vector<std::uint64_t> object_offsets;

    std::uint64_t size_value = 0;
    std::uint32_t alignment_value = 1;

    friend class runtime_layout_builder;

    friend runtime_layout_result prepare_runtime_layout(
        const compiled_project_view&,
        const server_abi_configuration&,
        runtime_layout&) noexcept;
};

// Derives only physical native layout needed by Runtime materialization.
//
// Contract:
// - compiled.bin remains ABI-independent;
// - no G reconstruction/copy;
// - no string/identity/member-name lookup;
// - no hash table and no sorting;
// - only dense direct slot indexing and bounded linear traversal.
[[nodiscard]] runtime_layout_result prepare_runtime_layout(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    runtime_layout& output) noexcept;

}
