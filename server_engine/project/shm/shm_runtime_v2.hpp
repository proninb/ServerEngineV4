/*
 * SHM Runtime V2 - production physical Runtime construction.
 *
 * Fixed policy:
 *   - object-driven local Type API preparation
 *   - bottom-up leaf inlining up to 64 physical operations
 *   - object-major hot materialization
 *   - static Graph links compiled to physical endpoint programs
 *
 * identity_ref / Graph endpoint metadata are consumed during prepare only.
 * Runtime execution receives only offsets, native reference words and compact
 * dereference programs. No semantic name/hash lookup is present in the
 * materialization path.
 */
#pragma once

#include "shm_type_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cw::server {

class compiled_project_view;
class shm_runtime_v2_link_builder;
class shm_runtime_v2_link_executor;

using shm_runtime_v2_result =
    shm_type_batch_result;

using shm_runtime_v2_prepare_telemetry =
    shm_type_batch_prepare_telemetry;

using shm_runtime_v2_execute_telemetry =
    shm_type_batch_execute_telemetry;

struct shm_runtime_v2_link_telemetry final {
    std::uint64_t links_prepared = 0;
    std::uint64_t endpoint_programs = 0;
    std::uint64_t dereference_steps = 0;

    std::uint64_t targets_marked = 0;
    std::uint64_t links_resolved = 0;
    std::uint64_t recursive_resolutions = 0;
    std::uint64_t dereference_reads = 0;
};

inline constexpr std::uint32_t
    shm_runtime_v2_inline_leaf_limit = 64;

class shm_runtime_v2 final {
public:
    shm_runtime_v2() = default;

    shm_runtime_v2(const shm_runtime_v2&) = delete;
    shm_runtime_v2& operator=(const shm_runtime_v2&) = delete;

    shm_runtime_v2(shm_runtime_v2&&) noexcept = default;
    shm_runtime_v2& operator=(shm_runtime_v2&&) noexcept = default;

    [[nodiscard]] bool prepared() const noexcept {
        return area.prepared();
    }

    [[nodiscard]] std::size_t type_api_count() const noexcept {
        return area.type_api_count();
    }

    [[nodiscard]] std::size_t object_count() const noexcept {
        return area.object_count();
    }

    [[nodiscard]] std::size_t link_count() const noexcept {
        return live_link_count_value;
    }

    [[nodiscard]] std::size_t link_endpoint_program_count() const noexcept {
        return live_link_count_value * 2;
    }

    [[nodiscard]] std::size_t link_dereference_count() const noexcept {
        return endpoint_dereferences.size();
    }

    [[nodiscard]] std::size_t resident_bytes() const noexcept {
        return
            area.resident_bytes() +
            links.size() * sizeof(link_plan) +
            endpoint_dereferences.size() *
                sizeof(shm_offset);
    }

private:
    enum class link_state : std::uint8_t {
        empty = 0,
        prepared,
        marked,
        resolving,
        resolved,
    };

    struct endpoint_program final {
        shm_offset root = 0;
        shm_offset tail = 0;

        std::uint32_t dereference_begin = 0;
        std::uint32_t dereference_count = 0;

        bool final_reference = false;
        std::uint8_t reserved[7]{};
    };

    static_assert(sizeof(endpoint_program) == 32);

    struct link_plan final {
        endpoint_program source{};
        endpoint_program target{};

        shm_offset target_slot = 0;
        shm_offset resolved_source = 0;

        link_state state = link_state::empty;
        bool live = false;
        std::uint8_t reserved[6]{};
    };

    static_assert(sizeof(link_plan) == 88);

    shm_type_batch area;

    // Stable Graph link WHERE -> physical plan. Dead Graph slots remain empty,
    // so ~link_handle remains a direct one-based index into this vector.
    std::vector<link_plan> links;

    // Each word is the folded static displacement immediately before one
    // native reference dereference.
    std::vector<shm_offset> endpoint_dereferences;

    std::size_t live_link_count_value = 0;

    friend class shm_runtime_v2_link_builder;
    friend class shm_runtime_v2_link_executor;

    friend shm_runtime_v2_result
    prepare_shm_runtime_v2(
        const compiled_project_view&,
        const server_abi_configuration&,
        const shm_layout&,
        shm_runtime_v2&,
        shm_runtime_v2_prepare_telemetry*) noexcept;

    friend shm_runtime_v2_result
    materialize_shm_runtime_v2_canonical(
        const shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_execute_telemetry*) noexcept;

    friend shm_runtime_v2_result
    mark_shm_runtime_v2_links(
        shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_link_telemetry*) noexcept;

    friend shm_runtime_v2_result
    materialize_shm_runtime_v2_objects(
        const shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_execute_telemetry*) noexcept;

    friend shm_runtime_v2_result
    materialize_shm_runtime_v2_links(
        shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_link_telemetry*) noexcept;
};

// Prepare resolves WHO / semantic endpoint paths once into physical Runtime
// object offsets and compact dereference programs.
[[nodiscard]] shm_runtime_v2_result
prepare_shm_runtime_v2(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& output,
    shm_runtime_v2_prepare_telemetry* telemetry = nullptr) noexcept;

// Canonical unconnected storage first.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_canonical(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry = nullptr) noexcept;

// Static-link pass 1. Target reference slots are marked before object
// construction. Object reference writes recognize/preserve these markers.
[[nodiscard]] shm_runtime_v2_result
mark_shm_runtime_v2_links(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_link_telemetry* telemetry = nullptr) noexcept;

// Hot object path is deliberately object-major.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_objects(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry = nullptr) noexcept;

// Static-link pass 2. Sources are resolved from physical endpoint programs.
// Pending-link dependencies recurse by stable Graph link WHERE and cycles fail.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_links(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_link_telemetry* telemetry = nullptr) noexcept;

}
