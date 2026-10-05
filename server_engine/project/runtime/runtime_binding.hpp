/*
 * Resident Runtime ABI binding index.
 *
 * This is the small address/offset sidecar retained after Runtime V2
 * construction. It is consumed by Runtime Query and IC operations together
 * with mmap-native compiled.bin and the final FIXED_DIRECT SHM image.
 *
 * It is not construction state and has no dependency on legacy runtime_layout.
 */
#pragma once

#include "../abi/abi_layout.hpp"
#include "../persistence/compiled_project.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cw::server {

class runtime_layout;
class shm_layout;

// Whole Project Runtime/SHM offsets remain 64-bit. Offsets inside one native
// record are compact 32-bit values; the maximum value is reserved internally.
using runtime_offset = std::uint64_t;
using record_offset = std::uint32_t;

static_assert(sizeof(runtime_offset) == 8);
static_assert(sizeof(record_offset) == 4);

class runtime_binding_index final {
public:
    runtime_binding_index() = default;

    runtime_binding_index(const runtime_binding_index&) = delete;
    runtime_binding_index& operator=(const runtime_binding_index&) = delete;

    runtime_binding_index(runtime_binding_index&&) noexcept = default;
    runtime_binding_index& operator=(runtime_binding_index&&) noexcept = default;

    [[nodiscard]] bool object_offset(
        object_handle object,
        runtime_offset& output) const noexcept;

    [[nodiscard]] bool member_offset(
        std::size_t index,
        record_offset& output) const noexcept;

    [[nodiscard]] bool base_offset(
        std::size_t index,
        record_offset& output) const noexcept;

    [[nodiscard]] bool intrinsic_size(
        intrinsic_type type,
        std::uint8_t& output) const noexcept;

    [[nodiscard]] bool reference_layout(
        std::uint8_t& size,
        std::uint64_t& target_base_address) const noexcept;

private:
    static constexpr std::size_t
    intrinsic_slot_count =
        static_cast<std::size_t>(
            intrinsic_type::nullptr_type) + 1;

    std::array<
        std::uint8_t,
        intrinsic_slot_count>
        intrinsic_sizes{};

    std::uint64_t target_base_address_value = 0;
    std::uint8_t reference_size_value = 0;

    std::vector<runtime_offset> object_offsets;
    std::vector<record_offset> member_offsets;
    std::vector<record_offset> base_offsets;

    friend class runtime_layout;

    friend bool prepare_runtime_bindings(
        const compiled_project_view&,
        const shm_layout&,
        const server_abi_configuration&,
        std::uint64_t,
        runtime_binding_index&) noexcept;
};

[[nodiscard]] bool prepare_runtime_bindings(
    const compiled_project_view& project,
    const shm_layout& layout,
    const server_abi_configuration& abi,
    std::uint64_t target_base_address,
    runtime_binding_index& output) noexcept;

}
