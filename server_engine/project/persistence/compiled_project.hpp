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

inline constexpr std::uint32_t compiled_project_format_version = 2;

inline constexpr std::size_t
compiled_project_header_size = 256;

inline constexpr std::size_t compiled_project_directory_count = 22;

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
    links = 13,
    graph_identity_index = 14,
    assign_records = 15,
    assign_bytes = 16,
    source_contributions = 17,
    source_roots = 18,
    source_root_indices = 19,
    source_files = 20,
    source_file_indices = 21,
    source_paths = 22,
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
    compiled_project_view() noexcept = default;

    [[nodiscard]] compiled_project_image_result bind(
        std::span<const std::byte> image) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return bytes.data() != nullptr;
    }

    // Deep/cold integrity + semantic audit. bind() performs only the structural
    // hot-path checks needed to create a safe mmap-native view.
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

    [[nodiscard]] std::size_t object_count() const noexcept {
        return object_count_value;
    }

    [[nodiscard]] std::size_t link_count() const noexcept {
        return link_count_value;
    }

    [[nodiscard]] std::size_t assign_count() const noexcept {
        return assign_count_value;
    }

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

    [[nodiscard]] identity_ref identity(
        type_handle handle) const noexcept;

    [[nodiscard]] type_handle find_type(
        identity_ref identity) const noexcept;

    [[nodiscard]] bool member(
        type_handle type,
        member_index member,
        member_record& output) const noexcept;

    [[nodiscard]] bool construction(
        type_handle type,
        member_index member,
        construction_value& output) const noexcept;

    [[nodiscard]] member_index find_member(
        type_handle type,
        string_id name) const noexcept;

    [[nodiscard]] bool derived(
        type_ref type,
        derived_type_record& output) const noexcept;

    [[nodiscard]] object_handle object_at(
        std::size_t index) const noexcept;

    [[nodiscard]] bool object(
        object_handle handle,
        object_entry& output) const noexcept;

    [[nodiscard]] identity_ref identity(
        object_handle handle) const noexcept;

    [[nodiscard]] object_handle find_object(
        identity_ref identity) const noexcept;

    [[nodiscard]] bool link(
        link_handle handle,
        link_record& output) const noexcept;

    [[nodiscard]] bool assign(
        std::size_t index,
        std::string_view& source,
        std::string_view& target) const noexcept;

    [[nodiscard]] std::size_t source_file_count() const noexcept;
    [[nodiscard]] std::size_t source_contribution_count() const noexcept;
    [[nodiscard]] bool source_contribution(std::uint32_t index,
                                           source_contribution_record &output) const noexcept;
    [[nodiscard]] bool source_root(file_id root, source_map_range &output) const noexcept;
    [[nodiscard]] bool source_file(file_id file,
                                   std::string_view &path,
                                   file_kind &kind,
                                   source_map_range &output) const noexcept;
    [[nodiscard]] bool source_root_index(std::uint32_t index, std::uint32_t &output) const noexcept;
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
