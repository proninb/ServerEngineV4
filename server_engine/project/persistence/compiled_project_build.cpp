#include "compiled_project_build.hpp"

#include "../graph/construction_semantics.hpp"
#include "../../filesystem_path.hpp"
#include "crc64_ecma.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> build_image_magic{
    std::byte{'S'},
    std::byte{'E'},
    std::byte{'C'},
    std::byte{'M'},
    std::byte{'P'},
    std::byte{'V'},
    std::byte{'4'},
    std::byte{0},
};

constexpr std::uint32_t build_endian_marker =
    0x01020304u;

constexpr std::size_t build_directory_offset =
    compiled_project_header_size;

constexpr std::size_t build_directory_bytes =
    compiled_project_directory_count *
    compiled_project_directory_entry_size;

constexpr std::size_t build_first_section_offset =
    compiled_project_prefix_size;

constexpr std::size_t build_header_string_count_offset = 48;
constexpr std::size_t build_header_identity_count_offset = 56;
constexpr std::size_t build_header_type_count_offset = 64;
constexpr std::size_t build_header_object_count_offset = 72;
constexpr std::size_t build_header_link_count_offset = 80;
constexpr std::size_t build_header_assign_count_offset = 88;
constexpr std::size_t build_header_directory_crc_offset = 240;
constexpr std::size_t build_header_crc_offset = 248;

constexpr std::uint32_t build_string_core_size = 8;
constexpr std::uint32_t build_index_record_size = 8;
constexpr std::uint32_t build_identity_core_size = 12;
constexpr std::uint32_t build_type_record_size = 20;
constexpr std::uint32_t build_type_identity_size = 4;
constexpr std::uint32_t build_base_record_size = 8;
constexpr std::uint32_t build_member_record_size = 12;
constexpr std::uint32_t build_construction_record_size = 16;
constexpr std::uint32_t build_derived_record_size = 16;
constexpr std::uint32_t build_object_record_size = 8;
constexpr std::uint32_t build_object_identity_size = 4;
constexpr std::uint32_t build_link_record_size = 16;
constexpr std::uint32_t build_initialization_record_size = 24;
constexpr std::uint32_t build_endpoint_path_record_size = 16;
constexpr std::uint32_t build_endpoint_path_step_record_size = 16;
constexpr std::uint32_t build_graph_identity_record_size = 4;
constexpr std::uint32_t build_assign_record_size = 16;

[[nodiscard]] constexpr std::size_t build_section_index(
    compiled_project_section kind) noexcept {

    const auto raw =
        static_cast<std::uint32_t>(
            kind);

    return raw >= 1 &&
        raw <=
            compiled_project_directory_count
        ? static_cast<std::size_t>(
            raw - 1)
        : compiled_project_directory_count;
}

void build_write_u16(
    std::byte* target,
    std::uint16_t value) noexcept {

    if constexpr (
        std::endian::native ==
        std::endian::little) {

        std::memcpy(
            target,
            &value,
            sizeof(value));

        return;
    }

    target[0] =
        static_cast<std::byte>(
            value & 0xffu);

    target[1] =
        static_cast<std::byte>(
            (value >> 8) & 0xffu);
}

void build_write_u32(
    std::byte* target,
    std::uint32_t value) noexcept {

    if constexpr (
        std::endian::native ==
        std::endian::little) {

        std::memcpy(
            target,
            &value,
            sizeof(value));

        return;
    }

    target[0] =
        static_cast<std::byte>(
            value & 0xffu);

    target[1] =
        static_cast<std::byte>(
            (value >> 8) & 0xffu);

    target[2] =
        static_cast<std::byte>(
            (value >> 16) & 0xffu);

    target[3] =
        static_cast<std::byte>(
            (value >> 24) & 0xffu);
}

void build_write_u64(
    std::byte* target,
    std::uint64_t value) noexcept {

    if constexpr (
        std::endian::native ==
        std::endian::little) {

        std::memcpy(
            target,
            &value,
            sizeof(value));

        return;
    }

    for (std::uint32_t index = 0;
         index < 8;
         ++index) {

        target[index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffu);
    }
}

[[nodiscard]] std::uint32_t build_read_u32(
    const std::byte* source) noexcept {

    if constexpr (
        std::endian::native ==
        std::endian::little) {

        std::uint32_t value = 0;

        std::memcpy(
            &value,
            source,
            sizeof(value));

        return value;
    }

    return
        static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(
                source[0])) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(
                 source[1])) << 8) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(
                 source[2])) << 16) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(
                 source[3])) << 24);
}

[[nodiscard]] std::uint64_t build_index_capacity(
    std::uint64_t live) noexcept {

    constexpr std::uint64_t minimum = 8;

    if (live >
        (std::numeric_limits<
            std::uint64_t>::max)() /
            2) {

        return 0;
    }

    const auto required =
        live * 2;

    std::uint64_t capacity =
        minimum;

    while (capacity < required) {
        if (capacity >
            (std::numeric_limits<
                std::uint64_t>::max)() /
                2) {

            return 0;
        }

        capacity *= 2;
    }

    return capacity;
}

[[nodiscard]] std::uint32_t build_string_hash(
    std::string_view value) noexcept {

    std::uint32_t hash = 2166136261u;

    for (const auto character :
         value) {

        hash ^=
            static_cast<std::uint8_t>(
                static_cast<unsigned char>(
                    character));

        hash *= 16777619u;
    }

    return hash == 0
        ? 1
        : hash;
}

[[nodiscard]] constexpr std::uint64_t build_mix64(
    std::uint64_t value) noexcept {

    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;

    return value;
}

[[nodiscard]] std::uint64_t build_identity_hash(
    std::uint32_t parent,
    std::uint32_t name,
    std::uint32_t kind) noexcept {

    const auto value =
        static_cast<std::uint64_t>(
            parent) |
        (static_cast<std::uint64_t>(
             name) << 32);

    return build_mix64(
        value ^
        (static_cast<std::uint64_t>(
             kind) *
         0x9e3779b97f4a7c15ULL));
}

[[nodiscard]] std::uint32_t build_fingerprint(
    std::uint64_t hash) noexcept {

    auto value =
        static_cast<std::uint32_t>(
            hash ^ (hash >> 32));

    return value == 0
        ? 1
        : value;
}

[[nodiscard]] std::uint64_t build_member_hash(
    type_handle type,
    string_id name) noexcept {

    const auto key =
        (static_cast<std::uint64_t>(
             type.value()) << 32) |
        name.value();

    return build_mix64(
        key);
}

[[nodiscard]] std::uint64_t build_derived_hash(
    type_ref child,
    derived_type_kind kind,
    std::uint64_t payload) noexcept {

    std::uint64_t hash =
        1469598103934665603ull;

    const auto mix =
        [&hash](
            std::uint64_t value) noexcept {

            for (std::size_t index = 0;
                 index < 8;
                 ++index) {

                hash ^=
                    static_cast<std::uint8_t>(
                        value & 0xffu);

                hash *=
                    1099511628211ull;

                value >>= 8;
            }
        };

    mix(child.value());
    mix(static_cast<std::uint8_t>(
        kind));
    mix(payload);

    return hash == 0
        ? 1
        : hash;
}

void build_path_hash_mix(
    std::uint64_t& hash,
    std::uint64_t value) noexcept {

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        hash ^=
            static_cast<std::uint8_t>(
                value & 0xffu);

        hash *=
            1099511628211ull;

        value >>= 8;
    }
}

[[nodiscard]] std::uint64_t build_link_target_hash(
    object_endpoint target) noexcept {

    const auto key =
        (static_cast<std::uint64_t>(
             target.object.value()) << 32) |
        (static_cast<std::uint64_t>(
             target.member.value()) +
         1);

    return build_mix64(
        key);
}

[[nodiscard]] bool build_valid_record_kind(
    graph_record_kind kind) noexcept {

    return kind ==
            graph_record_kind::struct_type ||
        kind ==
            graph_record_kind::class_type ||
        kind ==
            graph_record_kind::union_type;
}

[[nodiscard]] bool build_valid_member_access(
    graph_member_access access) noexcept {

    return access ==
            graph_member_access::public_access ||
        access ==
            graph_member_access::protected_access ||
        access ==
            graph_member_access::private_access;
}

[[nodiscard]] bool build_valid_derived_kind(
    derived_type_kind kind) noexcept {

    return kind >=
            derived_type_kind::const_qualified &&
        kind <=
            derived_type_kind::unbounded_array;
}

[[nodiscard]] std::uint32_t build_graph_location(
    std::uint32_t kind,
    std::uint32_t slot) noexcept {

    if (kind == 0 ||
        kind > 2 ||
        slot == 0 ||
        slot >
            type_ref::maximum_payload) {

        return 0;
    }

    return
        (kind << 30) |
        slot;
}

}

compiled_project_image_result
prepare_build_compiled_project_layout(
    const string_table& strings,
    const identity_space& identities,
    const graph_delta& G,
    const graph_dense_projection& projection,
    const assign_overlay_view& assigns,
    const file_context& files,
    const source_map_overlay_view& sources,
    compiled_project_layout& output) noexcept {

    output = {};

    if (!G.baseline_bound() ||
        !projection.prepared_for(G) ||
        !assigns.valid() ||
        !sources.valid() ||
        identities.size() == 0) {

        return compiled_project_image_result::
            invalid_state;
    }

    std::uint64_t source_path_bytes = 0;
    std::uint64_t source_contributions = 0;

    for (std::size_t index = 0;
         index < files.size();
         ++index) {

        const file_id file{
            static_cast<std::uint32_t>(
                index + 1)};

        std::size_t length = 0;

        if (filesystem_path_utf8_size(
                files.path(file),
                length) !=
                filesystem_path_result::
                    success ||
            length >
                (std::numeric_limits<
                    std::uint32_t>::max)() ||
            source_path_bytes >
                (std::numeric_limits<
                    std::uint32_t>::max)() -
                    length) {

            return compiled_project_image_result::
                invalid_state;
        }

        source_path_bytes +=
            length;

        std::size_t contribution_count = 0;

        if (!sources.contributions(
                file,
                contribution_count) ||
            contribution_count >
                (std::numeric_limits<
                    std::uint32_t>::max)() -
                    source_contributions) {

            return compiled_project_image_result::
                invalid_state;
        }

        source_contributions +=
            contribution_count;
    }

    const compiled_project_layout::
        preparation_counts counts{
            static_cast<std::uint64_t>(
                strings.size()),
            static_cast<std::uint64_t>(
                strings.byte_size()),
            static_cast<std::uint64_t>(
                identities.size()),
            static_cast<std::uint64_t>(
                projection.type_count()),
            static_cast<std::uint64_t>(
                projection.member_count()),
            static_cast<std::uint64_t>(
                projection.base_count()),
            static_cast<std::uint64_t>(
                projection.derived_type_count()),
            static_cast<std::uint64_t>(
                projection.object_count()),
            static_cast<std::uint64_t>(
                projection.object_count()),
            static_cast<std::uint64_t>(
                projection.link_count()),
            static_cast<std::uint64_t>(
                G.initialization_count()),
            static_cast<std::uint64_t>(
                projection.endpoint_path_count()),
            static_cast<std::uint64_t>(
                projection.endpoint_path_step_count()),
            static_cast<std::uint64_t>(
                assigns.size()),
            static_cast<std::uint64_t>(
                assigns.byte_size()),
            source_contributions,
            static_cast<std::uint64_t>(
                files.size()),
            source_path_bytes,
            G.constructor_defaults.entries().size(),
            0,
            0,
        };

    return compiled_project_layout::
        prepare_counts(
            counts,
            output);
}

compiled_project_image_result
encode_build_compiled_project_image(
    const string_table& strings,
    const identity_space& identities,
    const graph_delta& G,
    const graph_dense_projection& projection,
    const assign_overlay_view& assigns,
    const file_context& files,
    const source_map_overlay_view& sources,
    const compiled_project_layout& prepared_layout,
    std::span<std::byte> output) noexcept {

    if (!G.baseline_bound() ||
        !projection.prepared_for(G) ||
        !assigns.valid() ||
        !sources.valid() ||
        prepared_layout.size() <
            build_first_section_offset ||
        output.size() !=
            prepared_layout.size()) {

        return compiled_project_image_result::
            invalid_state;
    }

    const auto& layout =
        prepared_layout.sections;

    const auto count =
        [&](compiled_project_section kind) noexcept
        -> std::uint64_t {

            return layout[
                build_section_index(
                    kind)].count;
        };

    if (count(
            compiled_project_section::
                string_core) !=
            strings.size() ||
        count(
            compiled_project_section::
                string_bytes) !=
            strings.byte_size() ||
        count(
            compiled_project_section::
                identity_core) !=
            identities.size() ||
        count(
            compiled_project_section::
                types) !=
            projection.type_count() ||
        count(
            compiled_project_section::
                type_identities) !=
            projection.type_count() ||
        count(
            compiled_project_section::
                members) !=
            projection.member_count() ||
        count(
            compiled_project_section::
                member_construction) !=
            projection.member_count() ||
        count(
            compiled_project_section::
                bases) !=
            projection.base_count() ||
        count(
            compiled_project_section::
                derived_types) !=
            projection.derived_type_count() ||
        count(
            compiled_project_section::
                objects) !=
            projection.object_count() ||
        count(
            compiled_project_section::
                object_identities) !=
            projection.object_count() ||
        count(
            compiled_project_section::
                object_construction) !=
            projection.object_count() ||
        count(
            compiled_project_section::
                links) !=
            projection.link_count() ||
        count(
            compiled_project_section::
                object_initializations) !=
            G.initialization_count() ||
        count(
            compiled_project_section::
                endpoint_paths) !=
            projection.endpoint_path_count() ||
        count(
            compiled_project_section::
                endpoint_path_steps) !=
            projection.endpoint_path_step_count() ||
        count(
            compiled_project_section::
                assign_records) !=
            assigns.size() ||
        count(
            compiled_project_section::
                assign_bytes) !=
            assigns.byte_size() ||
        count(
            compiled_project_section::
                assign_files) !=
            assigns.size() ||
        count(
            compiled_project_section::
                source_files) !=
            files.size() ||
        count(
            compiled_project_section::
                source_roots) !=
            files.size() ||
        count(
            compiled_project_section::
                source_file_indices) !=
            count(
                compiled_project_section::
                    source_contributions) ||
        count(
            compiled_project_section::
                graph_identity_index) !=
            identities.size() + 1 ||
        count(
            compiled_project_section::
                string_index) !=
            build_index_capacity(
                strings.size()) ||
        count(
            compiled_project_section::
                identity_index) !=
            build_index_capacity(
                identities.size() > 0
                ? identities.size() - 1
                : 0) ||
        count(
            compiled_project_section::
                member_name_index) !=
            build_index_capacity(
                projection.member_count()) ||
        count(
            compiled_project_section::
                derived_index) !=
            build_index_capacity(
                projection.derived_type_count()) ||
        count(
            compiled_project_section::
                link_target_index) !=
            build_index_capacity(
                projection.link_count()) ||
        count(
            compiled_project_section::
                endpoint_path_index) !=
            build_index_capacity(
                projection.endpoint_path_count()) ||
        count(
            compiled_project_section::
                object_initialization_target_index) !=
            build_index_capacity(
                G.initialization_count())) {

        return compiled_project_image_result::
            invalid_state;
    }

    auto* base =
        output.data();

    std::fill(
        output.begin(),
        output.begin() +
            static_cast<std::ptrdiff_t>(
                build_first_section_offset),
        std::byte{0});

    std::uint64_t previous_end =
        build_first_section_offset;

    for (const auto& section :
         layout) {

        const auto bytes =
            section.count *
            section.record_size;

        if (section.offset <
                previous_end ||
            section.offset >
                output.size() ||
            bytes >
                output.size() -
                    section.offset) {

            return compiled_project_image_result::
                invalid_state;
        }

        if (section.offset >
            previous_end) {

            std::fill(
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        previous_end),
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        section.offset),
                std::byte{0});
        }

        previous_end =
            section.offset +
            bytes;
    }

    if (previous_end !=
        output.size()) {

        return compiled_project_image_result::
            invalid_state;
    }

    const auto section_data =
        [&](compiled_project_section kind) noexcept
        -> std::byte* {

            return base +
                static_cast<std::size_t>(
                    layout[
                        build_section_index(
                            kind)].offset);
        };

    const auto clear_section =
        [&](compiled_project_section kind) noexcept {

            const auto& section =
                layout[
                    build_section_index(
                        kind)];

            std::fill(
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        section.offset),
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        section.offset +
                        section.count *
                            section.record_size),
                std::byte{0});
        };

    for (const auto kind :
         {
             compiled_project_section::string_index,
             compiled_project_section::identity_index,
             compiled_project_section::graph_identity_index,
             compiled_project_section::object_construction,
             compiled_project_section::member_name_index,
             compiled_project_section::derived_index,
             compiled_project_section::link_target_index,
             compiled_project_section::endpoint_path_index,
             compiled_project_section::object_initialization_target_index,
             compiled_project_section::source_roots,
             compiled_project_section::source_files,
             compiled_project_section::runtime_abi_header,
             compiled_project_section::type_abi,
             compiled_project_section::derived_abi,
             compiled_project_section::member_abi,
             compiled_project_section::base_abi,
             compiled_project_section::object_abi,
             compiled_project_section::unconnected_intrinsic_abi,
             compiled_project_section::unconnected_type_abi,
             compiled_project_section::unconnected_derived_abi,
             compiled_project_section::unconnected_types,
             compiled_project_section::runtime_execution_header,
             compiled_project_section::link_runtime,
             compiled_project_section::initialization_runtime,
             compiled_project_section::runtime_endpoint_programs,
             compiled_project_section::runtime_endpoint_dereferences,
         }) {

        clear_section(
            kind);
    }

    {
        auto* core =
            section_data(
                compiled_project_section::
                    string_core);

        auto* index =
            section_data(
                compiled_project_section::
                    string_index);

        auto* data =
            section_data(
                compiled_project_section::
                    string_bytes);

        const auto mask =
            count(
                compiled_project_section::
                    string_index) - 1;

        std::uint32_t byte_offset = 0;

        for (std::uint32_t slot = 1;
             slot <= strings.size();
             ++slot) {

            const auto value =
                strings.spelling(
                    slot);

            if (value.empty()) {
                return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                core +
                static_cast<std::size_t>(
                    slot - 1) *
                    build_string_core_size;

            build_write_u32(
                record,
                byte_offset);

            build_write_u32(
                record + 4,
                static_cast<std::uint32_t>(
                    value.size()));

            std::memcpy(
                data + byte_offset,
                value.data(),
                value.size());

            const auto hash =
                build_string_hash(
                    value);

            auto position =
                static_cast<std::uint64_t>(
                    hash) &
                mask;

            for (;;) {
                auto* index_record =
                    index +
                    static_cast<std::size_t>(
                        position) *
                        build_index_record_size;

                if (build_read_u32(
                        index_record + 4) == 0) {

                    build_write_u32(
                        index_record,
                        hash);

                    build_write_u32(
                        index_record + 4,
                        slot);

                    break;
                }

                position =
                    (position + 1) &
                    mask;
            }

            byte_offset +=
                static_cast<std::uint32_t>(
                    value.size());
        }

        if (byte_offset !=
            count(
                compiled_project_section::
                    string_bytes)) {

            return compiled_project_image_result::
                invalid_state;
        }
    }

    {
        auto* core =
            section_data(
                compiled_project_section::
                    identity_core);

        auto* index =
            section_data(
                compiled_project_section::
                    identity_index);

        build_write_u32(
            core,
            identities.root().value());

        build_write_u32(
            core + 4,
            0);

        build_write_u32(
            core + 8,
            0);

        const auto mask =
            count(
                compiled_project_section::
                    identity_index) - 1;

        for (std::uint32_t slot = 2;
             slot <= identities.size();
             ++slot) {

            const auto identity =
                identities.at_slot(
                    slot);

            identity_record value;

            if (!identity ||
                !identities.record(
                    identity,
                    value) ||
                !identities.contains(
                    value.parent) ||
                value.parent.slot() >=
                    slot ||
                !strings.contains(
                    value.name)) {

                return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                core +
                static_cast<std::size_t>(
                    slot - 1) *
                    build_identity_core_size;

            build_write_u32(
                record,
                identity.value());

            build_write_u32(
                record + 4,
                value.parent.value());

            build_write_u32(
                record + 8,
                value.name.value());

            const auto hash =
                build_identity_hash(
                    value.parent.value(),
                    value.name.value(),
                    static_cast<std::uint32_t>(
                        identity.kind()));

            const auto fingerprint =
                build_fingerprint(
                    hash);

            auto position =
                hash &
                mask;

            for (;;) {
                auto* index_record =
                    index +
                    static_cast<std::size_t>(
                        position) *
                        build_index_record_size;

                if (build_read_u32(
                        index_record + 4) == 0) {

                    build_write_u32(
                        index_record,
                        fingerprint);

                    build_write_u32(
                        index_record + 4,
                        identity.value());

                    break;
                }

                position =
                    (position + 1) &
                    mask;
            }
        }
    }

    auto* graph_identity =
        section_data(
            compiled_project_section::
                graph_identity_index);

    auto* type_values =
        section_data(
            compiled_project_section::
                types);

    auto* type_identities =
        section_data(
            compiled_project_section::
                type_identities);

    auto* base_values =
        section_data(
            compiled_project_section::
                bases);

    auto* member_values =
        section_data(
            compiled_project_section::
                members);

    auto* member_construction =
        section_data(
            compiled_project_section::
                member_construction);

    auto* member_index =
        section_data(
            compiled_project_section::
                member_name_index);

    std::uint32_t dense_type_cursor = 0;
    std::uint32_t dense_base_cursor = 0;
    std::uint32_t dense_member_cursor = 0;

    const auto member_index_mask =
        count(
            compiled_project_section::
                member_name_index) - 1;

    for (std::size_t lineage_index = 0;
         lineage_index <
            G.type_count();
         ++lineage_index) {

        const auto lineage =
            G.type_at(
                lineage_index);

        if (!lineage) {
            continue;
        }

        const auto dense =
            projection.remap(
                lineage);

        type_entry value;
        const auto identity =
            G.identity(
                lineage);

        if (!dense ||
            dense.value() !=
                dense_type_cursor + 1 ||
            !G.type(
                lineage,
                value) ||
            !value.valid_kind() ||
            !build_valid_record_kind(
                value.record_kind) ||
            (value.flags &
                ~graph_type_flag_mask) != 0 ||
            (!value.defined() &&
             (value.members.count != 0 ||
              value.bases.count != 0)) ||
            (value.record_kind ==
                 graph_record_kind::union_type &&
             (value.bases.count != 0 ||
              value.polymorphic())) ||
            !identities.contains(
                identity) ||
            identity.kind() !=
                identity_kind::type) {

            return compiled_project_image_result::
                invalid_state;
        }

        auto* type_record =
            type_values +
            static_cast<std::size_t>(
                dense_type_cursor) *
                build_type_record_size;

        build_write_u32(
            type_record,
            dense_member_cursor);

        build_write_u32(
            type_record + 4,
            value.members.count);

        build_write_u32(
            type_record + 8,
            dense_base_cursor);

        build_write_u32(
            type_record + 12,
            value.bases.count);

        type_record[16] =
            static_cast<std::byte>(
                static_cast<std::uint8_t>(
                    value.kind));

        type_record[17] =
            static_cast<std::byte>(
                static_cast<std::uint8_t>(
                    value.record_kind));

        build_write_u16(
            type_record + 18,
            value.flags);

        build_write_u32(
            type_identities +
                static_cast<std::size_t>(
                    dense_type_cursor) *
                    build_type_identity_size,
            identity.value());

        auto* location =
            graph_identity +
            static_cast<std::size_t>(
                identity.slot()) *
                build_graph_identity_record_size;

        if (build_read_u32(
                location) != 0) {

            return compiled_project_image_result::
                invalid_state;
        }

        build_write_u32(
            location,
            build_graph_location(
                1,
                dense.value()));

        for (std::uint32_t local = 0;
             local <
                value.bases.count;
             ++local) {

            base_record base_value;

            if (!G.base(
                    lineage,
                    local,
                    base_value) ||
                !build_valid_member_access(
                    base_value.access) ||
                (base_value.flags &
                    ~graph_base_flag_mask) != 0 ||
                base_value.reserved != 0) {

                return compiled_project_image_result::
                    invalid_state;
            }

            const auto base_lineage =
                G.find_type(
                    base_value.type);

            const auto dense_base_type =
                projection.remap(
                    base_lineage);

            if (!base_value.type ||
                base_value.type.kind() !=
                    identity_kind::type ||
                !base_lineage ||
                !dense_base_type) {

                return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                base_values +
                static_cast<std::size_t>(
                    dense_base_cursor) *
                    build_base_record_size;

            build_write_u32(
                record,
                base_value.type.value());

            record[4] =
                static_cast<std::byte>(
                    static_cast<std::uint8_t>(
                        base_value.access));

            record[5] =
                static_cast<std::byte>(
                    base_value.flags);

            build_write_u16(
                record + 6,
                0);

            ++dense_base_cursor;
        }

        const auto type_member_begin =
            dense_member_cursor;

        for (std::uint32_t local = 0;
             local <
                value.members.count;
             ++local) {

            member_record member;
            construction_value initial;
            construction_value dense_initial;

            if (!G.member(
                    lineage,
                    local,
                    member) ||
                !G.construction(
                    lineage,
                    local,
                    initial) ||
                !strings.contains(
                    member.name) ||
                !build_valid_member_access(
                    member.access) ||
                !projection.remap(
                    initial,
                    dense_initial)) {

                return compiled_project_image_result::
                    invalid_state;
            }

            const auto dense_member_type =
                projection.remap(
                    member.type);

            if (!dense_member_type ||
                !valid_construction(
                    dense_initial)) {

                return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                member_values +
                static_cast<std::size_t>(
                    dense_member_cursor) *
                    build_member_record_size;

            build_write_u32(
                record,
                member.name.value());

            build_write_u32(
                record + 4,
                dense_member_type.value());

            build_write_u32(
                record + 8,
                static_cast<std::uint32_t>(
                    member.access));

            auto* initial_record =
                member_construction +
                static_cast<std::size_t>(
                    dense_member_cursor) *
                    build_construction_record_size;

            build_write_u32(
                initial_record,
                dense_initial.low);

            build_write_u32(
                initial_record + 4,
                dense_initial.high);

            build_write_u32(
                initial_record + 8,
                dense_initial.operand);

            build_write_u32(
                initial_record + 12,
                static_cast<std::uint32_t>(
                    dense_initial.kind));

            const auto hash =
                build_member_hash(
                    dense,
                    member.name);

            const auto fingerprint =
                build_fingerprint(
                    hash);

            auto position =
                hash &
                member_index_mask;

            for (;;) {
                auto* slot =
                    member_index +
                    static_cast<std::size_t>(
                        position) *
                        build_index_record_size;

                const auto raw =
                    build_read_u32(
                        slot + 4);

                if (raw == 0) {
                    build_write_u32(
                        slot,
                        fingerprint);

                    build_write_u32(
                        slot + 4,
                        dense_member_cursor + 1);

                    break;
                }

                if (build_read_u32(
                        slot) ==
                    fingerprint) {

                    const auto existing =
                        raw - 1;

                    if (existing >=
                            type_member_begin &&
                        existing <
                            dense_member_cursor &&
                        build_read_u32(
                            member_values +
                            static_cast<std::size_t>(
                                existing) *
                                build_member_record_size) ==
                            member.name.value()) {

                        return compiled_project_image_result::
                            invalid_state;
                    }
                }

                position =
                    (position + 1) &
                    member_index_mask;
            }

            ++dense_member_cursor;
        }

        ++dense_type_cursor;
    }

    if (dense_type_cursor !=
            projection.type_count() ||
        dense_member_cursor !=
            projection.member_count() ||
        dense_base_cursor !=
            projection.base_count()) {

        return compiled_project_image_result::
            invalid_state;
    }

    {
        auto* values =
            section_data(
                compiled_project_section::
                    derived_types);

        auto* index =
            section_data(
                compiled_project_section::
                    derived_index);

        const auto mask =
            count(
                compiled_project_section::
                    derived_index) - 1;

        std::uint32_t dense_cursor = 0;

        for (std::size_t lineage_index = 0;
             lineage_index <
                G.derived_type_count();
             ++lineage_index) {

            const auto lineage =
                G.derived_at(
                    lineage_index);

            const auto dense =
                projection.remap(
                    lineage);

            if (!dense) {
                continue;
            }

            derived_type_record value;

            if (dense.kind() !=
                    type_ref_kind::derived ||
                dense.payload() !=
                    dense_cursor + 1 ||
                !G.derived(
                    lineage,
                    value) ||
                !build_valid_derived_kind(
                    value.kind)) {

                return compiled_project_image_result::
                    invalid_state;
            }

            const auto child =
                projection.remap(
                    value.child);

            if (!child ||
                (child.kind() ==
                        type_ref_kind::derived &&
                 child.payload() >=
                    dense.payload())) {

                return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                values +
                static_cast<std::size_t>(
                    dense_cursor) *
                    build_derived_record_size;

            build_write_u64(
                record,
                value.payload);

            build_write_u32(
                record + 8,
                child.value());

            build_write_u32(
                record + 12,
                static_cast<std::uint32_t>(
                    value.kind));

            const auto hash =
                build_derived_hash(
                    child,
                    value.kind,
                    value.payload);

            auto position =
                hash &
                mask;

            for (;;) {
                auto* slot =
                    index +
                    static_cast<std::size_t>(
                        position) *
                        build_index_record_size;

                if (build_read_u32(
                        slot + 4) == 0) {

                    build_write_u32(
                        slot,
                        build_fingerprint(
                            hash));

                    build_write_u32(
                        slot + 4,
                        dense.value());

                    break;
                }

                position =
                    (position + 1) &
                    mask;
            }

            ++dense_cursor;
        }

        if (dense_cursor !=
            projection.derived_type_count()) {

            return compiled_project_image_result::
                invalid_state;
        }
    }

    {
        auto* values =
            section_data(
                compiled_project_section::
                    objects);

        auto* identities_out =
            section_data(
                compiled_project_section::
                    object_identities);

        auto* construction_out =
            section_data(
                compiled_project_section::
                    object_construction);

        std::uint32_t dense_cursor = 0;

        for (std::size_t lineage_index = 0;
             lineage_index <
                G.object_count();
             ++lineage_index) {

            const auto lineage =
                G.object_at(
                    lineage_index);

            if (!lineage) {
                continue;
            }

            const auto dense =
                projection.remap(
                    lineage);

            object_entry value;
            construction_value initial;
            construction_value dense_initial;

            const auto identity =
                G.identity(
                    lineage);

            if (!dense ||
                dense.value() !=
                    dense_cursor + 1 ||
                !G.object(
                    lineage,
                    value) ||
                !G.construction(
                    lineage,
                    initial) ||
                !projection.remap(
                    initial,
                    dense_initial) ||
                !identities.contains(
                    identity) ||
                identity.kind() !=
                    identity_kind::object) {

                return compiled_project_image_result::
                    invalid_state;
            }

            const auto dense_type =
                projection.remap(
                    value.type);

            if (!dense_type) {
                return compiled_project_image_result::
                    invalid_state;
            }

            std::uint32_t state =
                value.state &
                graph_object_internal_static;

            if (value.non_default_initializer()) {
                if (!valid_construction(
                        dense_initial) ||
                    dense_initial.kind ==
                        construction_kind::member_binding ||
                    dense_initial.kind ==
                        construction_kind::object_binding ||
                    dense.value() >
                        graph_object_construction_slot_mask) {

                    return compiled_project_image_result::invalid_state;
                }

                state |=
                    graph_object_non_default_initializer |
                    dense.value();

                auto* initial_record =
                    construction_out +
                    static_cast<std::size_t>(
                        dense.value() - 1) *
                        build_construction_record_size;

                build_write_u32(initial_record, dense_initial.low);
                build_write_u32(initial_record + 4, dense_initial.high);
                build_write_u32(initial_record + 8, dense_initial.operand);
                build_write_u32(
                    initial_record + 12,
                    static_cast<std::uint32_t>(dense_initial.kind));
            }
            else if (dense_initial != construction_value{}) {
                return compiled_project_image_result::invalid_state;
            }

            auto* record =
                values +
                static_cast<std::size_t>(
                    dense_cursor) *
                    build_object_record_size;

            build_write_u32(
                record,
                dense_type.value());

            build_write_u32(
                record + 4,
                state);

            build_write_u32(
                identities_out +
                    static_cast<std::size_t>(
                        dense_cursor) *
                        build_object_identity_size,
                identity.value());

            auto* location =
                graph_identity +
                static_cast<std::size_t>(
                    identity.slot()) *
                    build_graph_identity_record_size;

            if (build_read_u32(
                    location) != 0) {

                return compiled_project_image_result::
                    invalid_state;
            }

            build_write_u32(
                location,
                build_graph_location(
                    2,
                    dense.value()));

            ++dense_cursor;
        }

        if (dense_cursor !=
                projection.object_count()) {

            return compiled_project_image_result::
                invalid_state;
        }
    }

    {
        auto* paths =
            section_data(
                compiled_project_section::
                    endpoint_paths);

        auto* steps =
            section_data(
                compiled_project_section::
                    endpoint_path_steps);

        auto* index =
            section_data(
                compiled_project_section::
                    endpoint_path_index);

        const auto mask =
            count(
                compiled_project_section::
                    endpoint_path_index) - 1;

        std::uint32_t dense_path_cursor = 0;
        std::uint32_t dense_step_cursor = 0;

        for (std::size_t lineage_index = 0;
             lineage_index <
                G.endpoint_path_count();
             ++lineage_index) {

            const auto lineage =
                G.endpoint_path_at(
                    lineage_index);

            const auto dense =
                projection.remap(
                    lineage);

            if (!dense) {
                continue;
            }

            endpoint_path_record value;

            if (dense.value() !=
                    dense_path_cursor + 1 ||
                !G.endpoint_path(
                    lineage,
                    value)) {

                return compiled_project_image_result::
                    invalid_state;
            }

            const auto root_type =
                projection.remap(
                    value.root_type);

            const auto value_type =
                projection.remap(
                    value.value_type);

            if (!root_type ||
                !value_type) {

                return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                paths +
                static_cast<std::size_t>(
                    dense_path_cursor) *
                    build_endpoint_path_record_size;

            build_write_u32(
                record,
                dense_step_cursor);

            build_write_u32(
                record + 4,
                value.steps.count);

            build_write_u32(
                record + 8,
                root_type.value());

            build_write_u32(
                record + 12,
                value_type.value());

            std::uint64_t hash =
                1469598103934665603ull;

            build_path_hash_mix(
                hash,
                root_type.value());

            for (std::uint32_t local = 0;
                 local <
                    value.steps.count;
                 ++local) {

                endpoint_path_step step;

                if (!G.endpoint_path_step_at(
                        static_cast<std::size_t>(
                            value.steps.begin) +
                            local,
                        step) ||
                    (step.kind !=
                         endpoint_path_step_kind::
                             member &&
                     step.kind !=
                         endpoint_path_step_kind::
                             array_index)) {

                    return compiled_project_image_result::
                        invalid_state;
                }

                for (const auto reserved :
                     step.reserved) {

                    if (reserved != 0) {
                        return compiled_project_image_result::
                            invalid_state;
                    }
                }

                auto* step_record =
                    steps +
                    static_cast<std::size_t>(
                        dense_step_cursor) *
                        build_endpoint_path_step_record_size;

                build_write_u64(
                    step_record,
                    step.value);

                step_record[8] =
                    static_cast<std::byte>(
                        static_cast<std::uint8_t>(
                            step.kind));

                std::fill_n(
                    step_record + 9,
                    7,
                    std::byte{0});

                build_path_hash_mix(
                    hash,
                    static_cast<std::uint8_t>(
                        step.kind));

                build_path_hash_mix(
                    hash,
                    step.value);

                ++dense_step_cursor;
            }

            if (hash == 0) {
                hash = 1;
            }

            auto position =
                hash &
                mask;

            for (;;) {
                auto* slot =
                    index +
                    static_cast<std::size_t>(
                        position) *
                        build_index_record_size;

                if (build_read_u32(
                        slot + 4) == 0) {

                    build_write_u32(
                        slot,
                        build_fingerprint(
                            hash));

                    build_write_u32(
                        slot + 4,
                        dense.value());

                    break;
                }

                position =
                    (position + 1) &
                    mask;
            }

            ++dense_path_cursor;
        }

        if (dense_path_cursor !=
                projection.endpoint_path_count() ||
            dense_step_cursor !=
                projection.endpoint_path_step_count()) {

            return compiled_project_image_result::
                invalid_state;
        }
    }

    {
        struct initialization_context final {
            const graph_dense_projection* projection = nullptr;
            std::byte* values = nullptr;
            std::byte* index = nullptr;
            std::uint64_t index_mask = 0;
            std::uint32_t cursor = 0;
            std::uint32_t expected = 0;
        };

        initialization_context context{
            &projection,
            section_data(
                compiled_project_section::
                    object_initializations),
            section_data(
                compiled_project_section::
                    object_initialization_target_index),
            count(
                compiled_project_section::
                    object_initialization_target_index) -
                1,
            0,
            static_cast<std::uint32_t>(
                G.initialization_count()),
        };

        const auto visitor =
            [](void* raw,
               const object_initialization_record& value) noexcept
            -> server_status {

                auto& context =
                    *static_cast<
                        initialization_context*>(
                            raw);

                if (context.cursor >=
                    context.expected) {

                    return server_status::
                        project_artifact_invalid;
                }

                object_endpoint target;
                construction_value initial;

                if (!context.projection->remap(
                        value.target,
                        target) ||
                    !context.projection->remap(
                        value.value,
                        initial) ||
                    !valid_construction(
                        initial)) {

                    return server_status::
                        project_artifact_invalid;
                }

                auto* record =
                    context.values +
                    static_cast<std::size_t>(
                        context.cursor) *
                        build_initialization_record_size;

                build_write_u32(
                    record,
                    target.object.value());

                build_write_u32(
                    record + 4,
                    target.member.value());

                build_write_u32(
                    record + 8,
                    initial.low);

                build_write_u32(
                    record + 12,
                    initial.high);

                build_write_u32(
                    record + 16,
                    initial.operand);

                build_write_u32(
                    record + 20,
                    static_cast<std::uint32_t>(
                        initial.kind));

                const auto hash =
                    build_link_target_hash(
                        target);

                auto position =
                    hash &
                    context.index_mask;

                for (;;) {
                    auto* slot =
                        context.index +
                        static_cast<std::size_t>(
                            position) *
                            build_index_record_size;

                    if (build_read_u32(
                            slot + 4) == 0) {

                        build_write_u32(
                            slot,
                            build_fingerprint(
                                hash));

                        build_write_u32(
                            slot + 4,
                            context.cursor + 1);

                        break;
                    }

                    position =
                        (position + 1) &
                        context.index_mask;
                }

                ++context.cursor;

                return server_status::success;
            };

        const auto visited =
            G.visit_initializations(
                &context,
                visitor);

        if (!succeeded(visited) ||
            context.cursor !=
                context.expected) {

            return compiled_project_image_result::
                invalid_state;
        }
    }

    {
        auto* values =
            section_data(
                compiled_project_section::
                    links);

        auto* index =
            section_data(
                compiled_project_section::
                    link_target_index);

        const auto mask =
            count(
                compiled_project_section::
                    link_target_index) - 1;

        std::uint32_t dense_cursor = 0;

        for (std::size_t lineage_index = 0;
             lineage_index <
                G.link_count();
             ++lineage_index) {

            const auto lineage =
                G.link_at(
                    lineage_index);

            if (!lineage) {
                continue;
            }

            const auto dense =
                projection.remap(
                    lineage);

            link_record value;
            object_endpoint source;
            object_endpoint target;

            if (!dense ||
                dense.value() !=
                    dense_cursor + 1 ||
                !G.link(
                    lineage,
                    value) ||
                !projection.remap(
                    value.source,
                    source) ||
                !projection.remap(
                    value.target,
                    target)) {

                return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                values +
                static_cast<std::size_t>(
                    dense_cursor) *
                    build_link_record_size;

            build_write_u32(
                record,
                source.object.value());

            build_write_u32(
                record + 4,
                source.member.value());

            build_write_u32(
                record + 8,
                target.object.value());

            build_write_u32(
                record + 12,
                target.member.value());

            const auto hash =
                build_link_target_hash(
                    target);

            auto position =
                hash &
                mask;

            for (;;) {
                auto* slot =
                    index +
                    static_cast<std::size_t>(
                        position) *
                        build_index_record_size;

                if (build_read_u32(
                        slot + 4) == 0) {

                    build_write_u32(
                        slot,
                        build_fingerprint(
                            hash));

                    build_write_u32(
                        slot + 4,
                        dense.value());

                    break;
                }

                position =
                    (position + 1) &
                    mask;
            }

            ++dense_cursor;
        }

        if (dense_cursor !=
            projection.link_count()) {

            return compiled_project_image_result::
                invalid_state;
        }
    }

    {
        struct assign_context final {
            const file_context* files = nullptr;
            std::byte* records = nullptr;
            std::byte* bytes = nullptr;
            std::byte* file_records = nullptr;
            std::uint32_t byte_cursor = 0;
            std::uint32_t record_cursor = 0;
            std::uint32_t expected_records = 0;
            std::uint32_t expected_bytes = 0;
        };

        assign_context context{
            &files,
            section_data(
                compiled_project_section::
                    assign_records),
            section_data(
                compiled_project_section::
                    assign_bytes),
            section_data(
                compiled_project_section::
                    assign_files),
            0,
            0,
            static_cast<std::uint32_t>(
                assigns.size()),
            static_cast<std::uint32_t>(
                assigns.byte_size()),
        };

        const auto visitor =
            [](void* raw,
               file_id file,
               std::string_view source,
               std::string_view target) noexcept
            -> server_status {

                auto& context =
                    *static_cast<
                        assign_context*>(
                            raw);

                if (!file ||
                    !context.files->contains(
                        file) ||
                    context.files->kind(
                        file) !=
                        file_kind::assign ||
                    source.empty() ||
                    target.empty() ||
                    context.record_cursor >=
                        context.expected_records ||
                    context.byte_cursor >
                        context.expected_bytes ||
                    source.size() >
                        context.expected_bytes -
                            context.byte_cursor ||
                    target.size() >
                        context.expected_bytes -
                            context.byte_cursor -
                            source.size()) {

                    return server_status::
                        project_artifact_invalid;
                }

                auto* record =
                    context.records +
                    static_cast<std::size_t>(
                        context.record_cursor) *
                        build_assign_record_size;

                build_write_u32(
                    record,
                    context.byte_cursor);

                build_write_u32(
                    record + 4,
                    static_cast<std::uint32_t>(
                        source.size()));

                std::memcpy(
                    context.bytes +
                        context.byte_cursor,
                    source.data(),
                    source.size());

                context.byte_cursor +=
                    static_cast<std::uint32_t>(
                        source.size());

                build_write_u32(
                    record + 8,
                    context.byte_cursor);

                build_write_u32(
                    record + 12,
                    static_cast<std::uint32_t>(
                        target.size()));

                std::memcpy(
                    context.bytes +
                        context.byte_cursor,
                    target.data(),
                    target.size());

                context.byte_cursor +=
                    static_cast<std::uint32_t>(
                        target.size());

                build_write_u32(
                    context.file_records +
                        static_cast<std::size_t>(
                            context.record_cursor) *
                            4,
                    file.value());

                ++context.record_cursor;

                return server_status::success;
            };

        const auto visited =
            assigns.visit(
                &context,
                visitor);

        if (!succeeded(visited) ||
            context.record_cursor !=
                context.expected_records ||
            context.byte_cursor !=
                context.expected_bytes) {

            return compiled_project_image_result::
                invalid_state;
        }
    }

    {
        auto* contributions =
            section_data(
                compiled_project_section::
                    source_contributions);

        auto* roots =
            section_data(
                compiled_project_section::
                    source_roots);

        auto* file_values =
            section_data(
                compiled_project_section::
                    source_files);

        auto* file_indices =
            section_data(
                compiled_project_section::
                    source_file_indices);

        auto* paths =
            section_data(
                compiled_project_section::
                    source_paths);

        std::size_t path_cursor = 0;

        for (std::size_t index = 0;
             index < files.size();
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            if (!files.contains(
                    file)) {

                return compiled_project_image_result::
                    invalid_state;
            }

            std::size_t written = 0;

            if (filesystem_path_to_utf8(
                    files.path(file),
                    {
                        reinterpret_cast<char*>(
                            paths) +
                            path_cursor,
                        static_cast<std::size_t>(
                            count(
                                compiled_project_section::
                                    source_paths)) -
                            path_cursor,
                    },
                    written) !=
                    filesystem_path_result::
                        success ||
                written == 0) {

                return compiled_project_image_result::
                    invalid_state;
            }

            auto* file_record =
                file_values +
                index * 20;

            build_write_u32(
                file_record,
                static_cast<std::uint32_t>(
                    files.kind(file)));

            build_write_u32(
                file_record + 4,
                static_cast<std::uint32_t>(
                    path_cursor));

            build_write_u32(
                file_record + 8,
                static_cast<std::uint32_t>(
                    written));

            build_write_u32(
                file_record + 12,
                0);

            build_write_u32(
                file_record + 16,
                0);

            build_write_u32(
                roots + index * 8,
                0);

            build_write_u32(
                roots + index * 8 + 4,
                0);

            path_cursor +=
                written;
        }

        if (path_cursor !=
            count(
                compiled_project_section::
                    source_paths)) {

            return compiled_project_image_result::
                invalid_state;
        }

        std::uint32_t contribution_cursor = 0;

        const auto emit_domain =
            [&](file_kind domain)
            -> compiled_project_image_result {

                for (std::size_t root_index = 0;
                     root_index <
                        files.size();
                     ++root_index) {

                    const file_id root{
                        static_cast<std::uint32_t>(
                            root_index + 1)};

                    if (files.kind(root) !=
                        domain) {

                        continue;
                    }

                    std::size_t root_count = 0;

                    if (!sources.contributions(
                            root,
                            root_count) ||
                        root_count >
                            (std::numeric_limits<
                                std::uint32_t>::max)() ||
                        root_count >
                            count(
                                compiled_project_section::
                                    source_contributions) -
                                contribution_cursor) {

                        return compiled_project_image_result::
                            invalid_state;
                    }

                    if (root_count == 0) {
                        continue;
                    }

                    build_write_u32(
                        roots +
                            root_index * 8,
                        contribution_cursor);

                    build_write_u32(
                        roots +
                            root_index * 8 +
                            4,
                        static_cast<std::uint32_t>(
                            root_count));

                    for (std::size_t local = 0;
                         local < root_count;
                         ++local) {

                        source_contribution_record
                            contribution;

                        if (!sources.contribution(
                                root,
                                local,
                                contribution) ||
                            !contribution.file ||
                            !files.contains(
                                contribution.file) ||
                            !contribution.data) {

                            return compiled_project_image_result::
                                invalid_state;
                        }

                        auto data =
                            contribution.data;

                        file_kind semantic_domain =
                            file_kind::project;

                        switch (data.kind()) {
                        case source_data_kind::
                                type_declaration:
                        case source_data_kind::
                                type_definition: {
                            const auto identity =
                                identities.at_slot(
                                    data.slot());

                            const auto lineage =
                                identity &&
                                    identity.kind() ==
                                        identity_kind::type
                                ? G.find_type(
                                    identity)
                                : type_handle{};

                            type_entry type_value;

                            if (!lineage ||
                                !projection.remap(
                                    lineage) ||
                                !G.type(
                                    lineage,
                                    type_value) ||
                                (data.kind() ==
                                     source_data_kind::
                                         type_definition &&
                                 !type_value.defined())) {

                                return compiled_project_image_result::
                                    invalid_state;
                            }

                            semantic_domain =
                                file_kind::header;

                            break;
                        }

                        case source_data_kind::
                                object: {
                            const auto identity =
                                identities.at_slot(
                                    data.slot());

                            const auto lineage =
                                identity &&
                                    identity.kind() ==
                                        identity_kind::object
                                ? G.find_object(
                                    identity)
                                : object_handle{};

                            object_entry object_value;

                            if (!lineage ||
                                !projection.remap(
                                    lineage) ||
                                !G.object(
                                    lineage,
                                    object_value)) {

                                return compiled_project_image_result::
                                    invalid_state;
                            }

                            semantic_domain =
                                object_value.internal_static()
                                ? file_kind::header
                                : file_kind::source;

                            break;
                        }

                        case source_data_kind::
                                link: {
                            const auto lineage =
                                G.link_at(
                                    static_cast<std::size_t>(
                                        data.slot() - 1));

                            const auto dense =
                                projection.remap(
                                    lineage);

                            if (!dense) {
                                return compiled_project_image_result::
                                    invalid_state;
                            }

                            data =
                                source_data_ref::link(
                                    dense);

                            semantic_domain =
                                file_kind::source;

                            break;
                        }
                        }

                        if (semantic_domain !=
                                domain ||
                            files.kind(
                                contribution.file) !=
                                semantic_domain) {

                            return compiled_project_image_result::
                                invalid_state;
                        }

                        auto* record =
                            contributions +
                            static_cast<std::size_t>(
                                contribution_cursor) *
                                8;

                        build_write_u32(
                            record,
                            contribution.file.value());

                        build_write_u32(
                            record + 4,
                            data.raw());

                        auto* physical =
                            file_values +
                            static_cast<std::size_t>(
                                contribution.file.value() -
                                1) *
                                20;

                        const auto physical_count =
                            build_read_u32(
                                physical + 16);

                        if (physical_count ==
                            (std::numeric_limits<
                                std::uint32_t>::max)()) {

                            return compiled_project_image_result::
                                failed;
                        }

                        build_write_u32(
                            physical + 16,
                            physical_count + 1);

                        ++contribution_cursor;
                    }
                }

                return compiled_project_image_result::
                    success;
            };

        for (const auto domain :
             {
                 file_kind::header,
                 file_kind::source,
             }) {

            const auto emitted =
                emit_domain(
                    domain);

            if (emitted !=
                compiled_project_image_result::
                    success) {

                return emitted;
            }
        }

        if (contribution_cursor !=
            count(
                compiled_project_section::
                    source_contributions)) {

            return compiled_project_image_result::
                invalid_state;
        }

        std::uint32_t file_cursor = 0;

        for (std::size_t index = 0;
             index < files.size();
             ++index) {

            auto* record =
                file_values +
                index * 20;

            const auto physical_count =
                build_read_u32(
                    record + 16);

            build_write_u32(
                record + 12,
                file_cursor);

            if (physical_count >
                (std::numeric_limits<
                    std::uint32_t>::max)() -
                    file_cursor) {

                return compiled_project_image_result::
                    failed;
            }

            file_cursor +=
                physical_count;
        }

        if (file_cursor !=
            contribution_cursor) {

            return compiled_project_image_result::
                invalid_state;
        }

        for (std::uint32_t index = 0;
             index <
                contribution_cursor;
             ++index) {

            const auto* contribution =
                contributions +
                static_cast<std::size_t>(
                    index) *
                    8;

            const auto file =
                build_read_u32(
                    contribution);

            if (file == 0 ||
                file >
                    files.size()) {

                return compiled_project_image_result::
                    invalid_state;
            }

            auto* physical =
                file_values +
                static_cast<std::size_t>(
                    file - 1) *
                    20;

            const auto position =
                build_read_u32(
                    physical + 12);

            if (position >=
                contribution_cursor) {

                return compiled_project_image_result::
                    invalid_state;
            }

            build_write_u32(
                file_indices +
                    static_cast<std::size_t>(
                        position) *
                        4,
                index);

            build_write_u32(
                physical + 12,
                position + 1);
        }

        file_cursor = 0;

        for (std::size_t index = 0;
             index < files.size();
             ++index) {

            auto* record =
                file_values +
                index * 20;

            const auto physical_count =
                build_read_u32(
                    record + 16);

            build_write_u32(
                record + 12,
                file_cursor);

            file_cursor +=
                physical_count;
        }
    }

    {
        const auto entries = G.constructor_defaults.entries();
        if (count(compiled_project_section::constructor_defaults) != entries.size()) {
            return compiled_project_image_result::invalid_state;
        }
        auto* data = section_data(compiled_project_section::constructor_defaults);
        for (const auto& value : entries) {
            build_write_u32(data, value.owner.value()); build_write_u32(data + 4, value.path.value());
            build_write_u32(data + 8, value.value.low); build_write_u32(data + 12, value.value.high);
            build_write_u32(data + 16, value.value.operand);
            build_write_u32(data + 20, static_cast<std::uint32_t>(value.value.kind));
            data += 24;
        }
    }
    // No whole-section payload checksum pass. Normal BUILD is being moved
    // to changed-record writes and must remain independent of Project size.

    std::memcpy(
        base,
        build_image_magic.data(),
        build_image_magic.size());

    build_write_u32(
        base + 8,
        compiled_project_format_version);

    build_write_u32(
        base + 12,
        build_endian_marker);

    build_write_u32(
        base + 16,
        compiled_project_header_size);

    build_write_u32(
        base + 20,
        compiled_project_directory_count);

    build_write_u32(
        base + 24,
        compiled_project_directory_entry_size);

    build_write_u32(
        base + 28,
        0);

    build_write_u64(
        base + 32,
        build_directory_offset);

    build_write_u64(
        base + 40,
        output.size());

    build_write_u64(
        base +
            build_header_string_count_offset,
        strings.size());

    build_write_u64(
        base +
            build_header_identity_count_offset,
        identities.size());

    build_write_u64(
        base +
            build_header_type_count_offset,
        projection.type_count());

    build_write_u64(
        base +
            build_header_object_count_offset,
        projection.object_count());

    build_write_u64(
        base +
            build_header_link_count_offset,
        projection.link_count());

    build_write_u64(
        base +
            build_header_assign_count_offset,
        assigns.size());

    for (std::size_t index = 0;
         index <
            compiled_project_directory_count;
         ++index) {

        const auto& section =
            layout[index];

        auto* record =
            base +
            build_directory_offset +
            index *
                compiled_project_directory_entry_size;

        build_write_u32(
            record,
            static_cast<std::uint32_t>(
                section.kind));

        build_write_u32(
            record + 4,
            section.record_size);

        build_write_u64(
            record + 8,
            section.offset);

        build_write_u64(
            record + 16,
            section.count);

        build_write_u64(
            record + 24,
            0);
    }

    build_write_u64(
        base +
            build_header_directory_crc_offset,
        persistence_crc64(
            std::span<const std::byte>{
                base +
                    build_directory_offset,
                build_directory_bytes}));

    std::array<
        std::byte,
        compiled_project_header_size>
        header{};

    std::memcpy(
        header.data(),
        base,
        header.size());

    build_write_u64(
        header.data() +
            build_header_crc_offset,
        0);

    build_write_u64(
        base +
            build_header_crc_offset,
        persistence_crc64(
            header));

    compiled_project_view validation;

    return validation.bind(
        output);
}

}
