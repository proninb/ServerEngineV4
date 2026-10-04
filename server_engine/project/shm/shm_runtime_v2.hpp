/*
 * SHM Runtime V2 - production candidate boundary.
 *
 * Fixed policy:
 *   - object-driven local Type API preparation
 *   - bottom-up leaf inlining up to 64 physical operations
 *   - object-major hot materialization
 *
 * identity_ref is consumed during prepare only. The hot materializer receives
 * no G/project, semantic identity, hash/map lookup, or generic VM program.
 *
 * This is wired into create_resident_project() through the resident V2
 * publication bridge. Constructor defaults are part of the Type API physical
 * model; static links and per-object initializations remain later cold phases.
 */
#pragma once

#include "shm_type_batch.hpp"

#include <cstddef>
#include <span>

namespace cw::server {

class compiled_project_view;

using shm_runtime_v2_result =
    shm_type_batch_result;

using shm_runtime_v2_prepare_telemetry =
    shm_type_batch_prepare_telemetry;

using shm_runtime_v2_execute_telemetry =
    shm_type_batch_execute_telemetry;

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

    [[nodiscard]] std::size_t resident_bytes() const noexcept {
        return area.resident_bytes();
    }

private:
    shm_type_batch area;

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
    materialize_shm_runtime_v2_objects(
        const shm_runtime_v2&,
        const server_abi_configuration&,
        const shm_layout&,
        std::span<std::byte>,
        shm_runtime_v2_execute_telemetry*) noexcept;
};

// Production-candidate prepare policy. No runtime/user configuration knob:
// the threshold is an implementation decision validated by benchmark.
[[nodiscard]] shm_runtime_v2_result
prepare_shm_runtime_v2(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& output,
    shm_runtime_v2_prepare_telemetry* telemetry = nullptr) noexcept;

// Hot path. Canonical storage keeps the accepted Type Batch canonical executor.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_canonical(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry = nullptr) noexcept;

// Hot object path is deliberately object-major. The batch executor remains a
// benchmark/reference path and is not part of the production-candidate API.
[[nodiscard]] shm_runtime_v2_result
materialize_shm_runtime_v2_objects(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry = nullptr) noexcept;

}
