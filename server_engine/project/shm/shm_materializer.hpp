/*
 * SHM Runtime V2 - direct construction.
 *
 * This layer writes the final fixed SHM directly from mmap-native G plus the
 * already prepared dense shm_layout. It owns no planning/program workspace.
 */
#pragma once

#include "shm_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace cw::server {

class compiled_project_view;

enum class shm_materialization_result : std::uint8_t {
    success = 0,
    invalid_input,
    unsupported_type,
    incompatible_abi,
    overflow,
};

struct shm_materialization_telemetry final {
    std::uint64_t canonical_values = 0;
    std::uint64_t canonical_references = 0;

    std::uint64_t objects = 0;
    std::uint64_t records = 0;
    std::uint64_t bases = 0;
    std::uint64_t members = 0;

    std::uint64_t arrays = 0;
    std::uint64_t array_elements = 0;

    std::uint64_t scalar_writes = 0;
    std::uint64_t zero_noops = 0;

    std::uint64_t references = 0;
    std::uint64_t reference_chain_steps = 0;
    std::uint64_t reference_unconnected = 0;
    std::uint64_t reference_member_bindings = 0;
    std::uint64_t reference_object_bindings = 0;

    std::uint32_t failure_stage = 0;
    std::uint32_t failure_object_slot = 0;
    std::uint32_t failure_type_slot = 0;
    std::uint32_t failure_member_local = 0;
    std::uint64_t failure_member_global = 0;
    std::uint32_t failure_member_type = 0;
    std::uint32_t failure_construction_kind = 0;
    std::uint32_t failure_construction_operand = 0;
};

// Precondition: shm[0..layout.size()) is already zeroed.
// Writes only canonical unconnected<T> contents.
[[nodiscard]] shm_materialization_result
materialize_shm_canonical(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_materialization_telemetry* telemetry = nullptr) noexcept;

// Precondition: shm is the same zeroed/fixed mapping described by layout.
// Writes Project objects through direct Object -> Type -> Members traversal.
// Constructor-default table, static links and object initializations are
// intentionally separate later phases.
[[nodiscard]] shm_materialization_result
materialize_shm_objects(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_materialization_telemetry* telemetry = nullptr) noexcept;

}
