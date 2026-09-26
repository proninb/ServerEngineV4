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

#include <array>
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

// Whole Project Runtime/SHM offsets remain 64-bit. Offsets inside one native
// record are compact 32-bit values; the maximum value is reserved internally.
using runtime_offset = std::uint64_t;
using record_offset = std::uint32_t;

static_assert(sizeof(runtime_offset) == 8);
static_assert(sizeof(record_offset) == 4);

struct runtime_value_layout final {
    runtime_offset size = 0;
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

    [[nodiscard]] runtime_offset size() const noexcept {
        return size_value;
    }

    [[nodiscard]] std::uint32_t alignment() const noexcept {
        return alignment_value;
    }

    [[nodiscard]] bool value(
        type_ref type,
        runtime_value_layout& output) const noexcept;

    [[nodiscard]] bool unconnected_offset(
        type_ref type,
        runtime_offset& output) const noexcept;

    [[nodiscard]] std::size_t unconnected_count() const noexcept {
        return unconnected_types.size();
    }

    [[nodiscard]] type_ref unconnected_type(
        std::size_t index) const noexcept {

        return index <
                unconnected_types.size()
            ? unconnected_types[index]
            : type_ref{};
    }

    [[nodiscard]] bool type(
        type_handle type,
        runtime_value_layout& output) const noexcept;

    // index is type_entry.members.begin + local member_index.
    [[nodiscard]] bool member_offset(
        std::size_t index,
        record_offset& output) const noexcept;

    [[nodiscard]] bool object_offset(
        object_handle object,
        runtime_offset& output) const noexcept;

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

    static constexpr std::size_t
    intrinsic_slot_count =
        static_cast<std::size_t>(
            intrinsic_type::nullptr_type) + 1;

    std::vector<layout_slot> type_slots;
    std::vector<layout_slot> derived_slots;
    std::vector<record_offset> member_offsets;
    std::vector<runtime_offset> object_offsets;

    std::array<
        runtime_offset,
        intrinsic_slot_count>
        unconnected_intrinsic_offsets{};

    std::vector<runtime_offset>
        unconnected_type_offsets;

    std::vector<runtime_offset>
        unconnected_derived_offsets;

    std::vector<type_ref>
        unconnected_types;

    abi_target target_value =
        abi_target::windows_x64;

    runtime_offset size_value = 0;
    std::uint32_t alignment_value = 1;
    bool prepared_value = false;

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
