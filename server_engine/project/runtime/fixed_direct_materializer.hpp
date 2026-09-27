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
    std::span<std::byte> runtime) noexcept;

[[nodiscard]] fixed_direct_materialization_result
materialize_fixed_direct(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> runtime) noexcept;

}
