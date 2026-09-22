#include "compiled_project.hpp"
#include "crc64_ecma.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace cw::server {
namespace {

constexpr std::array<std::byte, 8> image_magic{
    std::byte{'S'},
    std::byte{'E'},
    std::byte{'C'},
    std::byte{'M'},
    std::byte{'P'},
    std::byte{'V'},
    std::byte{'4'},
    std::byte{0},
};

constexpr std::uint32_t endian_marker =
    0x01020304u;

constexpr std::size_t directory_offset =
    compiled_project_header_size;

constexpr std::size_t directory_bytes =
    compiled_project_directory_count *
    compiled_project_directory_entry_size;

constexpr std::size_t first_section_offset =
    compiled_project_prefix_size;

constexpr std::size_t header_string_count_offset = 48;
constexpr std::size_t header_identity_count_offset = 56;
constexpr std::size_t header_type_count_offset = 64;
constexpr std::size_t header_object_count_offset = 72;
constexpr std::size_t header_link_count_offset = 80;
constexpr std::size_t header_assign_count_offset = 88;
constexpr std::size_t header_reserved_begin = 96;
constexpr std::size_t header_directory_crc_offset = 240;
constexpr std::size_t header_crc_offset = 248;

constexpr std::uint32_t string_core_size = 8;
constexpr std::uint32_t index_record_size = 8;
constexpr std::uint32_t identity_core_size = 12;
constexpr std::uint32_t type_record_size = 12;
constexpr std::uint32_t type_identity_size = 4;
constexpr std::uint32_t member_record_size = 12;
constexpr std::uint32_t construction_record_size = 16;
constexpr std::uint32_t derived_record_size = 16;
constexpr std::uint32_t object_record_size = 8;
constexpr std::uint32_t object_identity_size = 4;
constexpr std::uint32_t link_record_size = 16;
constexpr std::uint32_t graph_identity_record_size = 4;
constexpr std::uint32_t assign_record_size = 16;

[[nodiscard]] constexpr std::size_t section_index(
    compiled_project_section kind) noexcept {

    const auto raw =
        static_cast<std::uint32_t>(kind);

    return raw >= 1 &&
        raw <= compiled_project_directory_count
        ? static_cast<std::size_t>(
            raw - 1)
        : compiled_project_directory_count;
}

[[nodiscard]] constexpr bool align64(
    std::uint64_t value,
    std::uint64_t& output) noexcept {

    if (value >
        (std::numeric_limits<std::uint64_t>::max)() -
            63u) {

        return false;
    }

    output =
        (value + 63u) &
        ~std::uint64_t{63u};

    return true;
}

[[nodiscard]] bool add_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    if (left >
        (std::numeric_limits<std::uint64_t>::max)() -
            right) {

        return false;
    }

    output = left + right;
    return true;
}

[[nodiscard]] bool multiply_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    if (left != 0 &&
        right >
            (std::numeric_limits<std::uint64_t>::max)() /
                left) {

        return false;
    }

    output = left * right;
    return true;
}

void write_u16(
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

void write_u32(
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

void write_u64(
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

[[nodiscard]] std::uint16_t read_u16(
    const std::byte* source) noexcept {

    if constexpr (
        std::endian::native ==
        std::endian::little) {

        std::uint16_t value = 0;

        std::memcpy(
            &value,
            source,
            sizeof(value));

        return value;
    }

    return
        static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(
                source[0])) |
        static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(
                source[1]) << 8);
}

[[nodiscard]] std::uint32_t read_u32(
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

[[nodiscard]] std::uint64_t read_u64(
    const std::byte* source) noexcept {

    if constexpr (
        std::endian::native ==
        std::endian::little) {

        std::uint64_t value = 0;

        std::memcpy(
            &value,
            source,
            sizeof(value));

        return value;
    }

    std::uint64_t value = 0;

    for (std::uint32_t index = 0;
         index < 8;
         ++index) {

        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    source[index]))
            << (index * 8);
    }

    return value;
}

[[nodiscard]] bool zero_bytes(
    const std::byte* data,
    std::size_t count) noexcept {

    for (std::size_t index = 0;
         index < count;
         ++index) {

        if (data[index] !=
            std::byte{0}) {

            return false;
        }
    }

    return true;
}

[[nodiscard]] std::uint32_t string_hash(
    std::string_view value) noexcept {

    std::uint32_t hash = 2166136261u;

    for (const auto character : value) {
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

[[nodiscard]] constexpr std::uint64_t mix64(
    std::uint64_t value) noexcept {

    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;

    return value;
}

[[nodiscard]] std::uint64_t identity_hash(
    std::uint32_t parent,
    std::uint32_t name,
    std::uint32_t kind) noexcept {

    const auto value =
        static_cast<std::uint64_t>(
            parent) |
        (static_cast<std::uint64_t>(
             name) << 32);

    return mix64(
        value ^
        (static_cast<std::uint64_t>(
             kind) *
         0x9e3779b97f4a7c15ULL));
}

[[nodiscard]] std::uint32_t identity_fingerprint(
    std::uint64_t hash) noexcept {

    auto value =
        static_cast<std::uint32_t>(
            hash ^ (hash >> 32));

    return value == 0
        ? 1
        : value;
}

[[nodiscard]] std::uint64_t index_capacity(
    std::uint64_t live) noexcept {

    constexpr std::uint64_t minimum = 8;

    if (live >
        (std::numeric_limits<std::uint64_t>::max)() /
            2) {

        return 0;
    }

    const auto required =
        live * 2;

    std::uint64_t capacity =
        minimum;

    while (capacity < required) {
        if (capacity >
            (std::numeric_limits<std::uint64_t>::max)() /
                2) {

            return 0;
        }

        capacity *= 2;
    }

    return capacity;
}

[[nodiscard]] bool valid_record_kind(
    graph_record_kind kind) noexcept {

    return kind ==
            graph_record_kind::struct_type ||
        kind ==
            graph_record_kind::class_type ||
        kind ==
            graph_record_kind::union_type;
}

[[nodiscard]] bool valid_member_access(
    graph_member_access access) noexcept {

    return access ==
            graph_member_access::public_access ||
        access ==
            graph_member_access::protected_access ||
        access ==
            graph_member_access::private_access;
}

[[nodiscard]] bool valid_derived_kind(
    derived_type_kind kind) noexcept {

    return kind >=
            derived_type_kind::const_qualified &&
        kind <=
            derived_type_kind::unbounded_array;
}

[[nodiscard]] std::uint32_t expected_record_size(
    compiled_project_section kind) noexcept {

    switch (kind) {
    case compiled_project_section::string_core:
        return string_core_size;

    case compiled_project_section::string_index:
        return index_record_size;

    case compiled_project_section::string_bytes:
        return 1;

    case compiled_project_section::identity_core:
        return identity_core_size;

    case compiled_project_section::identity_index:
        return index_record_size;

    case compiled_project_section::types:
        return type_record_size;

    case compiled_project_section::type_identities:
        return type_identity_size;

    case compiled_project_section::members:
        return member_record_size;

    case compiled_project_section::member_construction:
        return construction_record_size;

    case compiled_project_section::derived_types:
        return derived_record_size;

    case compiled_project_section::objects:
        return object_record_size;

    case compiled_project_section::object_identities:
        return object_identity_size;

    case compiled_project_section::links:
        return link_record_size;

    case compiled_project_section::graph_identity_index:
        return graph_identity_record_size;

    case compiled_project_section::assign_records:
        return assign_record_size;

    case compiled_project_section::assign_bytes:
        return 1;
    }

    return 0;
}

[[nodiscard]] std::uint32_t encode_graph_location(
    std::uint32_t kind,
    std::uint32_t slot) noexcept {

    if (kind == 0 ||
        kind > 2 ||
        slot == 0 ||
        slot > type_ref::maximum_payload) {

        return 0;
    }

    return
        (kind << 30) |
        slot;
}

[[nodiscard]] std::uint32_t graph_location_kind(
    std::uint32_t value) noexcept {

    return value >> 30;
}

[[nodiscard]] std::uint32_t graph_location_slot(
    std::uint32_t value) noexcept {

    return value &
        type_ref::maximum_payload;
}

}

const compiled_project_view::section_view&
compiled_project_view::section(
    compiled_project_section kind) const noexcept {

    static const section_view empty{};

    const auto index =
        section_index(kind);

    return index <
        compiled_project_directory_count
        ? sections[index]
        : empty;
}

std::span<const std::byte>
compiled_project_view::section_bytes(
    compiled_project_section kind) const noexcept {

    const auto& value =
        section(kind);

    std::uint64_t byte_count = 0;

    if (value.data == nullptr ||
        !multiply_u64(
            value.count,
            value.record_size,
            byte_count) ||
        byte_count >
            (std::numeric_limits<std::size_t>::max)()) {

        return {};
    }

    return {
        value.data,
        static_cast<std::size_t>(
            byte_count),
    };
}

void compiled_project_view::reset() noexcept {

    bytes = {};

    for (auto& value : sections) {
        value = {};
    }

    string_count_value = 0;
    identity_count_value = 0;
    type_count_value = 0;
    object_count_value = 0;
    link_count_value = 0;
    assign_count_value = 0;
}

compiled_project_image_result
compiled_project_view::bind(
    std::span<const std::byte> image) noexcept {

    reset();

    if (image.size() <
        first_section_offset) {

        return compiled_project_image_result::
            invalid_image;
    }

    if (!std::equal(
            image_magic.begin(),
            image_magic.end(),
            image.begin())) {

        return compiled_project_image_result::
            invalid_image;
    }

    if (read_u32(
            image.data() + 8) !=
            compiled_project_format_version ||
        read_u32(
            image.data() + 12) !=
            endian_marker ||
        read_u32(
            image.data() + 16) !=
            compiled_project_header_size ||
        read_u32(
            image.data() + 20) !=
            compiled_project_directory_count ||
        read_u32(
            image.data() + 24) !=
            compiled_project_directory_entry_size ||
        read_u32(
            image.data() + 28) != 0 ||
        read_u64(
            image.data() + 32) !=
            directory_offset ||
        read_u64(
            image.data() + 40) !=
            image.size()) {

        return compiled_project_image_result::
            invalid_image;
    }

    if (!zero_bytes(
            image.data() +
                header_reserved_begin,
            header_directory_crc_offset -
                header_reserved_begin)) {

        return compiled_project_image_result::
            invalid_image;
    }

    std::array<
        std::byte,
        compiled_project_header_size>
        header{};

    std::memcpy(
        header.data(),
        image.data(),
        header.size());

    const auto stored_header_crc =
        read_u64(
            header.data() +
                header_crc_offset);

    write_u64(
        header.data() +
            header_crc_offset,
        0);

    if (persistence_crc64(
            header) !=
        stored_header_crc) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto directory_span =
        image.subspan(
            directory_offset,
            directory_bytes);

    if (persistence_crc64(
            directory_span) !=
        read_u64(
            image.data() +
                header_directory_crc_offset)) {

        return compiled_project_image_result::
            invalid_image;
    }

    section_view candidate[
        compiled_project_directory_count]{};

    std::uint64_t previous_end =
        first_section_offset;

    for (std::size_t index = 0;
         index <
            compiled_project_directory_count;
         ++index) {

        const auto* entry =
            image.data() +
            directory_offset +
            index *
                compiled_project_directory_entry_size;

        const auto raw_kind =
            read_u32(entry);

        const auto kind =
            static_cast<
                compiled_project_section>(
                    raw_kind);

        const auto record_size =
            read_u32(
                entry + 4);

        const auto section_offset =
            read_u64(
                entry + 8);

        const auto count =
            read_u64(
                entry + 16);

        const auto section_crc =
            read_u64(
                entry + 24);

        std::uint64_t aligned_offset = 0;

        if (!align64(
                previous_end,
                aligned_offset) ||
            raw_kind != index + 1 ||
            record_size !=
                expected_record_size(kind) ||
            section_offset !=
                aligned_offset ||
            (section_offset & 63u) != 0 ||
            section_offset >
                image.size()) {

            return compiled_project_image_result::
                invalid_image;
        }

        if (section_offset >
                previous_end &&
            !zero_bytes(
                image.data() +
                    static_cast<std::size_t>(
                        previous_end),
                static_cast<std::size_t>(
                    section_offset -
                    previous_end))) {

            return compiled_project_image_result::
                invalid_image;
        }

        std::uint64_t byte_count = 0;
        std::uint64_t end = 0;

        if (!multiply_u64(
                count,
                record_size,
                byte_count) ||
            !add_u64(
                section_offset,
                byte_count,
                end) ||
            end >
                image.size()) {

            return compiled_project_image_result::
                invalid_image;
        }

        candidate[index] = {
            image.data() +
                static_cast<std::size_t>(
                    section_offset),
            count,
            record_size,
            section_crc,
        };

        previous_end = end;
    }

    if (previous_end !=
        image.size()) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto string_count =
        read_u64(
            image.data() +
                header_string_count_offset);

    const auto identity_count =
        read_u64(
            image.data() +
                header_identity_count_offset);

    const auto type_count =
        read_u64(
            image.data() +
                header_type_count_offset);

    const auto object_count =
        read_u64(
            image.data() +
                header_object_count_offset);

    const auto link_count =
        read_u64(
            image.data() +
                header_link_count_offset);

    const auto assign_count =
        read_u64(
            image.data() +
                header_assign_count_offset);

    if (string_count >
            (std::numeric_limits<std::size_t>::max)() ||
        identity_count >
            (std::numeric_limits<std::size_t>::max)() ||
        type_count >
            (std::numeric_limits<std::size_t>::max)() ||
        object_count >
            (std::numeric_limits<std::size_t>::max)() ||
        link_count >
            (std::numeric_limits<std::size_t>::max)() ||
        assign_count >
            (std::numeric_limits<std::size_t>::max)()) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto& string_core =
        candidate[
            section_index(
                compiled_project_section::
                    string_core)];

    const auto& string_index =
        candidate[
            section_index(
                compiled_project_section::
                    string_index)];

    const auto& string_bytes =
        candidate[
            section_index(
                compiled_project_section::
                    string_bytes)];

    const auto& identity_core =
        candidate[
            section_index(
                compiled_project_section::
                    identity_core)];

    const auto& identity_index =
        candidate[
            section_index(
                compiled_project_section::
                    identity_index)];

    const auto& types =
        candidate[
            section_index(
                compiled_project_section::
                    types)];

    const auto& type_identities =
        candidate[
            section_index(
                compiled_project_section::
                    type_identities)];

    const auto& members =
        candidate[
            section_index(
                compiled_project_section::
                    members)];

    const auto& construction =
        candidate[
            section_index(
                compiled_project_section::
                    member_construction)];

    const auto& derived_types =
        candidate[
            section_index(
                compiled_project_section::
                    derived_types)];

    const auto& objects =
        candidate[
            section_index(
                compiled_project_section::
                    objects)];

    const auto& object_identities =
        candidate[
            section_index(
                compiled_project_section::
                    object_identities)];

    const auto& links =
        candidate[
            section_index(
                compiled_project_section::
                    links)];

    const auto& graph_identity =
        candidate[
            section_index(
                compiled_project_section::
                    graph_identity_index)];

    const auto& assign_records =
        candidate[
            section_index(
                compiled_project_section::
                    assign_records)];

    const auto& assign_bytes =
        candidate[
            section_index(
                compiled_project_section::
                    assign_bytes)];

    const auto expected_string_index_count =
        index_capacity(
            string_count);

    const auto expected_identity_index_count =
        index_capacity(
            identity_count > 0
                ? identity_count - 1
                : 0);

    if (string_count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        identity_count == 0 ||
        identity_count >
            identity_ref::maximum_slot ||
        type_count >
            type_handle::maximum_slot ||
        object_count >
            object_handle::maximum_slot ||
        link_count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        assign_count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        string_bytes.count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        members.count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        derived_types.count >
            type_ref::maximum_payload ||
        assign_bytes.count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        expected_string_index_count == 0 ||
        expected_identity_index_count == 0 ||
        string_core.count !=
            string_count ||
        string_index.count !=
            expected_string_index_count ||
        identity_core.count !=
            identity_count ||
        identity_index.count !=
            expected_identity_index_count ||
        types.count !=
            type_count ||
        type_identities.count !=
            type_count ||
        members.count !=
            construction.count ||
        objects.count !=
            object_count ||
        object_identities.count !=
            object_count ||
        links.count !=
            link_count ||
        graph_identity.count !=
            identity_count + 1 ||
        assign_records.count !=
            assign_count) {

        return compiled_project_image_result::
            invalid_image;
    }

    for (std::size_t index = 0;
         index <
            compiled_project_directory_count;
         ++index) {

        sections[index] =
            candidate[index];
    }

    bytes = image;

    string_count_value =
        static_cast<std::size_t>(
            string_count);

    identity_count_value =
        static_cast<std::size_t>(
            identity_count);

    type_count_value =
        static_cast<std::size_t>(
            type_count);

    object_count_value =
        static_cast<std::size_t>(
            object_count);

    link_count_value =
        static_cast<std::size_t>(
            link_count);

    assign_count_value =
        static_cast<std::size_t>(
            assign_count);

    return compiled_project_image_result::
        success;
}

string_id compiled_project_view::string_from_raw(
    std::uint32_t value) const noexcept {

    return value != 0 &&
        value <= string_count_value
        ? string_id{value}
        : string_id{};
}

identity_ref
compiled_project_view::identity_from_raw(
    std::uint32_t value) const noexcept {

    const auto slot =
        value &
        identity_ref::slot_mask;

    if (slot == 0 ||
        slot > identity_count_value) {

        return {};
    }

    const auto& values =
        section(
            compiled_project_section::
                identity_core);

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            slot - 1) *
            identity_core_size;

    return read_u32(record) ==
        value
        ? identity_ref{value}
        : identity_ref{};
}

type_handle compiled_project_view::type_from_raw(
    std::uint32_t value) const noexcept {

    return value != 0 &&
        value <= type_count_value
        ? type_handle{value}
        : type_handle{};
}

object_handle
compiled_project_view::object_from_raw(
    std::uint32_t value) const noexcept {

    return value != 0 &&
        value <= object_count_value
        ? object_handle{value}
        : object_handle{};
}

link_handle compiled_project_view::link_from_raw(
    std::uint32_t value) const noexcept {

    return value != 0 &&
        value <= link_count_value
        ? link_handle{value}
        : link_handle{};
}

member_index compiled_project_view::member_from_raw(
    std::uint32_t value) const noexcept {

    return member_index{value};
}

type_ref compiled_project_view::type_ref_from_raw(
    std::uint32_t value) const noexcept {

    const auto kind =
        static_cast<type_ref_kind>(
            value >> 30);

    const auto payload =
        value &
        type_ref::maximum_payload;

    if (payload == 0) {
        return {};
    }

    switch (kind) {
    case type_ref_kind::intrinsic:
        if (payload >
            static_cast<std::uint32_t>(
                intrinsic_type::nullptr_type)) {
            return {};
        }
        break;

    case type_ref_kind::named:
        if (payload >
            type_count_value) {
            return {};
        }
        break;

    case type_ref_kind::derived:
        if (payload >
            section(
                compiled_project_section::
                    derived_types).count) {
            return {};
        }
        break;

    case type_ref_kind::invalid:
        return {};
    }

    return type_ref{value};
}

std::string_view compiled_project_view::string(
    string_id id) const noexcept {

    if (!id ||
        id.value() >
            string_count_value) {

        return {};
    }

    const auto& core =
        section(
            compiled_project_section::
                string_core);

    const auto& data =
        section(
            compiled_project_section::
                string_bytes);

    const auto* record =
        core.data +
        static_cast<std::size_t>(
            id.value() - 1) *
            string_core_size;

    const auto offset =
        read_u32(record);

    const auto length =
        read_u32(
            record + 4);

    if (length == 0 ||
        offset >
            data.count ||
        length >
            data.count -
                offset) {

        return {};
    }

    return {
        reinterpret_cast<const char*>(
            data.data + offset),
        length,
    };
}

string_id compiled_project_view::find_string(
    std::string_view value) const noexcept {

    if (value.empty()) {
        return {};
    }

    const auto& index =
        section(
            compiled_project_section::
                string_index);

    if (index.count == 0 ||
        (index.count &
            (index.count - 1)) != 0) {

        return {};
    }

    const auto hash =
        string_hash(value);

    const auto mask =
        index.count - 1;

    auto position =
        static_cast<std::uint64_t>(
            hash) &
        mask;

    for (std::uint64_t probe = 0;
         probe < index.count;
         ++probe) {

        const auto* slot =
            index.data +
            static_cast<std::size_t>(
                position) *
                index_record_size;

        const auto fingerprint =
            read_u32(slot);

        const auto raw =
            read_u32(
                slot + 4);

        if (raw == 0) {
            return {};
        }

        if (fingerprint == hash) {
            const auto id =
                string_from_raw(raw);

            if (id &&
                string(id) == value) {

                return id;
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

identity_ref
compiled_project_view::identity_root() const noexcept {

    return identity_from_raw(1);
}

bool compiled_project_view::identity_valid(
    identity_ref identity) const noexcept {

    return static_cast<bool>(
        identity_from_raw(
            identity.value()));
}

identity_ref
compiled_project_view::identity_parent(
    identity_ref identity) const noexcept {

    const auto valid =
        identity_from_raw(
            identity.value());

    if (!valid ||
        valid.slot() == 1) {

        return {};
    }

    const auto& core =
        section(
            compiled_project_section::
                identity_core);

    const auto* record =
        core.data +
        static_cast<std::size_t>(
            valid.slot() - 1) *
            identity_core_size;

    return identity_from_raw(
        read_u32(
            record + 4));
}

string_id compiled_project_view::identity_name(
    identity_ref identity) const noexcept {

    const auto valid =
        identity_from_raw(
            identity.value());

    if (!valid ||
        valid.slot() == 1) {

        return {};
    }

    const auto& core =
        section(
            compiled_project_section::
                identity_core);

    const auto* record =
        core.data +
        static_cast<std::size_t>(
            valid.slot() - 1) *
            identity_core_size;

    return string_from_raw(
        read_u32(
            record + 8));
}

identity_ref compiled_project_view::find_identity(
    identity_ref parent,
    string_id name,
    identity_kind kind) const noexcept {

    if (!identity_valid(parent) ||
        !string_from_raw(
            name.value()) ||
        kind ==
            identity_kind::root) {

        return {};
    }

    const auto& index =
        section(
            compiled_project_section::
                identity_index);

    if (index.count == 0 ||
        (index.count &
            (index.count - 1)) != 0) {

        return {};
    }

    const auto hash =
        identity_hash(
            parent.value(),
            name.value(),
            static_cast<std::uint32_t>(
                kind));

    const auto fingerprint =
        identity_fingerprint(hash);

    const auto mask =
        index.count - 1;

    auto position =
        hash &
        mask;

    for (std::uint64_t probe = 0;
         probe < index.count;
         ++probe) {

        const auto* slot =
            index.data +
            static_cast<std::size_t>(
                position) *
                index_record_size;

        const auto slot_fingerprint =
            read_u32(slot);

        const auto raw =
            read_u32(
                slot + 4);

        if (raw == 0) {
            return {};
        }

        if (slot_fingerprint ==
            fingerprint) {

            const auto identity =
                identity_from_raw(raw);

            if (identity &&
                identity.kind() == kind &&
                identity_parent(identity) ==
                    parent &&
                identity_name(identity) ==
                    name) {

                return identity;
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

type_handle compiled_project_view::type_at(
    std::size_t index) const noexcept {

    return index <
        type_count_value
        ? type_handle{
            static_cast<std::uint32_t>(
                index + 1)}
        : type_handle{};
}

bool compiled_project_view::type(
    type_handle handle,
    type_entry& output) const noexcept {

    output = {};

    if (!handle ||
        handle.value() >
            type_count_value) {

        return false;
    }

    const auto& values =
        section(
            compiled_project_section::
                types);

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            handle.value() - 1) *
            type_record_size;

    output.members.begin =
        read_u32(record);

    output.members.count =
        read_u32(
            record + 4);

    output.kind =
        static_cast<graph_type_kind>(
            std::to_integer<std::uint8_t>(
                record[8]));

    output.record_kind =
        static_cast<graph_record_kind>(
            std::to_integer<std::uint8_t>(
                record[9]));

    output.flags =
        read_u16(
            record + 10);

    return true;
}

identity_ref compiled_project_view::identity(
    type_handle handle) const noexcept {

    if (!handle ||
        handle.value() >
            type_count_value) {

        return {};
    }

    const auto& values =
        section(
            compiled_project_section::
                type_identities);

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            handle.value() - 1) *
            type_identity_size;

    return identity_from_raw(
        read_u32(record));
}

type_handle compiled_project_view::find_type(
    identity_ref identity_value) const noexcept {

    if (!identity_valid(
            identity_value) ||
        identity_value.slot() >
            identity_count_value) {

        return {};
    }

    const auto& values =
        section(
            compiled_project_section::
                graph_identity_index);

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            identity_value.slot()) *
            graph_identity_record_size;

    const auto location =
        read_u32(record);

    if (graph_location_kind(
            location) != 1) {

        return {};
    }

    return type_from_raw(
        graph_location_slot(
            location));
}

bool compiled_project_view::member(
    type_handle type_value,
    member_index member_value,
    member_record& output) const noexcept {

    output = {};

    type_entry type_record;

    if (!type(
            type_value,
            type_record) ||
        !type_record.defined() ||
        !member_value ||
        member_value.value() >=
            type_record.members.count) {

        return false;
    }

    const auto global =
        static_cast<std::uint64_t>(
            type_record.members.begin) +
        member_value.value();

    const auto& values =
        section(
            compiled_project_section::
                members);

    if (global >=
        values.count) {

        return false;
    }

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            global) *
            member_record_size;

    output.name =
        string_from_raw(
            read_u32(record));

    output.type =
        type_ref_from_raw(
            read_u32(
                record + 4));

    output.access =
        static_cast<graph_member_access>(
            std::to_integer<std::uint8_t>(
                record[8]));

    return output.name &&
        output.type &&
        zero_bytes(
            record + 9,
            3);
}

bool compiled_project_view::construction(
    type_handle type_value,
    member_index member_value,
    construction_value& output) const noexcept {

    output = {};

    type_entry type_record;

    if (!type(
            type_value,
            type_record) ||
        !type_record.defined() ||
        !member_value ||
        member_value.value() >=
            type_record.members.count) {

        return false;
    }

    const auto global =
        static_cast<std::uint64_t>(
            type_record.members.begin) +
        member_value.value();

    const auto& values =
        section(
            compiled_project_section::
                member_construction);

    if (global >=
        values.count) {

        return false;
    }

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            global) *
            construction_record_size;

    output.low =
        read_u32(record);

    output.high =
        read_u32(
            record + 4);

    output.operand =
        read_u32(
            record + 8);

    output.kind =
        static_cast<construction_kind>(
            read_u32(
                record + 12));

    return valid_construction(output);
}

member_index compiled_project_view::find_member(
    type_handle type_value,
    string_id name) const noexcept {

    if (!string_from_raw(
            name.value())) {

        return {};
    }

    type_entry type_record;

    if (!type(
            type_value,
            type_record) ||
        !type_record.defined()) {

        return {};
    }

    for (std::uint32_t index = 0;
         index <
            type_record.members.count;
         ++index) {

        member_record value;

        const auto member_value =
            member_from_raw(index);

        if (member(
                type_value,
                member_value,
                value) &&
            value.name == name) {

            return member_value;
        }
    }

    return {};
}

bool compiled_project_view::derived(
    type_ref type,
    derived_type_record& output) const noexcept {

    output = {};

    if (!type ||
        type.kind() !=
            type_ref_kind::derived) {

        return false;
    }

    const auto& values =
        section(
            compiled_project_section::
                derived_types);

    if (type.payload() == 0 ||
        type.payload() >
            values.count) {

        return false;
    }

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            type.payload() - 1) *
            derived_record_size;

    output.payload =
        read_u64(record);

    output.child =
        type_ref_from_raw(
            read_u32(
                record + 8));

    output.kind =
        static_cast<derived_type_kind>(
            std::to_integer<std::uint8_t>(
                record[12]));

    return output.child &&
        valid_derived_kind(
            output.kind) &&
        zero_bytes(
            record + 13,
            3);
}

object_handle compiled_project_view::object_at(
    std::size_t index) const noexcept {

    return index <
        object_count_value
        ? object_handle{
            static_cast<std::uint32_t>(
                index + 1)}
        : object_handle{};
}

bool compiled_project_view::object(
    object_handle handle,
    object_entry& output) const noexcept {

    output = {};

    if (!handle ||
        handle.value() >
            object_count_value) {

        return false;
    }

    const auto& values =
        section(
            compiled_project_section::
                objects);

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            handle.value() - 1) *
            object_record_size;

    output.type =
        type_ref_from_raw(
            read_u32(record));

    output.flags =
        read_u32(
            record + 4);

    return static_cast<bool>(
        output.type);
}

identity_ref compiled_project_view::identity(
    object_handle handle) const noexcept {

    if (!handle ||
        handle.value() >
            object_count_value) {

        return {};
    }

    const auto& values =
        section(
            compiled_project_section::
                object_identities);

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            handle.value() - 1) *
            object_identity_size;

    return identity_from_raw(
        read_u32(record));
}

object_handle compiled_project_view::find_object(
    identity_ref identity_value) const noexcept {

    if (!identity_valid(
            identity_value) ||
        identity_value.slot() >
            identity_count_value) {

        return {};
    }

    const auto& values =
        section(
            compiled_project_section::
                graph_identity_index);

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            identity_value.slot()) *
            graph_identity_record_size;

    const auto location =
        read_u32(record);

    if (graph_location_kind(
            location) != 2) {

        return {};
    }

    return object_from_raw(
        graph_location_slot(
            location));
}

bool compiled_project_view::link(
    link_handle handle,
    link_record& output) const noexcept {

    output = {};

    if (!handle ||
        handle.value() >
            link_count_value) {

        return false;
    }

    const auto& values =
        section(
            compiled_project_section::
                links);

    const auto* record =
        values.data +
        static_cast<std::size_t>(
            handle.value() - 1) *
            link_record_size;

    output.source.object =
        object_from_raw(
            read_u32(record));

    output.source.member =
        member_from_raw(
            read_u32(
                record + 4));

    output.target.object =
        object_from_raw(
            read_u32(
                record + 8));

    output.target.member =
        member_from_raw(
            read_u32(
                record + 12));

    return output.source.object &&
        output.source.member &&
        output.target.object &&
        output.target.member;
}

bool compiled_project_view::assign(
    std::size_t index,
    std::string_view& source,
    std::string_view& target) const noexcept {

    source = {};
    target = {};

    if (index >=
        assign_count_value) {

        return false;
    }

    const auto& records =
        section(
            compiled_project_section::
                assign_records);

    const auto& data =
        section(
            compiled_project_section::
                assign_bytes);

    const auto* record =
        records.data +
        index *
            assign_record_size;

    const auto source_offset =
        read_u32(record);

    const auto source_length =
        read_u32(
            record + 4);

    const auto target_offset =
        read_u32(
            record + 8);

    const auto target_length =
        read_u32(
            record + 12);

    if (source_length == 0 ||
        target_length == 0 ||
        source_offset >
            data.count ||
        source_length >
            data.count -
                source_offset ||
        target_offset >
            data.count ||
        target_length >
            data.count -
                target_offset) {

        return false;
    }

    source = {
        reinterpret_cast<const char*>(
            data.data +
            source_offset),
        source_length,
    };

    target = {
        reinterpret_cast<const char*>(
            data.data +
            target_offset),
        target_length,
    };

    return true;
}

compiled_project_image_result
compiled_project_view::verify_contents() const noexcept {

    if (!valid()) {
        return compiled_project_image_result::
            invalid_state;
    }

    for (const auto& value : sections) {
        std::uint64_t byte_count = 0;

        if (!multiply_u64(
                value.count,
                value.record_size,
                byte_count) ||
            byte_count >
                (std::numeric_limits<std::size_t>::max)()) {

            return compiled_project_image_result::
                invalid_image;
        }

        if (persistence_crc64(
                std::span<const std::byte>{
                    value.data,
                    static_cast<std::size_t>(
                        byte_count)}) !=
            value.crc64) {

            return compiled_project_image_result::
                invalid_image;
        }
    }

    // Strings are dense in V4. Numeric string_id slots are therefore preserved
    // exactly without a remap table.
    const auto& string_core =
        section(
            compiled_project_section::
                string_core);

    const auto& string_bytes =
        section(
            compiled_project_section::
                string_bytes);

    std::uint64_t expected_string_offset = 0;

    for (std::size_t index = 0;
         index <
            string_count_value;
         ++index) {

        const auto* record =
            string_core.data +
            index *
                string_core_size;

        const auto offset =
            read_u32(record);

        const auto length =
            read_u32(
                record + 4);

        if (length == 0 ||
            offset !=
                expected_string_offset ||
            length >
                string_bytes.count -
                    expected_string_offset) {

            return compiled_project_image_result::
                invalid_image;
        }

        const string_id id{
            static_cast<std::uint32_t>(
                index + 1)};

        const auto value =
            string(id);

        if (value.empty() ||
            find_string(value) !=
                id) {

            return compiled_project_image_result::
                invalid_image;
        }

        expected_string_offset +=
            length;
    }

    if (expected_string_offset !=
        string_bytes.count) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto& string_index =
        section(
            compiled_project_section::
                string_index);

    std::size_t indexed_strings = 0;

    for (std::uint64_t index = 0;
         index <
            string_index.count;
         ++index) {

        const auto* slot =
            string_index.data +
            static_cast<std::size_t>(
                index) *
                index_record_size;

        const auto fingerprint =
            read_u32(slot);

        const auto raw =
            read_u32(
                slot + 4);

        if (raw == 0) {
            if (fingerprint != 0) {
                return compiled_project_image_result::
                    invalid_image;
            }

            continue;
        }

        const auto id =
            string_from_raw(raw);

        const auto value =
            string(id);

        if (!id ||
            value.empty() ||
            fingerprint !=
                string_hash(value) ||
            find_string(value) !=
                id) {

            return compiled_project_image_result::
                invalid_image;
        }

        ++indexed_strings;
    }

    if (indexed_strings !=
        string_count_value) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto root =
        identity_root();

    if (!root ||
        root.slot() != 1 ||
        root.kind() !=
            identity_kind::root ||
        identity_parent(root) ||
        identity_name(root)) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto& identity_core =
        section(
            compiled_project_section::
                identity_core);

    for (std::uint32_t slot = 2;
         slot <= identity_count_value;
         ++slot) {

        const auto* record =
            identity_core.data +
            static_cast<std::size_t>(
                slot - 1) *
                identity_core_size;

        const auto raw =
            read_u32(record);

        const auto parent_raw =
            read_u32(
                record + 4);

        const auto name_raw =
            read_u32(
                record + 8);

        const auto identity =
            identity_from_raw(raw);

        const auto parent =
            identity_from_raw(
                parent_raw);

        const auto name =
            string_from_raw(
                name_raw);

        if (!identity ||
            identity.slot() != slot ||
            identity.kind() ==
                identity_kind::root ||
            !parent ||
            parent.slot() >= slot ||
            !name ||
            find_identity(
                parent,
                name,
                identity.kind()) !=
                    identity) {

            return compiled_project_image_result::
                invalid_image;
        }
    }

    const auto& identity_index =
        section(
            compiled_project_section::
                identity_index);

    std::size_t indexed_identities = 0;

    for (std::uint64_t index = 0;
         index <
            identity_index.count;
         ++index) {

        const auto* slot =
            identity_index.data +
            static_cast<std::size_t>(
                index) *
                index_record_size;

        const auto fingerprint =
            read_u32(slot);

        const auto raw =
            read_u32(
                slot + 4);

        if (raw == 0) {
            if (fingerprint != 0) {
                return compiled_project_image_result::
                    invalid_image;
            }

            continue;
        }

        const auto identity =
            identity_from_raw(raw);

        if (!identity ||
            identity.kind() ==
                identity_kind::root) {

            return compiled_project_image_result::
                invalid_image;
        }

        const auto parent =
            identity_parent(identity);

        const auto name =
            identity_name(identity);

        const auto hash =
            identity_hash(
                parent.value(),
                name.value(),
                static_cast<std::uint32_t>(
                    identity.kind()));

        if (!parent ||
            !name ||
            fingerprint !=
                identity_fingerprint(hash) ||
            find_identity(
                parent,
                name,
                identity.kind()) !=
                    identity) {

            return compiled_project_image_result::
                invalid_image;
        }

        ++indexed_identities;
    }

    if (indexed_identities + 1 !=
        identity_count_value) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto& members =
        section(
            compiled_project_section::
                members);

    const auto& construction_values =
        section(
            compiled_project_section::
                member_construction);

    // Cold audit marks ownership once so overlap/orphan validation stays O(n).
    std::vector<std::uint8_t> member_owners;

    try {
        member_owners.assign(
            static_cast<std::size_t>(
                members.count),
            0);
    }
    catch (...) {
        return compiled_project_image_result::
            failed;
    }

    for (std::size_t index = 0;
         index <
            type_count_value;
         ++index) {

        const auto handle =
            type_at(index);

        type_entry type_value;

        const auto identity_value =
            identity(handle);

        if (!type(
                handle,
                type_value) ||
            type_value.kind !=
                graph_type_kind::record ||
            !valid_record_kind(
                type_value.record_kind) ||
            (type_value.flags &
                ~graph_type_defined) != 0 ||
            !identity_value ||
            identity_value.kind() !=
                identity_kind::type ||
            find_type(identity_value) !=
                handle) {

            return compiled_project_image_result::
                invalid_image;
        }

        if (!type_value.defined()) {
            if (type_value.members.begin != 0 ||
                type_value.members.count != 0) {

                return compiled_project_image_result::
                    invalid_image;
            }

            continue;
        }

        const auto begin =
            static_cast<std::uint64_t>(
                type_value.members.begin);

        const auto count =
            static_cast<std::uint64_t>(
                type_value.members.count);

        if (begin >
                members.count ||
            count >
                members.count -
                    begin) {

            return compiled_project_image_result::
                invalid_image;
        }

        for (std::uint32_t local = 0;
             local <
                type_value.members.count;
             ++local) {

            const auto global =
                begin +
                local;

            auto& owner =
                member_owners[
                    static_cast<std::size_t>(
                        global)];

            if (owner != 0) {
                return compiled_project_image_result::
                    invalid_image;
            }

            owner = 1;

            const auto member_value =
                member_from_raw(local);

            member_record member_record_value;
            construction_value construction_value_value;

            if (!member(
                    handle,
                    member_value,
                    member_record_value) ||
                !construction(
                    handle,
                    member_value,
                    construction_value_value) ||
                !valid_member_access(
                    member_record_value.access) ||
                !member_record_value.name ||
                !member_record_value.type ||
                (construction_value_value.kind ==
                        construction_kind::
                            member_binding &&
                 construction_value_value.operand >
                    type_value.members.count)) {

                return compiled_project_image_result::
                    invalid_image;
            }
        }
    }

    for (const auto owner :
         member_owners) {

        if (owner == 0) {
            return compiled_project_image_result::
                invalid_image;
        }
    }

    if (members.count !=
        construction_values.count) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto& derived_values =
        section(
            compiled_project_section::
                derived_types);

    for (std::uint32_t slot = 1;
         slot <= derived_values.count;
         ++slot) {

        const type_ref type{
            (static_cast<std::uint32_t>(
                 type_ref_kind::derived) << 30) |
            slot};

        derived_type_record value;

        if (!derived(
                type,
                value) ||
            !valid_derived_kind(
                value.kind)) {

            return compiled_project_image_result::
                invalid_image;
        }

        if (value.child.kind() ==
                type_ref_kind::derived &&
            value.child.payload() >= slot) {

            return compiled_project_image_result::
                invalid_image;
        }
    }

    for (std::size_t index = 0;
         index <
            object_count_value;
         ++index) {

        const auto handle =
            object_at(index);

        object_entry value;

        const auto identity_value =
            identity(handle);

        if (!object(
                handle,
                value) ||
            !value.type ||
            (value.flags &
                ~graph_object_non_default_initializer) != 0 ||
            !identity_value ||
            identity_value.kind() !=
                identity_kind::object ||
            find_object(identity_value) !=
                handle) {

            return compiled_project_image_result::
                invalid_image;
        }
    }

    const auto& graph_identity =
        section(
            compiled_project_section::
                graph_identity_index);

    for (std::size_t slot = 0;
         slot <=
            identity_count_value;
         ++slot) {

        const auto location =
            read_u32(
                graph_identity.data +
                slot *
                    graph_identity_record_size);

        if (slot == 0) {
            if (location != 0) {
                return compiled_project_image_result::
                    invalid_image;
            }

            continue;
        }

        const auto* identity_record =
            identity_core.data +
            (slot - 1) *
                identity_core_size;

        const auto identity_value =
            identity_from_raw(
                read_u32(
                    identity_record));

        if (!identity_value ||
            identity_value.slot() !=
                slot) {

            return compiled_project_image_result::
                invalid_image;
        }

        switch (identity_value.kind()) {
        case identity_kind::root:
        case identity_kind::namespace_scope:
            if (location != 0) {
                return compiled_project_image_result::
                    invalid_image;
            }
            break;

        case identity_kind::type: {
            if (graph_location_kind(
                    location) != 1) {

                return compiled_project_image_result::
                    invalid_image;
            }

            const auto handle =
                type_from_raw(
                    graph_location_slot(
                        location));

            if (!handle ||
                identity(handle) !=
                    identity_value) {

                return compiled_project_image_result::
                    invalid_image;
            }
            break;
        }

        case identity_kind::object: {
            if (graph_location_kind(
                    location) != 2) {

                return compiled_project_image_result::
                    invalid_image;
            }

            const auto handle =
                object_from_raw(
                    graph_location_slot(
                        location));

            if (!handle ||
                identity(handle) !=
                    identity_value) {

                return compiled_project_image_result::
                    invalid_image;
            }
            break;
        }
        }
    }

    // Cold audit keeps target uniqueness linear without a persisted link index.
    std::vector<std::uint64_t> link_targets;

    if (link_count_value != 0) {
        const auto capacity =
            index_capacity(
                link_count_value);

        if (capacity == 0 ||
            capacity >
                (std::numeric_limits<std::size_t>::max)()) {

            return compiled_project_image_result::
                failed;
        }

        try {
            link_targets.assign(
                static_cast<std::size_t>(
                    capacity),
                0);
        }
        catch (...) {
            return compiled_project_image_result::
                failed;
        }
    }

    for (std::size_t index = 0;
         index <
            link_count_value;
         ++index) {

        const link_handle handle{
            static_cast<std::uint32_t>(
                index + 1)};

        link_record value;

        if (!link(
                handle,
                value)) {

            return compiled_project_image_result::
                invalid_image;
        }

        object_entry source_object;
        object_entry target_object;

        if (!object(
                value.source.object,
                source_object) ||
            !object(
                value.target.object,
                target_object) ||
            source_object.type.kind() !=
                type_ref_kind::named ||
            target_object.type.kind() !=
                type_ref_kind::named) {

            return compiled_project_image_result::
                invalid_image;
        }

        type_entry source_type;
        type_entry target_type;

        if (!type(
                type_from_raw(
                    source_object.type.payload()),
                source_type) ||
            !type(
                type_from_raw(
                    target_object.type.payload()),
                target_type) ||
            !value.source.member ||
            !value.target.member ||
            value.source.member.value() >=
                source_type.members.count ||
            value.target.member.value() >=
                target_type.members.count) {

            return compiled_project_image_result::
                invalid_image;
        }

        const auto target_key =
            (static_cast<std::uint64_t>(
                 value.target.object.value()) << 32) |
            (static_cast<std::uint64_t>(
                 value.target.member.value()) +
             1);

        const auto mask =
            link_targets.size() - 1;

        auto position =
            static_cast<std::size_t>(
                mix64(
                    target_key)) &
            mask;

        bool inserted = false;

        for (std::size_t probe = 0;
             probe <
                link_targets.size();
             ++probe) {

            auto& slot =
                link_targets[position];

            if (slot == 0) {
                slot = target_key;
                inserted = true;
                break;
            }

            if (slot == target_key) {
                return compiled_project_image_result::
                    invalid_image;
            }

            position =
                (position + 1) &
                mask;
        }

        if (!inserted) {
            return compiled_project_image_result::
                invalid_image;
        }
    }

    const auto& assign_records =
        section(
            compiled_project_section::
                assign_records);

    const auto& assign_bytes =
        section(
            compiled_project_section::
                assign_bytes);

    std::uint64_t expected_assign_offset = 0;

    for (std::size_t index = 0;
         index <
            assign_count_value;
         ++index) {

        const auto* record =
            assign_records.data +
            index *
                assign_record_size;

        const auto source_offset =
            read_u32(record);

        const auto source_length =
            read_u32(
                record + 4);

        const auto target_offset =
            read_u32(
                record + 8);

        const auto target_length =
            read_u32(
                record + 12);

        std::string_view source;
        std::string_view target;

        if (source_offset !=
                expected_assign_offset ||
            source_length == 0 ||
            target_offset !=
                source_offset +
                source_length ||
            target_length == 0 ||
            !assign(
                index,
                source,
                target)) {

            return compiled_project_image_result::
                invalid_image;
        }

        expected_assign_offset =
            static_cast<std::uint64_t>(
                target_offset) +
            target_length;
    }

    if (expected_assign_offset !=
        assign_bytes.count) {

        return compiled_project_image_result::
            invalid_image;
    }

    return compiled_project_image_result::
        success;
}

compiled_project_image_result
prepare_compiled_project_layout(
    const string_table& strings,
    const identity_space& identities,
    const graph& G,
    const assign_table& assigns,
    compiled_project_layout& output) noexcept {

    output = {};

    if (identities.size() == 0 ||
        G.type_entries().size() !=
            G.type_identity_entries().size() ||
        G.member_entries().size() !=
            G.member_construction_entries().size() ||
        G.object_entries().size() !=
            G.object_identity_entries().size()) {

        return compiled_project_image_result::
            invalid_state;
    }

    const auto string_count =
        static_cast<std::uint64_t>(
            strings.size());

    const auto identity_count =
        static_cast<std::uint64_t>(
            identities.size());

    const auto type_count =
        static_cast<std::uint64_t>(
            G.type_count());

    const auto member_count =
        static_cast<std::uint64_t>(
            G.member_count());

    const auto derived_count =
        static_cast<std::uint64_t>(
            G.derived_type_count());

    const auto object_count =
        static_cast<std::uint64_t>(
            G.object_count());

    const auto link_count =
        static_cast<std::uint64_t>(
            G.link_count());

    const auto assign_count =
        static_cast<std::uint64_t>(
            assigns.size());

    if (string_count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        identity_count >
            identity_ref::maximum_slot ||
        type_count >
            type_handle::maximum_slot ||
        member_count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        derived_count >
            type_ref::maximum_payload ||
        object_count >
            object_handle::maximum_slot ||
        link_count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        assign_count >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return compiled_project_image_result::
            invalid_state;
    }

    const auto string_bytes_count =
        static_cast<std::uint64_t>(
            strings.byte_size());

    const auto assign_bytes_count =
        static_cast<std::uint64_t>(
            assigns.byte_size());

    if (string_bytes_count >
            (std::numeric_limits<std::uint32_t>::max)() ||
        assign_bytes_count >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return compiled_project_image_result::
            invalid_state;
    }

    const auto string_index_count =
        index_capacity(
            string_count);

    const auto identity_index_count =
        index_capacity(
            identity_count > 0
                ? identity_count - 1
                : 0);

    if (string_index_count == 0 ||
        identity_index_count == 0) {

        return compiled_project_image_result::
            failed;
    }

    std::array<
        compiled_project_layout::section_record,
        compiled_project_directory_count>
        layout{{
            {
                compiled_project_section::
                    string_core,
                string_core_size,
                string_count,
            },
            {
                compiled_project_section::
                    string_index,
                index_record_size,
                string_index_count,
            },
            {
                compiled_project_section::
                    string_bytes,
                1,
                string_bytes_count,
            },
            {
                compiled_project_section::
                    identity_core,
                identity_core_size,
                identity_count,
            },
            {
                compiled_project_section::
                    identity_index,
                index_record_size,
                identity_index_count,
            },
            {
                compiled_project_section::
                    types,
                type_record_size,
                type_count,
            },
            {
                compiled_project_section::
                    type_identities,
                type_identity_size,
                type_count,
            },
            {
                compiled_project_section::
                    members,
                member_record_size,
                member_count,
            },
            {
                compiled_project_section::
                    member_construction,
                construction_record_size,
                member_count,
            },
            {
                compiled_project_section::
                    derived_types,
                derived_record_size,
                derived_count,
            },
            {
                compiled_project_section::
                    objects,
                object_record_size,
                object_count,
            },
            {
                compiled_project_section::
                    object_identities,
                object_identity_size,
                object_count,
            },
            {
                compiled_project_section::
                    links,
                link_record_size,
                link_count,
            },
            {
                compiled_project_section::
                    graph_identity_index,
                graph_identity_record_size,
                identity_count + 1,
            },
            {
                compiled_project_section::
                    assign_records,
                assign_record_size,
                assign_count,
            },
            {
                compiled_project_section::
                    assign_bytes,
                1,
                assign_bytes_count,
            },
        }};

    std::uint64_t cursor =
        first_section_offset;

    for (auto& value : layout) {
        if (!align64(
                cursor,
                value.offset)) {

            return compiled_project_image_result::
                failed;
        }

        std::uint64_t bytes = 0;

        if (!multiply_u64(
                value.count,
                value.record_size,
                bytes) ||
            !add_u64(
                value.offset,
                bytes,
                cursor)) {

            return compiled_project_image_result::
                failed;
        }
    }

    if (cursor >
        (std::numeric_limits<std::size_t>::max)()) {

        return compiled_project_image_result::
            failed;
    }


    output.sections =
        layout;

    output.size_value =
        static_cast<std::size_t>(
            cursor);

    return compiled_project_image_result::
        success;
}

compiled_project_image_result
encode_compiled_project_image(
    const string_table& strings,
    const identity_space& identities,
    const graph& G,
    const assign_table& assigns,
    const compiled_project_layout& prepared_layout,
    std::span<std::byte> output) noexcept {

    if (output.size() !=
        prepared_layout.size()) {

        return compiled_project_image_result::
            invalid_state;
    }

    const auto& layout =
        prepared_layout.sections;

    std::fill(
        output.begin(),
        output.begin() +
            static_cast<std::ptrdiff_t>(
                first_section_offset),
        std::byte{0});

    std::uint64_t previous_end =
        first_section_offset;

    for (const auto& value : layout) {
        if (value.offset >
            previous_end) {

            std::fill(
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        previous_end),
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        value.offset),
                std::byte{0});
        }

        previous_end =
            value.offset +
            value.count *
                value.record_size;
    }

    const auto clear_section =
        [&](compiled_project_section kind) noexcept {
            const auto& value =
                layout[
                    section_index(
                        kind)];

            const auto begin =
                static_cast<std::size_t>(
                    value.offset);

            const auto size =
                static_cast<std::size_t>(
                    value.count *
                    value.record_size);

            std::fill(
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        begin),
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        begin + size),
                std::byte{0});
        };

    clear_section(
        compiled_project_section::
            string_index);

    clear_section(
        compiled_project_section::
            identity_index);

    clear_section(
        compiled_project_section::
            graph_identity_index);

    const auto count =
        [&](compiled_project_section kind) noexcept {
            return layout[
                section_index(
                    kind)].count;
        };

    const auto string_count =
        count(
            compiled_project_section::
                string_core);

    const auto identity_count =
        count(
            compiled_project_section::
                identity_core);

    const auto type_count =
        count(
            compiled_project_section::
                types);

    const auto member_count =
        count(
            compiled_project_section::
                members);

    const auto derived_count =
        count(
            compiled_project_section::
                derived_types);

    const auto object_count =
        count(
            compiled_project_section::
                objects);

    const auto link_count =
        count(
            compiled_project_section::
                links);

    const auto assign_count =
        count(
            compiled_project_section::
                assign_records);

    const auto string_bytes_count =
        count(
            compiled_project_section::
                string_bytes);

    const auto assign_bytes_count =
        count(
            compiled_project_section::
                assign_bytes);

    const auto string_index_count =
        count(
            compiled_project_section::
                string_index);

    const auto identity_index_count =
        count(
            compiled_project_section::
                identity_index);

    if (string_count !=
            strings.size() ||
        string_bytes_count !=
            strings.byte_size() ||
        identity_count !=
            identities.size() ||
        type_count !=
            G.type_count() ||
        member_count !=
            G.member_count() ||
        derived_count !=
            G.derived_type_count() ||
        object_count !=
            G.object_count() ||
        link_count !=
            G.link_count() ||
        assign_count !=
            assigns.size() ||
        assign_bytes_count !=
            assigns.byte_size()) {

        return compiled_project_image_result::
            invalid_state;
    }

    auto* base =
        output.data();

    const auto section_data =
        [&](compiled_project_section kind)
        -> std::byte* {

        const auto index =
            section_index(kind);

        return index <
            layout.size()
            ? base +
                static_cast<std::size_t>(
                    layout[index].offset)
            : nullptr;
    };

    // Strings + persisted string lookup index.
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

        std::uint32_t byte_offset = 0;

        const auto mask =
            string_index_count - 1;

        for (std::uint64_t slot = 1;
             slot <= string_count;
             ++slot) {

            const auto slot_value =
                static_cast<std::uint32_t>(
                    slot);

            const auto value =
                strings.spelling(
                    slot_value);

            auto* record =
                core +
                static_cast<std::size_t>(
                    slot - 1) *
                    string_core_size;

            write_u32(
                record,
                byte_offset);

            write_u32(
                record + 4,
                static_cast<std::uint32_t>(
                    value.size()));

            std::memcpy(
                data + byte_offset,
                value.data(),
                value.size());

            const auto hash =
                string_hash(value);

            auto position =
                static_cast<std::uint64_t>(
                    hash) &
                mask;

            for (;;) {
                auto* index_record =
                    index +
                    static_cast<std::size_t>(
                        position) *
                        index_record_size;

                if (read_u32(
                        index_record + 4) == 0) {

                    write_u32(
                        index_record,
                        hash);

                    write_u32(
                        index_record + 4,
                        slot_value);

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
            string_bytes_count) {

                return compiled_project_image_result::
                invalid_state;
        }
    }

    // Identities + persisted identity lookup index.
    {
        auto* core =
            section_data(
                compiled_project_section::
                    identity_core);

        auto* index =
            section_data(
                compiled_project_section::
                    identity_index);

        write_u32(
            core,
            identities.root().value());

        write_u32(
            core + 4,
            0);

        write_u32(
            core + 8,
            0);

        const auto mask =
            identity_index_count - 1;

        for (std::uint32_t slot = 2;
             slot <= identity_count;
             ++slot) {

            const auto identity =
                identities.at_slot(slot);

            const auto* value =
                identities.record(identity);

            if (!identity ||
                value == nullptr ||
                !identities.contains(
                    value->parent) ||
                value->parent.slot() >=
                    slot ||
                !strings.contains(
                    value->name)) {

                        return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                core +
                static_cast<std::size_t>(
                    slot - 1) *
                    identity_core_size;

            write_u32(
                record,
                identity.value());

            write_u32(
                record + 4,
                value->parent.value());

            write_u32(
                record + 8,
                value->name.value());

            const auto hash =
                identity_hash(
                    value->parent.value(),
                    value->name.value(),
                    static_cast<std::uint32_t>(
                        identity.kind()));

            const auto fingerprint =
                identity_fingerprint(hash);

            auto position =
                hash &
                mask;

            for (;;) {
                auto* index_record =
                    index +
                    static_cast<std::size_t>(
                        position) *
                        index_record_size;

                if (read_u32(
                        index_record + 4) == 0) {

                    write_u32(
                        index_record,
                        fingerprint);

                    write_u32(
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

    const auto type_entries =
        G.type_entries();

    const auto type_identities =
        G.type_identity_entries();

    auto* graph_identity =
        section_data(
            compiled_project_section::
                graph_identity_index);

    // Types + dense identity->Graph location index.
    {
        auto* values =
            section_data(
                compiled_project_section::
                    types);

        auto* identities_out =
            section_data(
                compiled_project_section::
                    type_identities);

        for (std::size_t index = 0;
             index <
                type_entries.size();
             ++index) {

            const auto& value =
                type_entries[index];

            const auto identity =
                type_identities[index];

            if (value.kind !=
                    graph_type_kind::record ||
                !valid_record_kind(
                    value.record_kind) ||
                (value.flags &
                    ~graph_type_defined) != 0 ||
                !identities.contains(identity) ||
                identity.kind() !=
                    identity_kind::type) {

                        return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                values +
                index *
                    type_record_size;

            write_u32(
                record,
                value.members.begin);

            write_u32(
                record + 4,
                value.members.count);

            record[8] =
                static_cast<std::byte>(
                    static_cast<std::uint8_t>(
                        value.kind));

            record[9] =
                static_cast<std::byte>(
                    static_cast<std::uint8_t>(
                        value.record_kind));

            write_u16(
                record + 10,
                value.flags);

            write_u32(
                identities_out +
                    index *
                        type_identity_size,
                identity.value());

            auto* location =
                graph_identity +
                static_cast<std::size_t>(
                    identity.slot()) *
                    graph_identity_record_size;

            if (read_u32(location) != 0) {
                        return compiled_project_image_result::
                    invalid_state;
            }

            write_u32(
                location,
                encode_graph_location(
                    1,
                    static_cast<std::uint32_t>(
                        index + 1)));
        }
    }

    // Members + construction sidecar.
    {
        auto* values =
            section_data(
                compiled_project_section::
                    members);

        auto* construction_out =
            section_data(
                compiled_project_section::
                    member_construction);

        const auto member_entries =
            G.member_entries();

        const auto construction =
            G.member_construction_entries();

        for (std::size_t index = 0;
             index <
                member_entries.size();
             ++index) {

            const auto& value =
                member_entries[index];

            const auto& initial =
                construction[index];

            if (!strings.contains(
                    value.name) ||
                !G.contains(
                    value.type) ||
                !valid_member_access(
                    value.access) ||
                !valid_construction(
                    initial)) {

                        return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                values +
                index *
                    member_record_size;

            write_u32(
                record,
                value.name.value());

            write_u32(
                record + 4,
                value.type.value());

            write_u32(
                record + 8,
                static_cast<std::uint32_t>(
                    value.access));

            auto* initial_record =
                construction_out +
                index *
                    construction_record_size;

            write_u32(
                initial_record,
                initial.low);

            write_u32(
                initial_record + 4,
                initial.high);

            write_u32(
                initial_record + 8,
                initial.operand);

            write_u32(
                initial_record + 12,
                static_cast<std::uint32_t>(
                    initial.kind));
        }
    }

    // Derived type canonical slots.
    {
        auto* values =
            section_data(
                compiled_project_section::
                    derived_types);

        const auto derived =
            G.derived_type_entries();

        for (std::size_t index = 0;
             index <
                derived.size();
             ++index) {

            const auto& value =
                derived[index];

            if (!G.contains(
                    value.child) ||
                !valid_derived_kind(
                    value.kind) ||
                (value.child.kind() ==
                        type_ref_kind::derived &&
                 value.child.payload() >
                    index)) {

                        return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                values +
                index *
                    derived_record_size;

            write_u64(
                record,
                value.payload);

            write_u32(
                record + 8,
                value.child.value());

            write_u32(
                record + 12,
                static_cast<std::uint32_t>(
                    value.kind));
        }
    }

    // Objects + dense identity->Graph location index.
    {
        auto* values =
            section_data(
                compiled_project_section::
                    objects);

        auto* identities_out =
            section_data(
                compiled_project_section::
                    object_identities);

        const auto objects =
            G.object_entries();

        const auto object_identities =
            G.object_identity_entries();

        for (std::size_t index = 0;
             index <
                objects.size();
             ++index) {

            const auto& value =
                objects[index];

            const auto identity =
                object_identities[index];

            if (!G.contains(
                    value.type) ||
                (value.flags &
                    ~graph_object_non_default_initializer) != 0 ||
                !identities.contains(identity) ||
                identity.kind() !=
                    identity_kind::object) {

                        return compiled_project_image_result::
                    invalid_state;
            }

            auto* record =
                values +
                index *
                    object_record_size;

            write_u32(
                record,
                value.type.value());

            write_u32(
                record + 4,
                value.flags);

            write_u32(
                identities_out +
                    index *
                        object_identity_size,
                identity.value());

            auto* location =
                graph_identity +
                static_cast<std::size_t>(
                    identity.slot()) *
                    graph_identity_record_size;

            if (read_u32(location) != 0) {
                        return compiled_project_image_result::
                    invalid_state;
            }

            write_u32(
                location,
                encode_graph_location(
                    2,
                    static_cast<std::uint32_t>(
                        index + 1)));
        }
    }

    // Links preserve object_handle + record-local member_index exactly.
    {
        auto* values =
            section_data(
                compiled_project_section::
                    links);

        const auto links =
            G.link_entries();

        for (std::size_t index = 0;
             index <
                links.size();
             ++index) {

            const auto& value =
                links[index];

            auto* record =
                values +
                index *
                    link_record_size;

            write_u32(
                record,
                value.source.object.value());

            write_u32(
                record + 4,
                value.source.member.value());

            write_u32(
                record + 8,
                value.target.object.value());

            write_u32(
                record + 12,
                value.target.member.value());
        }
    }

    // Assign remains raw user data, independent from string_id/identity_ref.
    {
        auto* records =
            section_data(
                compiled_project_section::
                    assign_records);

        auto* data =
            section_data(
                compiled_project_section::
                    assign_bytes);

        std::uint32_t byte_offset = 0;
        std::size_t index = 0;

        for (const auto& value :
             assigns.records()) {

            const auto source =
                assigns.source(value);

            const auto target =
                assigns.target(value);

            auto* record =
                records +
                index *
                    assign_record_size;

            write_u32(
                record,
                byte_offset);

            write_u32(
                record + 4,
                static_cast<std::uint32_t>(
                    source.size()));

            std::memcpy(
                data + byte_offset,
                source.data(),
                source.size());

            byte_offset +=
                static_cast<std::uint32_t>(
                    source.size());

            write_u32(
                record + 8,
                byte_offset);

            write_u32(
                record + 12,
                static_cast<std::uint32_t>(
                    target.size()));

            std::memcpy(
                data + byte_offset,
                target.data(),
                target.size());

            byte_offset +=
                static_cast<std::uint32_t>(
                    target.size());

            ++index;
        }

        if (byte_offset !=
            assign_bytes_count) {

                return compiled_project_image_result::
                invalid_state;
        }
    }

    // Per-section CRC belongs to this encoding, not to the layout.
    std::array<
        std::uint64_t,
        compiled_project_directory_count>
        section_crc{};

    for (std::size_t index = 0;
         index < layout.size();
         ++index) {

        const auto& value =
            layout[index];

        std::uint64_t byte_count = 0;

        if (!multiply_u64(
                value.count,
                value.record_size,
                byte_count) ||
            byte_count >
                (std::numeric_limits<std::size_t>::max)()) {

            return compiled_project_image_result::
                failed;
        }

        section_crc[index] =
            persistence_crc64(
                std::span<const std::byte>{
                    base +
                        static_cast<std::size_t>(
                            value.offset),
                    static_cast<std::size_t>(
                        byte_count)});
    }

    std::memcpy(
        base,
        image_magic.data(),
        image_magic.size());

    write_u32(
        base + 8,
        compiled_project_format_version);

    write_u32(
        base + 12,
        endian_marker);

    write_u32(
        base + 16,
        compiled_project_header_size);

    write_u32(
        base + 20,
        compiled_project_directory_count);

    write_u32(
        base + 24,
        compiled_project_directory_entry_size);

    write_u32(
        base + 28,
        0);

    write_u64(
        base + 32,
        directory_offset);

    write_u64(
        base + 40,
        output.size());

    write_u64(
        base + header_string_count_offset,
        string_count);

    write_u64(
        base + header_identity_count_offset,
        identity_count);

    write_u64(
        base + header_type_count_offset,
        type_count);

    write_u64(
        base + header_object_count_offset,
        object_count);

    write_u64(
        base + header_link_count_offset,
        link_count);

    write_u64(
        base + header_assign_count_offset,
        assign_count);

    for (std::size_t index = 0;
         index < layout.size();
         ++index) {

        const auto& value =
            layout[index];

        auto* entry =
            base +
            directory_offset +
            index *
                compiled_project_directory_entry_size;

        write_u32(
            entry,
            static_cast<std::uint32_t>(
                value.kind));

        write_u32(
            entry + 4,
            value.record_size);

        write_u64(
            entry + 8,
            value.offset);

        write_u64(
            entry + 16,
            value.count);

        write_u64(
            entry + 24,
            section_crc[index]);
    }

    write_u64(
        base +
            header_directory_crc_offset,
        persistence_crc64(
            std::span<const std::byte>{
                base + directory_offset,
                directory_bytes}));

    std::array<
        std::byte,
        compiled_project_header_size>
        header{};

    std::memcpy(
        header.data(),
        base,
        header.size());

    write_u64(
        header.data() +
            header_crc_offset,
        0);

    write_u64(
        base +
            header_crc_offset,
        persistence_crc64(
            header));

    compiled_project_view validation;

    const auto bound =
        validation.bind(
            output);

    if (bound !=
        compiled_project_image_result::
            success) {

        return bound;
    }

    return compiled_project_image_result::
        success;
}

}
