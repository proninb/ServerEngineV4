#include "runtime_project.hpp"

#include "../shm/shm_layout.hpp"
#include "../shm/shm_runtime_v2.hpp"
#include "../shm/shm_type_batch.hpp"
#include "../../writable_file_mapping.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> image_magic{
    std::byte{'S'}, std::byte{'E'}, std::byte{'R'}, std::byte{'T'},
    std::byte{'V'}, std::byte{'4'}, std::byte{'R'}, std::byte{0},
};

constexpr std::size_t directory_offset = runtime_project_header_size;

[[nodiscard]] constexpr std::size_t section_index(
    compiled_project_section kind) noexcept {

    const auto raw = static_cast<std::uint32_t>(kind);
    const auto first = static_cast<std::uint32_t>(
        compiled_project_section::runtime_abi_header);
    const auto last = static_cast<std::uint32_t>(
        compiled_project_section::runtime_type_object_patches);

    return raw >= first && raw <= last
        ? static_cast<std::size_t>(raw - first)
        : runtime_project_directory_count;
}

[[nodiscard]] constexpr bool align64(
    std::uint64_t value,
    std::uint64_t& output) noexcept {

    if (value > (std::numeric_limits<std::uint64_t>::max)() - 63u)
        return false;

    output = (value + 63u) & ~std::uint64_t{63u};
    return true;
}

[[nodiscard]] constexpr bool add_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    if (left > (std::numeric_limits<std::uint64_t>::max)() - right)
        return false;

    output = left + right;
    return true;
}

[[nodiscard]] constexpr bool multiply_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    if (left != 0 &&
        right > (std::numeric_limits<std::uint64_t>::max)() / left)
        return false;

    output = left * right;
    return true;
}

[[nodiscard]] std::uint32_t read_u32(const std::byte* value) noexcept {
    std::uint32_t result = 0;
    std::memcpy(&result, value, sizeof(result));
    return result;
}

[[nodiscard]] std::uint64_t read_u64(const std::byte* value) noexcept {
    std::uint64_t result = 0;
    std::memcpy(&result, value, sizeof(result));
    return result;
}

void write_u32(std::byte* target, std::uint32_t value) noexcept {
    std::memcpy(target, &value, sizeof(value));
}

void write_u64(std::byte* target, std::uint64_t value) noexcept {
    std::memcpy(target, &value, sizeof(value));
}

[[nodiscard]] constexpr std::uint32_t expected_record_size(
    compiled_project_section kind) noexcept {

    switch (kind) {
    case compiled_project_section::runtime_abi_header:
        return 64;
    case compiled_project_section::type_abi:
    case compiled_project_section::derived_abi:
        return 16;
    case compiled_project_section::member_abi:
    case compiled_project_section::base_abi:
    case compiled_project_section::unconnected_types:
        return 4;
    case compiled_project_section::object_abi:
    case compiled_project_section::unconnected_intrinsic_abi:
    case compiled_project_section::unconnected_type_abi:
    case compiled_project_section::unconnected_derived_abi:
    case compiled_project_section::runtime_endpoint_dereferences:
        return 8;
    case compiled_project_section::runtime_execution_header:
        return 64;
    case compiled_project_section::link_runtime:
    case compiled_project_section::initialization_runtime:
        return 32;
    case compiled_project_section::runtime_endpoint_programs:
        return 24;
    case compiled_project_section::runtime_type_apis:
        return 56;
    case compiled_project_section::runtime_type_relative_references:
    case compiled_project_section::runtime_type_object_references:
    case compiled_project_section::runtime_type_children:
        return 8;
    case compiled_project_section::runtime_type_absolute_references:
    case compiled_project_section::runtime_type_constants:
    case compiled_project_section::runtime_type_objects:
    case compiled_project_section::runtime_type_canonical_roots:
        return 16;
    case compiled_project_section::runtime_type_stores:
    case compiled_project_section::runtime_type_post_stores:
    case compiled_project_section::runtime_type_object_groups:
    case compiled_project_section::runtime_type_canonical_groups:
    case compiled_project_section::runtime_type_object_patches:
        return 12;
    case compiled_project_section::runtime_type_repeats:
        return 24;
    case compiled_project_section::runtime_type_object_where:
    case compiled_project_section::runtime_type_object_group_offsets:
    case compiled_project_section::runtime_type_canonical_group_offsets:
        return 8;
    default:
        return 0;
    }
}

struct execution_counts final {
    std::uint64_t endpoint_programs = 0;
    std::uint64_t dereferences = 0;
};

[[nodiscard]] bool count_endpoint(
    const compiled_project_view& project,
    object_endpoint endpoint,
    execution_counts& counts) noexcept {

    if (!endpoint.object || !endpoint.member)
        return false;

    if (!endpoint.member.is_path())
        return true;

    endpoint_path_record path;
    if (!project.endpoint_path(endpoint.member.path(), path))
        return false;

    std::uint64_t dereferences = 0;

    for (std::uint32_t local = 0; local < path.steps.count; ++local) {
        endpoint_path_step step;

        if (!project.endpoint_path_step_at(
                static_cast<std::size_t>(path.steps.begin) + local,
                step))
            return false;

        if (step.kind == endpoint_path_step_kind::dereference) {
            if (dereferences ==
                (std::numeric_limits<std::uint64_t>::max)())
                return false;

            ++dereferences;
        }
    }

    if (dereferences == 0)
        return true;

    if (counts.endpoint_programs ==
            (std::numeric_limits<std::uint64_t>::max)() ||
        dereferences >
            (std::numeric_limits<std::uint64_t>::max)() -
                counts.dereferences)
        return false;

    ++counts.endpoint_programs;
    counts.dereferences += dereferences;
    return true;
}

[[nodiscard]] bool prepare_execution_counts(
    const compiled_project_view& project,
    execution_counts& counts) noexcept {

    counts = {};

    for (std::size_t index = 0; index < project.link_count(); ++index) {
        const auto handle = project.link_at(index);
        if (!handle)
            continue;

        link_record link;
        if (!project.link(handle, link) ||
            !count_endpoint(project, link.source, counts) ||
            !count_endpoint(project, link.target, counts))
            return false;
    }

    for (std::size_t index = 0;
         index < project.initialization_count();
         ++index) {

        object_initialization_record value;

        if (!project.initialization_at(index, value) ||
            !count_endpoint(project, value.target, counts))
            return false;
    }

    return true;
}

}

void runtime_project_view::reset() noexcept {
    bytes = {};
    for (auto& value : sections)
        value = {};
}

void runtime_project_view::attach(
    std::span<const std::byte> image) noexcept {

    reset();
    bytes = image;

    for (std::size_t index = 0;
         index < runtime_project_directory_count;
         ++index) {

        const auto* entry =
            image.data() +
            directory_offset +
            index * runtime_project_directory_entry_size;

        const auto offset = read_u64(entry + 8);

        sections[index] = {
            image.data() + static_cast<std::size_t>(offset),
            read_u64(entry + 16),
            read_u32(entry + 4),
        };
    }
}

runtime_project_image_result
runtime_project_view::bind(
    std::span<const std::byte> image) noexcept {

    reset();

    if (image.size() < runtime_project_prefix_size ||
        std::memcmp(
            image.data(),
            image_magic.data(),
            image_magic.size()) != 0 ||
        read_u32(image.data() + 8) != runtime_project_format_version ||
        read_u32(image.data() + 12) != runtime_project_header_size ||
        read_u32(image.data() + 16) != runtime_project_directory_count ||
        read_u32(image.data() + 20) != runtime_project_directory_entry_size ||
        read_u64(image.data() + 24) != image.size())
        return runtime_project_image_result::invalid_image;

    std::uint64_t previous_end = runtime_project_prefix_size;

    for (std::size_t index = 0;
         index < runtime_project_directory_count;
         ++index) {

        const auto* entry =
            image.data() +
            directory_offset +
            index * runtime_project_directory_entry_size;

        const auto kind = static_cast<compiled_project_section>(
            read_u32(entry));

        const auto expected_kind = static_cast<compiled_project_section>(
            static_cast<std::uint32_t>(
                compiled_project_section::runtime_abi_header) +
            static_cast<std::uint32_t>(index));

        const auto record_size = read_u32(entry + 4);
        const auto offset = read_u64(entry + 8);
        const auto count = read_u64(entry + 16);

        std::uint64_t byte_count = 0;
        std::uint64_t end = 0;

        if (kind != expected_kind ||
            record_size != expected_record_size(kind) ||
            record_size == 0 ||
            offset < previous_end ||
            (offset & 63u) != 0 ||
            !multiply_u64(count, record_size, byte_count) ||
            !add_u64(offset, byte_count, end) ||
            end > image.size())
            return runtime_project_image_result::invalid_image;

        previous_end = end;
    }

    attach(image);
    return runtime_project_image_result::success;
}

std::span<const std::byte>
runtime_project_view::section(
    compiled_project_section kind) const noexcept {

    const auto index = section_index(kind);

    if (!valid() || index >= sections.size())
        return {};

    const auto& value = sections[index];
    std::uint64_t byte_count = 0;

    if (!multiply_u64(value.count, value.record_size, byte_count) ||
        byte_count >
            (std::numeric_limits<std::size_t>::max)())
        return {};

    return {
        value.data,
        static_cast<std::size_t>(byte_count),
    };
}

runtime_project_image_result
prepare_runtime_project_layout(
    const compiled_project_view& project,
    const compiled_project_runtime_type_counts& type_counts,
    runtime_project_layout& output) noexcept {

    output = {};

    if (!project.valid())
        return runtime_project_image_result::invalid_state;

    execution_counts execution;

    if (!prepare_execution_counts(project, execution))
        return runtime_project_image_result::invalid_state;

    const auto intrinsic_count =
        static_cast<std::uint64_t>(shm_layout_intrinsic_slot_count);

    const std::array<
        runtime_project_layout::section_record,
        runtime_project_directory_count>
        layout{{
            {compiled_project_section::runtime_abi_header, 64, 1},
            {compiled_project_section::type_abi, 16, project.type_count()},
            {compiled_project_section::derived_abi, 16, project.derived_type_count()},
            {compiled_project_section::member_abi, 4, project.member_count()},
            {compiled_project_section::base_abi, 4, project.base_count()},
            {compiled_project_section::object_abi, 8, project.object_count()},
            {compiled_project_section::unconnected_intrinsic_abi, 8, intrinsic_count},
            {compiled_project_section::unconnected_type_abi, 8, project.identity_count()},
            {compiled_project_section::unconnected_derived_abi, 8, project.derived_type_count()},
            {compiled_project_section::unconnected_types, 4,
                intrinsic_count + project.identity_count() +
                    project.derived_type_count()},
            {compiled_project_section::runtime_execution_header, 64, 1},
            {compiled_project_section::link_runtime, 32, project.link_count()},
            {compiled_project_section::initialization_runtime, 32, project.initialization_count()},
            {compiled_project_section::runtime_endpoint_programs, 24, execution.endpoint_programs},
            {compiled_project_section::runtime_endpoint_dereferences, 8, execution.dereferences},
            {compiled_project_section::runtime_type_apis, 56, type_counts.type_apis},
            {compiled_project_section::runtime_type_relative_references, 8, type_counts.relative_references},
            {compiled_project_section::runtime_type_absolute_references, 16, type_counts.absolute_references},
            {compiled_project_section::runtime_type_object_references, 8, type_counts.object_references},
            {compiled_project_section::runtime_type_stores, 12, type_counts.stores},
            {compiled_project_section::runtime_type_post_stores, 12, type_counts.post_stores},
            {compiled_project_section::runtime_type_children, 8, type_counts.children},
            {compiled_project_section::runtime_type_repeats, 24, type_counts.repeats},
            {compiled_project_section::runtime_type_constants, 16, type_counts.constants},
            {compiled_project_section::runtime_type_object_where, 8, type_counts.object_where},
            {compiled_project_section::runtime_type_objects, 16, type_counts.objects},
            {compiled_project_section::runtime_type_canonical_roots, 16, type_counts.canonical_roots},
            {compiled_project_section::runtime_type_object_groups, 12, type_counts.object_groups},
            {compiled_project_section::runtime_type_object_group_offsets, 8, type_counts.object_group_offsets},
            {compiled_project_section::runtime_type_canonical_groups, 12, type_counts.canonical_groups},
            {compiled_project_section::runtime_type_canonical_group_offsets, 8, type_counts.canonical_group_offsets},
            {compiled_project_section::runtime_type_object_patches, 12, type_counts.object_patches},
        }};

    auto prepared = layout;
    std::uint64_t cursor = runtime_project_prefix_size;

    for (auto& section : prepared) {
        std::uint64_t offset = 0;
        std::uint64_t byte_count = 0;

        if (!align64(cursor, offset) ||
            !multiply_u64(
                section.count,
                section.record_size,
                byte_count) ||
            !add_u64(offset, byte_count, cursor))
            return runtime_project_image_result::failed;

        section.offset = offset;
    }

    if (cursor >
        (std::numeric_limits<std::size_t>::max)())
        return runtime_project_image_result::failed;

    output.sections = prepared;
    output.size_value = static_cast<std::size_t>(cursor);

    return runtime_project_image_result::success;
}

runtime_project_image_result
encode_runtime_project_image(
    const runtime_project_layout& layout,
    std::span<std::byte> output) noexcept {

    if (layout.size() < runtime_project_prefix_size ||
        output.size() != layout.size())
        return runtime_project_image_result::invalid_state;

    std::fill(output.begin(), output.end(), std::byte{0});

    std::memcpy(
        output.data(),
        image_magic.data(),
        image_magic.size());

    write_u32(output.data() + 8, runtime_project_format_version);
    write_u32(output.data() + 12, runtime_project_header_size);
    write_u32(output.data() + 16, runtime_project_directory_count);
    write_u32(output.data() + 20, runtime_project_directory_entry_size);
    write_u64(output.data() + 24, output.size());

    for (std::size_t index = 0;
         index < layout.sections.size();
         ++index) {

        const auto& section = layout.sections[index];

        auto* entry =
            output.data() +
            directory_offset +
            index * runtime_project_directory_entry_size;

        write_u32(
            entry,
            static_cast<std::uint32_t>(section.kind));
        write_u32(entry + 4, section.record_size);
        write_u64(entry + 8, section.offset);
        write_u64(entry + 16, section.count);
    }

    runtime_project_view validation;

    return validation.bind(
        std::span<const std::byte>{
            output.data(),
            output.size()});
}

std::span<std::byte>
runtime_project_mutable_section(
    std::span<std::byte> image,
    compiled_project_section kind) noexcept {

    const auto index = section_index(kind);

    if (index >= runtime_project_directory_count ||
        image.size() < runtime_project_prefix_size)
        return {};

    const auto* entry =
        image.data() +
        directory_offset +
        index * runtime_project_directory_entry_size;

    if (read_u32(entry) != static_cast<std::uint32_t>(kind) ||
        read_u32(entry + 4) != expected_record_size(kind))
        return {};

    const auto record_size = read_u32(entry + 4);
    const auto offset = read_u64(entry + 8);
    const auto count = read_u64(entry + 16);

    std::uint64_t byte_count = 0;

    if (!multiply_u64(count, record_size, byte_count) ||
        offset > image.size() ||
        byte_count >
            image.size() -
                static_cast<std::size_t>(offset))
        return {};

    return image.subspan(
        static_cast<std::size_t>(offset),
        static_cast<std::size_t>(byte_count));
}

runtime_project_image_result
persist_runtime_project(
    const std::filesystem::path& path,
    const compiled_project_view& project,
    const server_abi_configuration& abi) noexcept {

    if (path.empty() || !project.valid())
        return runtime_project_image_result::invalid_state;

    shm_layout layout;

    if (prepare_shm_layout(project, abi, layout) !=
        shm_layout_result::success)
        return runtime_project_image_result::failed;

    shm_type_batch type;

    if (prepare_shm_type_batch_inline64(
            project,
            abi,
            layout,
            type) !=
        shm_type_batch_result::success)
        return runtime_project_image_result::failed;

    compiled_project_runtime_type_counts type_counts;

    shm_type_batch_compiled_counts(type, type_counts);

    runtime_project_layout image_layout;

    if (prepare_runtime_project_layout(
            project,
            type_counts,
            image_layout) !=
        runtime_project_image_result::success)
        return runtime_project_image_result::failed;

    writable_file_mapping mapping;

    if (mapping.create(path, image_layout.size()) !=
        writable_file_mapping_result::success)
        return runtime_project_image_result::io_failed;

    if (encode_runtime_project_image(
            image_layout,
            mapping.bytes()) !=
            runtime_project_image_result::success ||
        encode_shm_layout_columns(
            layout,
            abi,
            mapping.bytes()) !=
            shm_layout_result::success ||
        encode_shm_type_batch_physical_columns(
            type,
            mapping.bytes()) !=
            shm_type_batch_result::success ||
        encode_shm_runtime_v2_physical_columns(
            project,
            abi,
            layout,
            mapping.bytes()) !=
            shm_runtime_v2_result::success) {

        mapping.reset();

        try {
            std::error_code error;
            (void)std::filesystem::remove(path, error);
        }
        catch (...) {
        }

        return runtime_project_image_result::invalid_image;
    }

    runtime_project_view validation;

    if (validation.bind(mapping.bytes()) !=
        runtime_project_image_result::success) {

        mapping.reset();

        try {
            std::error_code error;
            (void)std::filesystem::remove(path, error);
        }
        catch (...) {
        }

        return runtime_project_image_result::invalid_image;
    }

    if (mapping.flush() != writable_file_mapping_result::success) {
        mapping.reset();

        try {
            std::error_code error;
            (void)std::filesystem::remove(path, error);
        }
        catch (...) {
        }

        return runtime_project_image_result::io_failed;
    }

    return runtime_project_image_result::success;
}

}
