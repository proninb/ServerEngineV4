/*
 * SHM Runtime V2 - physical layout only.
 *
 * G + ABI -> one dense physical SHM layout.
 * No dependency on runtime_layout, runtime_type_plan, runtime_program_op,
 * fixed_direct_materializer, or the existing Runtime micro-ISA.
 */
#pragma once

#include "../../configuration/server_configuration.hpp"
#include "../graph/object_handle.hpp"
#include "../graph/type_handle.hpp"
#include "../graph/type_ref.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cw::server {

class compiled_project_view;
class shm_layout_builder;

enum class shm_layout_result : std::uint8_t {
    success = 0,
    invalid_input,
    unsupported_type,
    overflow,
    failed,
};

using shm_offset = std::uint64_t;
using shm_record_offset = std::uint32_t;

struct shm_value_layout final {
    shm_offset size = 0;
    std::uint32_t alignment = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(shm_value_layout) == 16);

class shm_layout final {
public:
    shm_layout() = default;

    shm_layout(const shm_layout&) = delete;
    shm_layout& operator=(const shm_layout&) = delete;

    shm_layout(shm_layout&&) noexcept = default;
    shm_layout& operator=(shm_layout&&) noexcept = default;

    [[nodiscard]] shm_offset size() const noexcept { return size_value; }
    [[nodiscard]] std::uint32_t alignment() const noexcept { return alignment_value; }
    [[nodiscard]] abi_target target() const noexcept { return target_value; }

    [[nodiscard]] bool value(
        type_ref type,
        shm_value_layout& output) const noexcept;

    [[nodiscard]] bool type(
        type_handle type,
        shm_value_layout& output) const noexcept;

    [[nodiscard]] bool member_offset(
        std::size_t global_member,
        shm_record_offset& output) const noexcept;

    [[nodiscard]] bool base_offset(
        std::size_t global_base,
        shm_record_offset& output) const noexcept;

    [[nodiscard]] bool object_offset(
        object_handle object,
        shm_offset& output) const noexcept;

    [[nodiscard]] bool unconnected_offset(
        type_ref type,
        shm_offset& output) const noexcept;

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

private:
    enum class slot_state : std::uint8_t {
        empty = 0,
        visiting,
        ready,
    };

    struct value_slot final {
        std::uint64_t size = 0;
        std::uint32_t alignment = 0;
        slot_state state = slot_state::empty;
        bool empty_record = false;
        std::uint8_t reserved[2]{};
    };

    static_assert(sizeof(value_slot) == 16);

    static constexpr std::size_t intrinsic_slot_count =
        static_cast<std::size_t>(intrinsic_type::nullptr_type) + 1;

    void reset() noexcept;

    std::vector<value_slot> type_slots;
    std::vector<value_slot> derived_slots;
    std::vector<shm_record_offset> member_offsets;
    std::vector<shm_record_offset> base_offsets;
    std::vector<shm_offset> object_offsets;

    std::array<shm_offset, intrinsic_slot_count>
        unconnected_intrinsic_offsets{};

    // Named type_ref payload is identity_ref.slot(), not type_handle.slot().
    std::vector<shm_offset> unconnected_type_offsets;
    std::vector<shm_offset> unconnected_derived_offsets;
    std::vector<type_ref> unconnected_types;

    abi_target target_value = abi_target::windows_x64;
    shm_offset size_value = 0;
    std::uint32_t alignment_value = 1;
    bool prepared_value = false;

    friend class shm_layout_builder;
    friend shm_layout_result prepare_shm_layout(
        const compiled_project_view&,
        const server_abi_configuration&,
        shm_layout&) noexcept;
};

[[nodiscard]] shm_layout_result prepare_shm_layout(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    shm_layout& output) noexcept;

}
