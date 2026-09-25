/*
 * FIXED_DIRECT ABI image materializer.
 *
 * The materializer writes the final native x64 object image directly into
 * Project SHM. It allocates no second Runtime buffer. Native reference slots
 * contain absolute addresses valid because Server and Tasks map the same SHM
 * at the same virtual address.
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

// Verifies the concrete process/compiler representation required by the
// FIXED_DIRECT ABI image contract, including native data-member references.
[[nodiscard]] bool fixed_direct_host_compatible(
    const server_abi_configuration& abi) noexcept;

// Writes canonical unconnected<T> objects and all Project objects directly into
// caller-owned final SHM bytes. runtime must cover layout.size() bytes.
[[nodiscard]] fixed_direct_materialization_result
materialize_fixed_direct(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> runtime) noexcept;

}
