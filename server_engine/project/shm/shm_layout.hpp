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
#include <span>
#include <vector>

namespace cw::server {

class compiled_project_view;
class runtime_project_view;
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

inline constexpr std::size_t
    shm_layout_intrinsic_slot_count =
        static_cast<std::size_t>(
            intrinsic_type::nullptr_type) + 1;

struct shm_abi_value_record final {
    std::uint64_t size = 0;
    std::uint32_t alignment = 0;
    std::uint32_t flags = 0;
};

static_assert(sizeof(shm_abi_value_record) == 16);

inline constexpr std::uint32_t
    shm_abi_value_ready_flag = 0x01u;

inline constexpr std::uint32_t
    shm_abi_value_empty_record_flag = 0x02u;


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
        return persisted_view
            ? persisted_unconnected_types.size()
            : unconnected_types.size();
    }

    [[nodiscard]] type_ref unconnected_type(
        std::size_t index) const noexcept {

        if (persisted_view) {
            return index <
                    persisted_unconnected_types.size()
                ? persisted_unconnected_types[index]
                : type_ref{};
        }

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
        // Construction-only MSVC nonvirtual extent, not sizeof(record).
        // The persisted SHM ABI columns still contain size/alignment/flags.
        std::uint32_t nonvirtual_size = 0;
        std::uint16_t alignment = 0;
        slot_state state = slot_state::empty;
        bool empty_record = false;
    };

    static_assert(sizeof(value_slot) == 16);

    void reset() noexcept;

    std::vector<value_slot> type_slots;
    std::vector<value_slot> derived_slots;
    std::vector<shm_record_offset> member_offsets;
    std::vector<shm_record_offset> base_offsets;
    std::vector<shm_offset> object_offsets;

    std::array<shm_offset, shm_layout_intrinsic_slot_count>
        unconnected_intrinsic_offsets{};

    // Named type_ref payload is identity_ref.slot(), not type_handle.slot().
    std::vector<shm_offset> unconnected_type_offsets;
    std::vector<shm_offset> unconnected_derived_offsets;
    std::vector<type_ref> unconnected_types;

    // LOAD binds these directly to compiled.bin mmap columns. The owning
    // vectors above are used only while PUBLISH/REBUILD derive a new layout.
    std::span<const shm_abi_value_record> persisted_type_slots;
    std::span<const shm_abi_value_record> persisted_derived_slots;
    std::span<const shm_record_offset> persisted_member_offsets;
    std::span<const shm_record_offset> persisted_base_offsets;
    std::span<const shm_offset> persisted_object_offsets;
    std::span<const shm_offset> persisted_unconnected_intrinsic_offsets;
    std::span<const shm_offset> persisted_unconnected_type_offsets;
    std::span<const shm_offset> persisted_unconnected_derived_offsets;
    std::span<const type_ref> persisted_unconnected_types;

    abi_target target_value = abi_target::windows_x64;
    shm_offset size_value = 0;
    std::uint32_t alignment_value = 1;
    bool prepared_value = false;
    bool persisted_view = false;

    friend class shm_layout_builder;
    friend shm_layout_result prepare_shm_layout(
        const compiled_project_view&,
        const server_abi_configuration&,
        shm_layout&) noexcept;
    friend shm_layout_result encode_shm_layout_columns(
        const shm_layout&,
        const server_abi_configuration&,
        std::span<std::byte>) noexcept;

    friend void attach_shm_layout_columns(
        const runtime_project_view&,
        shm_layout&) noexcept;

    // Transitional dead-code friendship for the superseded 08A serializer.
    // Production LOAD/PUBLISH use the physical-column API above.
    friend shm_layout_result encode_shm_layout_image(
        const shm_layout&,
        const server_abi_configuration&,
        std::span<std::byte>) noexcept;

    friend shm_layout_result load_shm_layout_image(
        const compiled_project_view&,
        const server_abi_configuration&,
        std::span<const std::byte>,
        shm_layout&) noexcept;
};

[[nodiscard]] shm_layout_result encode_shm_layout_columns(
    const shm_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> compiled_image) noexcept;

// Trusted LOAD view attachment: pointer/span setup only.
void attach_shm_layout_columns(
    const runtime_project_view& project,
    shm_layout& output) noexcept;

[[nodiscard]] shm_layout_result prepare_shm_layout(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    shm_layout& output) noexcept;

}
