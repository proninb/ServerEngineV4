/*
 * mmap-native compiled Project artifact.
 *
 * compiled.bin is final LOAD state, not BUILD acceleration. Numeric
 * string_id/identity_ref/Graph slots are preserved exactly. LOAD binds this
 * immutable view directly over mapped bytes; it never reconstructs mutable
 * string_table, identity_space, or graph containers.
 */
#pragma once

#include "project_artifact.hpp"
#include "../assign/assign_table.hpp"
#include "../source/source_map.hpp"
#include "../graph/graph.hpp"
#include "../semantic/identity.hpp"
#include "../string/string_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cw::server {

class graph_delta;
class graph_dense_projection;
class source_map_overlay_view;

inline constexpr std::uint32_t compiled_project_format_version = 24;

inline constexpr std::size_t
compiled_project_header_size = 256;

inline constexpr std::size_t compiled_project_directory_count = 65;

inline constexpr std::size_t
compiled_project_directory_entry_size = 32;

inline constexpr std::size_t
compiled_project_prefix_size =
    (compiled_project_header_size +
     compiled_project_directory_count *
         compiled_project_directory_entry_size +
     63u) &
    ~std::size_t{63u};

enum class compiled_project_section : std::uint32_t {
    string_core = 1,
    string_index = 2,
    string_bytes = 3,
    identity_core = 4,
    identity_index = 5,
    types = 6,
    type_identities = 7,
    members = 8,
    member_construction = 9,
    derived_types = 10,
    objects = 11,
    object_identities = 12,
    object_construction = 13,
    links = 14,
    graph_identity_index = 15,
    assign_records = 16,
    assign_bytes = 17,
    assign_files = 18,
    source_contributions = 19,
    source_roots = 20,
    source_files = 21,
    source_file_indices = 22,
    source_paths = 23,
    derived_index = 24,
    link_target_index = 25,
    endpoint_paths = 26,
    endpoint_path_steps = 27,
    endpoint_path_index = 28,
    bases = 29,
    member_name_index = 30,
    object_initializations = 31,
    object_initialization_target_index = 32,
    constructor_defaults = 33,

    // Same-WHERE target-ABI physical columns. These are accelerators over the
    // semantic Graph sections, not a second Runtime identity space.
    runtime_abi_header = 34,
    type_abi = 35,
    derived_abi = 36,
    member_abi = 37,
    base_abi = 38,
    object_abi = 39,
    unconnected_intrinsic_abi = 40,
    unconnected_type_abi = 41,
    unconnected_derived_abi = 42,
    unconnected_types = 43,

    // Same-WHERE Runtime execution columns. Fixed records remain 1:1 with
    // Graph WHERE; only dereference endpoint programs use the variable tail.
    runtime_execution_header = 44,
    link_runtime = 45,
    initialization_runtime = 46,
    runtime_endpoint_programs = 47,
    runtime_endpoint_dereferences = 48,

    // Persisted INLINE-64 Type execution image. These are executable physical
    // columns only; semantic identity/build caches are never persisted.
    runtime_type_apis = 49,
    runtime_type_relative_references = 50,
    runtime_type_absolute_references = 51,
    runtime_type_object_references = 52,
    runtime_type_stores = 53,
    runtime_type_post_stores = 54,
    runtime_type_children = 55,
    runtime_type_repeats = 56,
    runtime_type_constants = 57,
    runtime_type_object_where = 58,
    runtime_type_objects = 59,
    runtime_type_canonical_roots = 60,
    runtime_type_object_groups = 61,
    runtime_type_object_group_offsets = 62,
    runtime_type_canonical_groups = 63,
    runtime_type_canonical_group_offsets = 64,
    runtime_type_object_patches = 65,
};

struct compiled_project_runtime_type_counts final {
    std::uint64_t type_apis = 0;
    std::uint64_t relative_references = 0;
    std::uint64_t absolute_references = 0;
    std::uint64_t object_references = 0;
    std::uint64_t stores = 0;
    std::uint64_t post_stores = 0;
    std::uint64_t children = 0;
    std::uint64_t repeats = 0;
    std::uint64_t constants = 0;
    std::uint64_t object_where = 0;
    std::uint64_t objects = 0;
    std::uint64_t canonical_roots = 0;
    std::uint64_t object_groups = 0;
    std::uint64_t object_group_offsets = 0;
    std::uint64_t canonical_groups = 0;
    std::uint64_t canonical_group_offsets = 0;
    std::uint64_t object_patches = 0;
};

enum class compiled_project_image_result : std::uint8_t {
    success,
    invalid_state,
    invalid_image,
    failed,
};

// Fixed-size preparation state for direct compiled.bin encoding. It contains
// only section counts/offsets; no Project payload bytes are copied into it.
class compiled_project_layout final {
public:
    [[nodiscard]] std::size_t size() const noexcept {
        return size_value;
    }

private:
    struct preparation_counts final {
        std::uint64_t string_count = 0;
        std::uint64_t string_bytes_count = 0;
        std::uint64_t identity_count = 0;
        std::uint64_t type_count = 0;
        std::uint64_t member_count = 0;
        std::uint64_t base_count = 0;
        std::uint64_t derived_count = 0;
        std::uint64_t object_count = 0;
        std::uint64_t object_construction_count = 0;
        std::uint64_t link_count = 0;
        std::uint64_t initialization_count = 0;
        std::uint64_t endpoint_path_count = 0;
        std::uint64_t endpoint_path_step_count = 0;
        std::uint64_t assign_count = 0;
        std::uint64_t assign_bytes_count = 0;
        std::uint64_t source_contribution_count = 0;
        std::uint64_t source_file_count = 0;
        std::uint64_t source_path_bytes = 0;
        std::uint64_t constructor_default_count = 0;
        std::uint64_t runtime_endpoint_program_count = 0;
        std::uint64_t runtime_endpoint_dereference_count = 0;
    };

    [[nodiscard]] static compiled_project_image_result
    prepare_counts(
        const preparation_counts& counts,
        compiled_project_layout& output) noexcept;

    struct section_record final {
        compiled_project_section kind{};
        std::uint32_t record_size = 0;
        std::uint64_t count = 0;
        std::uint64_t offset = 0;
    };

    std::array<
        section_record,
        compiled_project_directory_count>
        sections{};

    std::size_t size_value = 0;

    friend compiled_project_image_result
    prepare_compiled_project_layout(const string_table &,
                                    const identity_space &,
                                    const graph &,
                                    const assign_table &,
                                    const file_context &,
                                    const source_map &,
                                    compiled_project_layout &) noexcept;

    friend compiled_project_image_result
    prepare_build_compiled_project_layout(
        const string_table&,
        const identity_space&,
        const graph_delta&,
        const graph_dense_projection&,
        const assign_overlay_view&,
        const file_context&,
        const source_map_overlay_view&,
        compiled_project_layout&) noexcept;

    friend compiled_project_image_result
    encode_build_compiled_project_image(
        const string_table&,
        const identity_space&,
        const graph_delta&,
        const graph_dense_projection&,
        const assign_overlay_view&,
        const file_context&,
        const source_map_overlay_view&,
        const compiled_project_layout&,
        std::span<std::byte>) noexcept;

    friend compiled_project_image_result
    encode_compiled_project_image(const string_table &,
                                  const identity_space &,
                                  const graph &,
                                  const assign_table &,
                                  const file_context &,
                                  const source_map &,
                                  const compiled_project_layout &,
                                  std::span<std::byte>) noexcept;
};

// Read-only query view over one mapped/immutable compiled.bin image.
class compiled_project_view final {
public:
    std::size_t constructor_default_count() const noexcept {
        return static_cast<std::size_t>(section(compiled_project_section::constructor_defaults).count);
    }
    bool constructor_default_at(std::size_t index, constructor_default& output) const noexcept;
    compiled_project_view() noexcept = default;

    [[nodiscard]] compiled_project_image_result bind(
        std::span<const std::byte> image) noexcept;

    // Trusted current-format attachment used by LOAD.
    // No format validation, CRC, semantic audit, deserialization or copy.
    void attach(
        std::span<const std::byte> image) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return bytes.data() != nullptr;
    }

    // Deep/cold integrity + semantic audit. bind() is a cold verifier used by
    // PUBLISH/tests. LOAD uses attach() and trusts the current internal image.
    [[nodiscard]] compiled_project_image_result
    verify_contents() const noexcept;

    [[nodiscard]] std::size_t string_count() const noexcept {
        return string_count_value;
    }

    [[nodiscard]] std::size_t string_byte_size() const noexcept;

    [[nodiscard]] std::size_t identity_count() const noexcept {
        return identity_count_value;
    }

    [[nodiscard]] std::size_t type_count() const noexcept {
        return type_count_value;
    }

    [[nodiscard]] std::size_t type_slot_count() const noexcept {
        return type_count_value;
    }

    [[nodiscard]] std::size_t live_type_count() const noexcept {
        return live_type_count_value;
    }

    [[nodiscard]] std::size_t member_count() const noexcept {
        return static_cast<std::size_t>(
            section(
                compiled_project_section::
                    members).count);
    }

    [[nodiscard]] std::size_t base_count() const noexcept {
        return static_cast<std::size_t>(
            section(
                compiled_project_section::
                    bases).count);
    }

    [[nodiscard]] std::size_t derived_type_count() const noexcept {
        return static_cast<std::size_t>(
            section(
                compiled_project_section::
                    derived_types).count);
    }

    [[nodiscard]] std::size_t object_construction_count() const noexcept {
        return static_cast<std::size_t>(
            section(
                compiled_project_section::
                    object_construction).count);
    }

    [[nodiscard]] std::size_t object_count() const noexcept {
        return object_count_value;
    }

    [[nodiscard]] std::size_t object_slot_count() const noexcept {
        return object_count_value;
    }

    [[nodiscard]] std::size_t live_object_count() const noexcept {
        return live_object_count_value;
    }

    [[nodiscard]] std::size_t link_count() const noexcept {
        return link_count_value;
    }

    [[nodiscard]] std::size_t link_slot_count() const noexcept {
        return link_count_value;
    }

    [[nodiscard]] std::size_t live_link_count() const noexcept {
        return live_link_count_value;
    }

    [[nodiscard]] std::size_t initialization_count() const noexcept {
        return static_cast<std::size_t>(
            section(
                compiled_project_section::
                    object_initializations).count);
    }

    [[nodiscard]] std::size_t assign_count() const noexcept {
        return assign_count_value;
    }

    // Exposes only target-ABI physical columns. The section WHERE is the
    // same Graph WHERE as its semantic counterpart.
    [[nodiscard]] std::span<const std::byte>
    runtime_physical_section(
        compiled_project_section kind) const noexcept;

    [[nodiscard]] std::string_view string(
        string_id id) const noexcept;

    [[nodiscard]] string_id find_string(
        std::string_view value) const noexcept;

    [[nodiscard]] identity_ref identity_root() const noexcept;

    [[nodiscard]] identity_ref identity_at_slot(
        std::uint32_t slot) const noexcept;

    [[nodiscard]] bool identity_valid(
        identity_ref identity) const noexcept;

    [[nodiscard]] identity_ref identity_parent(
        identity_ref identity) const noexcept;

    [[nodiscard]] string_id identity_name(
        identity_ref identity) const noexcept;

    [[nodiscard]] identity_ref find_identity(
        identity_ref parent,
        string_id name,
        identity_kind kind) const noexcept;

    [[nodiscard]] type_handle type_at(
        std::size_t index) const noexcept;

    [[nodiscard]] bool type(
        type_handle handle,
        type_entry& output) const noexcept;

    [[nodiscard]] bool type_slot_live(
        type_handle handle) const noexcept;

    [[nodiscard]] bool type_raw(
        type_handle handle,
        type_entry& output) const noexcept;

    [[nodiscard]] identity_ref identity(
        type_handle handle) const noexcept;

    [[nodiscard]] identity_ref type_identity_raw(
        type_handle handle) const noexcept {

        return identity(handle);
    }

    [[nodiscard]] bool base_at(
        std::size_t index,
        base_record& output) const noexcept;

    // Direct graph_identity_index slot read: semantic WHO -> physical WHERE.
    // This is not a hash/name search and allocates nothing.
    [[nodiscard]] type_handle type_location(
        identity_ref identity) const noexcept;

    [[nodiscard]] type_handle type_location(
        type_ref type) const noexcept;

    [[nodiscard]] type_handle find_type(
        identity_ref identity) const noexcept;

    [[nodiscard]] type_handle find_type_lineage(
        identity_ref identity) const noexcept;

    [[nodiscard]] bool member(
        type_handle type,
        member_index member,
        member_record& output) const noexcept;

    [[nodiscard]] bool member(
        type_handle type,
        std::uint32_t local_member,
        member_record& output) const noexcept;

    // Direct global member-slot access for dense sequential consumers such as
    // Runtime ABI layout. This performs no name/hash lookup.
    [[nodiscard]] bool member_at(
        std::size_t index,
        member_record& output) const noexcept;

    // Direct global member-construction access for dense Runtime consumers.
    // This performs no type/member-range decode.
    [[nodiscard]] bool construction_at(
        std::size_t index,
        construction_value& output) const noexcept;

    [[nodiscard]] bool construction(
        type_handle type,
        member_index member,
        construction_value& output) const noexcept;

    [[nodiscard]] bool construction(
        type_handle type,
        std::uint32_t local_member,
        construction_value& output) const noexcept;

    // Persisted O(1) lookup from (type, name) to the type-local
    // member index. The hash table is a read accelerator over members.
    [[nodiscard]] member_index find_member(
        type_handle type,
        string_id name) const noexcept;

    [[nodiscard]] type_ref named(type_handle type) const noexcept {
        const auto value = identity(type);
        return value ? type_ref::make(type_ref_kind::named, value.slot()) : type_ref{};
    }

    [[nodiscard]] bool named(
        type_ref type,
        type_handle& output) const noexcept;

    [[nodiscard]] bool derived(
        type_ref type,
        derived_type_record& output) const noexcept;

    // Persisted O(1) canonical lookup used by sparse BUILD. The index is a
    // read accelerator over derived_types, not another semantic representation.
    [[nodiscard]] type_ref find_derived(
        type_ref child,
        derived_type_kind kind,
        std::uint64_t payload) const noexcept;

    [[nodiscard]] object_handle object_at(
        std::size_t index) const noexcept;

    [[nodiscard]] bool object(
        object_handle handle,
        object_entry& output) const noexcept;

    [[nodiscard]] bool object_slot_live(
        object_handle handle) const noexcept;

    [[nodiscard]] bool object_raw(
        object_handle handle,
        object_entry& output) const noexcept;

    [[nodiscard]] identity_ref identity(
        object_handle handle) const noexcept;

    [[nodiscard]] identity_ref object_identity_raw(
        object_handle handle) const noexcept {

        return identity(handle);
    }

    // Direct graph_identity_index slot read for object WHO -> WHERE.
    [[nodiscard]] object_handle object_location(
        identity_ref identity) const noexcept;

    [[nodiscard]] object_handle find_object(
        identity_ref identity) const noexcept;

    [[nodiscard]] object_handle find_object_lineage(
        identity_ref identity) const noexcept;

    [[nodiscard]] bool construction(
        object_handle object,
        construction_value& output) const noexcept;

    [[nodiscard]] std::size_t endpoint_path_count() const noexcept {
        return static_cast<std::size_t>(
            section(
                compiled_project_section::
                    endpoint_paths).count);
    }

    [[nodiscard]] std::size_t endpoint_path_step_count() const noexcept {
        return static_cast<std::size_t>(
            section(
                compiled_project_section::
                    endpoint_path_steps).count);
    }

    [[nodiscard]] bool endpoint_path(
        endpoint_path_handle path,
        endpoint_path_record& output) const noexcept;

    [[nodiscard]] bool endpoint_path_step_at(
        std::size_t index,
        endpoint_path_step& output) const noexcept;

    // Persisted O(1) canonical lookup used by sparse BUILD. The index already
    // belongs to compiled.bin; this exposes it without reconstructing paths.
    [[nodiscard]] endpoint_path_handle find_endpoint_path(
        type_ref root_type,
        std::span<const endpoint_path_step> steps) const noexcept;

    [[nodiscard]] link_handle link_at(
        std::size_t index) const noexcept;

    [[nodiscard]] bool link(
        link_handle handle,
        link_record& output) const noexcept;

    [[nodiscard]] bool link_slot_live(
        link_handle handle) const noexcept;

    [[nodiscard]] bool link_raw(
        link_handle handle,
        link_record& output) const noexcept;

    [[nodiscard]] bool initialization_at(
        std::size_t index,
        object_initialization_record& output) const noexcept;

    // O(1) lookup of the canonical final init for one object subobject.
    [[nodiscard]] bool initialization(
        object_endpoint target,
        object_initialization_record& output) const noexcept;

    // Persisted O(1) target ownership lookup. Graph link semantics require one
    // binding per target endpoint, so sparse BUILD must not scan all OLD links.
    [[nodiscard]] link_handle find_link_target(
        object_endpoint target) const noexcept;

    [[nodiscard]] link_handle find_link_target_lineage(
        object_endpoint target) const noexcept;

    [[nodiscard]] link_handle find_link_target(
        object_handle object,
        std::uint32_t local_member) const noexcept;

    [[nodiscard]] bool assign(
        std::size_t index,
        std::string_view& source,
        std::string_view& target) const noexcept;

    [[nodiscard]] bool assign_file(
        std::size_t index,
        file_id& output) const noexcept;

    [[nodiscard]] std::size_t source_file_count() const noexcept;
    [[nodiscard]] std::size_t source_contribution_count() const noexcept;
    [[nodiscard]] bool source_contribution(std::uint32_t index,
                                           source_contribution_record &output) const noexcept;
    [[nodiscard]] bool source_root(file_id root, source_map_range &output) const noexcept;
    [[nodiscard]] bool source_file(file_id file,
                                   std::string_view &path,
                                   file_kind &kind,
                                   source_map_range &output) const noexcept;
    [[nodiscard]] bool source_file_index(std::uint32_t index, std::uint32_t &output) const noexcept;
    [[nodiscard]] compiled_project_image_result verify_sources() const noexcept;

  private:
    struct section_view final {
        const std::byte* data = nullptr;
        std::uint64_t count = 0;
        std::uint32_t record_size = 0;
        std::uint64_t crc64 = 0;
    };

    [[nodiscard]] const section_view& section(
        compiled_project_section kind) const noexcept;

    [[nodiscard]] std::span<const std::byte> section_bytes(
        compiled_project_section kind) const noexcept;

    [[nodiscard]] std::size_t initialization_position(
        object_endpoint target) const noexcept;

    [[nodiscard]] string_id string_from_raw(
        std::uint32_t value) const noexcept;

    [[nodiscard]] identity_ref identity_from_raw(
        std::uint32_t value) const noexcept;

    [[nodiscard]] type_handle type_from_raw(
        std::uint32_t value) const noexcept;

    [[nodiscard]] object_handle object_from_raw(
        std::uint32_t value) const noexcept;

    [[nodiscard]] link_handle link_from_raw(
        std::uint32_t value) const noexcept;

    [[nodiscard]] member_index member_from_raw(
        std::uint32_t value) const noexcept;

    [[nodiscard]] type_ref type_ref_from_raw(
        std::uint32_t value) const noexcept;

    std::span<const std::byte> bytes;
    section_view sections[
        compiled_project_directory_count]{};

    std::size_t string_count_value = 0;
    std::size_t identity_count_value = 0;
    std::size_t type_count_value = 0;
    std::size_t object_count_value = 0;
    std::size_t link_count_value = 0;
    std::size_t live_type_count_value = 0;
    std::size_t live_object_count_value = 0;
    std::size_t live_link_count_value = 0;
    std::size_t assign_count_value = 0;
};

// Precomputes the exact final compiled.bin size and fixed section offsets
// without allocating or copying Project payload bytes.
[[nodiscard]] compiled_project_image_result
prepare_compiled_project_layout(const string_table &strings,
                                const identity_space &identities,
                                const graph &G,
                                const assign_table &assigns,
                                const file_context &files,
                                const source_map &sources,
                                compiled_project_layout &output) noexcept;

// Returns one writable target-ABI physical column after a structurally
// valid bind. Semantic sections are deliberately rejected.
[[nodiscard]] std::span<std::byte>
compiled_project_runtime_physical_section(
    std::span<std::byte> image,
    compiled_project_section kind) noexcept;

// PUBLISH-only append-tail preparation. The initial v24 image contains zero
// counts for Type execution sections; after INLINE-64 compilation the same
// final compiled.bin is extended and these directory entries are finalized.
[[nodiscard]] compiled_project_image_result
prepare_compiled_project_runtime_type_tail(
    std::span<const std::byte> image,
    const compiled_project_runtime_type_counts& counts,
    std::size_t& final_size) noexcept;

[[nodiscard]] compiled_project_image_result
apply_compiled_project_runtime_type_tail(
    const compiled_project_runtime_type_counts& counts,
    std::span<std::byte> image) noexcept;

// Encodes directly into caller-owned bytes, including writable mmap pages.
// layout must come from prepare_compiled_project_layout() for the same unchanged
// construction state. Encoding never performs a second layout pass.
[[nodiscard]] compiled_project_image_result
encode_compiled_project_image(const string_table &strings,
                              const identity_space &identities,
                              const graph &G,
                              const assign_table &assigns,
                              const file_context &files,
                              const source_map &sources,
                              const compiled_project_layout &layout,
                              std::span<std::byte> output) noexcept;
}
