/*
 * Sparse BUILD compiled.bin construction adapter.
 *
 * This layer translates BUILD-local mmap overlays and dense Graph projection
 * into the same final compiled.bin layout contract used by full construction.
 * LOAD/runtime persistence code does not depend on BUILD acceleration types.
 */
#pragma once

#include "compiled_project.hpp"
#include "source_save.hpp"
#include "../graph/graph_delta.hpp"

namespace cw::server {

[[nodiscard]] compiled_project_image_result
prepare_build_compiled_project_layout(
    const string_table& strings,
    const identity_space& identities,
    const graph_delta& G,
    const graph_dense_projection& projection,
    const assign_overlay_view& assigns,
    const file_context& files,
    const source_map_overlay_view& sources,
    compiled_project_layout& output) noexcept;

// Encodes the sparse BUILD candidate directly into caller-owned bytes. The
// output may be a writable mmap for the new compiled.bin; no dense mutable
// graph/source_map materialization or intermediate image copy is required.
[[nodiscard]] compiled_project_image_result
encode_build_compiled_project_image(
    const string_table& strings,
    const identity_space& identities,
    const graph_delta& G,
    const graph_dense_projection& projection,
    const assign_overlay_view& assigns,
    const file_context& files,
    const source_map_overlay_view& sources,
    const compiled_project_layout& layout,
    std::span<std::byte> output) noexcept;

}
