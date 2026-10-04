/*
 * FIXED_DIRECT ABI image materializer.
 *
 * The materializer writes the final target-native object image directly into
 * Project SHM. It allocates no second Runtime buffer. Native reference slots
 * contain target-width absolute addresses valid because Tasks map the same SHM
 * at the same configured virtual address.
 */
#pragma once

#include "runtime_layout.hpp"

#include "../../configuration/server_configuration.hpp"
#include "../persistence/compiled_project.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace cw::server {

enum class fixed_direct_materialization_result : std::uint8_t {
    success = 0,
    invalid_input,
    unsupported_type,
    incompatible_abi,
    overflow,
    failed,
};

// Cheap stage timing collected by ordinary Runtime publication.
// No per-record/member/reference counters belong here.
struct fixed_direct_materialization_telemetry final {
    std::uint64_t workspace_ns = 0;
    std::uint64_t runtime_plan_ns = 0;
    std::uint64_t zero_ns = 0;
    std::uint64_t canonical_ns = 0;
    std::uint64_t links_mark_ns = 0;
    std::uint64_t objects_ns = 0;
    std::uint64_t links_materialize_ns = 0;
    std::uint64_t initializations_ns = 0;
};

// Explicit diagnostic profiler. Passing nullptr removes these counter writes
// from the normal FIXED_DIRECT hot path.
struct fixed_direct_materialization_profile final {
    std::uint64_t normal_record_calls = 0;
    std::uint64_t planned_record_calls = 0;
    std::uint64_t unplanned_record_calls = 0;
    std::uint64_t planned_member_visits = 0;
    std::uint64_t planned_none_visits = 0;
    std::uint64_t planned_reference_visits = 0;
    std::uint64_t planned_reference_zero_construction = 0;
    std::uint64_t planned_reference_member_binding = 0;
    std::uint64_t planned_reference_object_binding = 0;
    std::uint64_t planned_reference_other_construction = 0;

    std::uint64_t direct_member_binding_fast = 0;
    std::uint64_t member_binding_to_value = 0;
    std::uint64_t member_binding_to_reference = 0;
    std::uint64_t object_binding_to_value = 0;
    std::uint64_t object_binding_to_reference = 0;

    std::uint64_t resolver_steps_total = 0;
    std::uint64_t resolver_path_pushes = 0;
    std::uint64_t unplanned_reference_fallbacks = 0;

    std::uint64_t planned_materialize_visits = 0;
    std::uint64_t unplanned_member_visits = 0;

    std::uint64_t plan_members_built = 0;
    std::uint64_t plan_none_members = 0;
    std::uint64_t plan_reference_members = 0;
    std::uint64_t plan_materialize_members = 0;

    std::uint64_t program_ops_built = 0;
    std::uint64_t program_inlined_programs = 0;
    std::uint64_t program_shared_calls_built = 0;
    std::uint64_t program_direct_reference_ops_built = 0;
    std::uint64_t program_generic_reference_ops_built = 0;
    std::uint64_t program_physical_value_ops_built = 0;
    std::uint64_t program_residual_value_ops_built = 0;
    std::uint64_t program_constant_64_built = 0;
    std::uint64_t program_constant_128_built = 0;
    std::uint64_t program_executions = 0;
    std::uint64_t program_op_visits = 0;
    std::uint64_t program_direct_reference_visits = 0;
    std::uint64_t program_generic_reference_visits = 0;
    std::uint64_t program_physical_value_visits = 0;
    std::uint64_t program_residual_value_visits = 0;

    std::uint64_t array_calls = 0;
    std::uint64_t array_elements_visited = 0;
    std::uint64_t zero_array_calls = 0;
    std::uint64_t zero_array_elements_visited = 0;

    std::uint64_t constructor_plan_builds = 0;
    std::uint64_t constructor_defaults_cached = 0;
    std::uint64_t constructor_defaults_applied = 0;
};

[[nodiscard]] bool fixed_direct_host_compatible(
    const server_abi_configuration& abi) noexcept;

[[nodiscard]] bool fixed_direct_target_range_compatible(
    const server_abi_configuration& abi,
    std::uint64_t target_base_address,
    std::uint64_t runtime_size) noexcept;

[[nodiscard]] fixed_direct_materialization_result
materialize_fixed_direct(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::uint64_t target_base_address,
    std::span<std::byte> runtime,
    fixed_direct_materialization_telemetry* telemetry = nullptr,
    fixed_direct_materialization_profile* profile = nullptr) noexcept;

[[nodiscard]] fixed_direct_materialization_result
materialize_fixed_direct(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> runtime,
    fixed_direct_materialization_telemetry* telemetry = nullptr,
    fixed_direct_materialization_profile* profile = nullptr) noexcept;


// Materializes into storage guaranteed to be all-zero on entry.
// The caller owns storage provenance/preparation. This entry point skips the
// materializer's full-buffer zero pass; arbitrary/reused buffers must call
// materialize_fixed_direct() instead.
[[nodiscard]] fixed_direct_materialization_result
materialize_fixed_direct_zeroed(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::uint64_t target_base_address,
    std::span<std::byte> runtime,
    fixed_direct_materialization_telemetry* telemetry = nullptr,
    fixed_direct_materialization_profile* profile = nullptr) noexcept;

[[nodiscard]] fixed_direct_materialization_result
materialize_fixed_direct_zeroed(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> runtime,
    fixed_direct_materialization_telemetry* telemetry = nullptr,
    fixed_direct_materialization_profile* profile = nullptr) noexcept;

}
