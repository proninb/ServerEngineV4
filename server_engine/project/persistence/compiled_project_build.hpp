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

struct compiled_project_slot_append final {
    std::uint32_t first_slot = 0;
    std::uint32_t count = 0;
};

struct compiled_project_index_append final {
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
};

// Exact physical Graph delta presented to sparse compiled.bin persistence.
// Fixed top-level entities are patches at stable WHERE; variable arenas and
// genuinely new physical slots are append ranges in their existing numbering.
class compiled_project_graph_write_plan final {
public:
    [[nodiscard]] bool empty() const noexcept {
        return type_patch_count == 0 &&
            object_patch_count == 0 &&
            link_patch_count == 0 &&
            initialization_change_count == 0 &&
            appended_types.count == 0 &&
            appended_members.count == 0 &&
            appended_bases.count == 0 &&
            appended_objects.count == 0 &&
            appended_object_construction.count == 0 &&
            appended_links.count == 0 &&
            appended_derived_types.count == 0 &&
            appended_endpoint_paths.count == 0 &&
            appended_endpoint_path_steps.count == 0;
    }

    std::uint32_t type_patch_count = 0;
    std::uint32_t object_patch_count = 0;
    std::uint32_t link_patch_count = 0;
    std::uint32_t initialization_change_count = 0;

    compiled_project_slot_append appended_types;
    compiled_project_index_append appended_members;
    compiled_project_index_append appended_bases;
    compiled_project_slot_append appended_objects;
    compiled_project_slot_append appended_object_construction;
    compiled_project_slot_append appended_links;
    compiled_project_slot_append appended_derived_types;
    compiled_project_slot_append appended_endpoint_paths;
    compiled_project_index_append appended_endpoint_path_steps;

    std::size_t graph_payload_bytes = 0;
};

[[nodiscard]] compiled_project_image_result
prepare_compiled_project_graph_write_plan(
    const graph_delta& G,
    compiled_project_graph_write_plan& output) noexcept;

// Applies only existing top-level type/object/link patches to one writable
// compiled.bin image. It never scans or rewrites unchanged Graph payload.
// Append arenas are intentionally rejected by this stage.
[[nodiscard]] compiled_project_image_result
apply_compiled_project_graph_fixed_writes(
    const graph_delta& G,
    std::span<std::byte> image) noexcept;

// Writes one validated existing-object semantic result directly to mapped
// final compiled.bin bytes. New object WHERE allocation is outside this slice.
[[nodiscard]] compiled_project_image_result
apply_compiled_project_graph_object_write(
    const graph_delta& G,
    const graph_delta_object_change& change,
    std::span<std::byte> image) noexcept;

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
