#include "compiled_project_build.hpp"
#include "crc64_ecma.hpp"

#include <array>
#include <cstring>
#include <limits>

namespace cw::server {
namespace {

constexpr std::size_t sparse_directory_offset =
    compiled_project_header_size;

constexpr std::size_t sparse_directory_bytes =
    compiled_project_directory_count *
    compiled_project_directory_entry_size;

constexpr std::size_t sparse_header_file_size_offset = 40;
constexpr std::size_t sparse_header_type_count_offset = 64;
constexpr std::size_t sparse_header_object_count_offset = 72;
constexpr std::size_t sparse_header_link_count_offset = 80;
constexpr std::size_t sparse_header_directory_crc_offset = 240;
constexpr std::size_t sparse_header_crc_offset = 248;

constexpr std::uint32_t sparse_type_record_size = 20;
constexpr std::uint32_t sparse_member_record_size = 12;
constexpr std::uint32_t sparse_construction_record_size = 16;
constexpr std::uint32_t sparse_base_record_size = 8;
constexpr std::uint32_t sparse_object_record_size = 8;
constexpr std::uint32_t sparse_link_record_size = 16;
constexpr std::uint32_t sparse_graph_identity_record_size = 4;
constexpr std::uint32_t sparse_index_record_size = 8;

constexpr std::uint32_t sparse_location_inactive_kind = 3u;
constexpr std::uint32_t sparse_link_inactive_flag = 0x80000000u;
constexpr std::uint32_t sparse_link_state_mask = 0xc0000000u;

[[nodiscard]] constexpr std::size_t sparse_section_index(
    compiled_project_section kind) noexcept {

    return static_cast<std::size_t>(
        static_cast<std::uint32_t>(
            kind) - 1);
}

[[nodiscard]] std::uint32_t sparse_read_u32(
    const std::byte* source) noexcept {

    std::uint32_t value = 0;

    for (std::uint32_t index = 0;
         index < 4;
         ++index) {

        value |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    source[index]))
            << (index * 8);
    }

    return value;
}

[[nodiscard]] std::uint64_t sparse_read_u64(
    const std::byte* source) noexcept {

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

void sparse_write_u16(
    std::byte* target,
    std::uint16_t value) noexcept {

    target[0] =
        static_cast<std::byte>(
            value & 0xffu);

    target[1] =
        static_cast<std::byte>(
            (value >> 8) & 0xffu);
}

void sparse_write_u32(
    std::byte* target,
    std::uint32_t value) noexcept {

    for (std::uint32_t index = 0;
         index < 4;
         ++index) {

        target[index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffu);
    }
}

void sparse_write_u64(
    std::byte* target,
    std::uint64_t value) noexcept {

    for (std::uint32_t index = 0;
         index < 8;
         ++index) {

        target[index] =
            static_cast<std::byte>(
                (value >> (index * 8)) &
                0xffu);
    }
}

[[nodiscard]] constexpr std::uint64_t sparse_mix64(
    std::uint64_t value) noexcept {

    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;

    return value;
}

[[nodiscard]] std::uint64_t sparse_link_target_hash(
    object_endpoint target) noexcept {

    const auto key =
        (static_cast<std::uint64_t>(
             target.object.value()) << 32) |
        (static_cast<std::uint64_t>(
             target.member.value()) +
         1);

    return sparse_mix64(key);
}

[[nodiscard]] std::uint32_t sparse_fingerprint(
    std::uint64_t hash) noexcept {

    auto value =
        static_cast<std::uint32_t>(
            hash ^ (hash >> 32));

    return value == 0
        ? 1
        : value;
}

struct sparse_section final {
    std::byte* data = nullptr;
    std::uint64_t count = 0;
    std::uint32_t record_size = 0;
};

[[nodiscard]] bool sparse_section_at(
    std::span<std::byte> image,
    compiled_project_section kind,
    std::uint32_t expected_record_size,
    sparse_section& output) noexcept {

    output = {};

    const auto index =
        sparse_section_index(kind);

    if (index >=
        compiled_project_directory_count) {

        return false;
    }

    const auto* entry =
        image.data() +
        sparse_directory_offset +
        index *
            compiled_project_directory_entry_size;

    if (sparse_read_u32(entry) !=
            static_cast<std::uint32_t>(
                kind) ||
        sparse_read_u32(
            entry + 4) !=
            expected_record_size) {

        return false;
    }

    const auto offset =
        sparse_read_u64(
            entry + 8);

    const auto count =
        sparse_read_u64(
            entry + 16);

    if (count != 0 &&
        expected_record_size >
            (std::numeric_limits<
                std::uint64_t>::max)() /
                count) {

        return false;
    }

    const auto byte_count =
        count *
        expected_record_size;

    if (offset >
            image.size() ||
        byte_count >
            image.size() -
                static_cast<std::size_t>(
                    offset)) {

        return false;
    }

    output = {
        image.data() +
            static_cast<std::size_t>(
                offset),
        count,
        expected_record_size,
    };

    return true;
}

[[nodiscard]] std::uint32_t sparse_graph_location(
    std::uint32_t kind,
    std::uint32_t slot) noexcept {

    return kind != 0 &&
        kind <=
            sparse_location_inactive_kind &&
        slot != 0 &&
        slot <=
            type_ref::maximum_payload
        ? (kind << 30) | slot
        : 0;
}

void sparse_encode_type(
    std::byte* target,
    const type_entry& value) noexcept {

    sparse_write_u32(
        target,
        value.members.begin);

    sparse_write_u32(
        target + 4,
        value.members.count);

    sparse_write_u32(
        target + 8,
        value.bases.begin);

    sparse_write_u32(
        target + 12,
        value.bases.count);

    target[16] =
        static_cast<std::byte>(
            static_cast<std::uint8_t>(
                value.kind));

    target[17] =
        static_cast<std::byte>(
            static_cast<std::uint8_t>(
                value.record_kind));

    sparse_write_u16(
        target + 18,
        value.flags);
}

void sparse_encode_member(
    std::byte* target,
    const member_record& value) noexcept {

    sparse_write_u32(
        target,
        value.name.value());

    sparse_write_u32(
        target + 4,
        value.type.value());

    target[8] =
        static_cast<std::byte>(
            static_cast<std::uint8_t>(
                value.access));

    target[9] = std::byte{0};
    target[10] = std::byte{0};
    target[11] = std::byte{0};
}

void sparse_encode_construction(
    std::byte* target,
    const construction_value& value) noexcept {

    sparse_write_u32(
        target,
        value.low);

    sparse_write_u32(
        target + 4,
        value.high);

    sparse_write_u32(
        target + 8,
        value.operand);

    sparse_write_u32(
        target + 12,
        static_cast<std::uint32_t>(
            value.kind));
}

void sparse_encode_base(
    std::byte* target,
    const base_record& value) noexcept {

    sparse_write_u32(
        target,
        value.type.value());

    target[4] =
        static_cast<std::byte>(
            static_cast<std::uint8_t>(
                value.access));

    target[5] =
        static_cast<std::byte>(
            value.flags);

    sparse_write_u16(
        target + 6,
        0);
}

void sparse_encode_object(
    std::byte* target,
    const object_entry& value) noexcept {

    sparse_write_u32(
        target,
        value.type.value());

    sparse_write_u32(
        target + 4,
        value.state);
}

void sparse_encode_link(
    std::byte* target,
    const link_record& value) noexcept {

    sparse_write_u32(
        target,
        value.source.object.value());

    sparse_write_u32(
        target + 4,
        value.source.member.value());

    sparse_write_u32(
        target + 8,
        value.target.object.value());

    sparse_write_u32(
        target + 12,
        value.target.member.value());
}

struct sparse_fixed_write_context final {
    std::span<std::byte> image;
    const graph_delta* graph = nullptr;
    sparse_section types;
    sparse_section type_identities;
    sparse_section members;
    sparse_section member_construction;
    sparse_section bases;
    sparse_section objects;
    sparse_section object_identities;
    sparse_section object_construction;
    sparse_section links;
    sparse_section graph_identity;
    sparse_section link_target_index;
    sparse_section graph_append;
    std::size_t reused_members = 0;
    std::size_t reused_bases = 0;
    std::size_t reused_object_construction = 0;
    std::size_t grown_object_construction = 0;
    std::size_t tail_cursor = 0;
    bool allow_growth = false;
};

[[nodiscard]] std::byte*
sparse_object_construction_record(
    sparse_fixed_write_context& context,
    std::uint32_t locator) noexcept {

    if (locator == 0) {
        return nullptr;
    }

    if (locator <=
        context.object_construction.count) {

        return context.object_construction.data +
            static_cast<std::size_t>(
                locator - 1) *
                sparse_construction_record_size;
    }

    const auto tail_unit =
        static_cast<std::uint64_t>(
            locator) -
        context.object_construction.count -
        1;

    if (tail_unit >
        (std::numeric_limits<
            std::uint64_t>::max)() /
            sparse_construction_record_size) {

        return nullptr;
    }

    const auto tail_offset =
        tail_unit *
        sparse_construction_record_size;

    if (tail_offset >
            context.graph_append.count ||
        sparse_construction_record_size >
            context.graph_append.count -
                tail_offset) {

        return nullptr;
    }

    return context.graph_append.data +
        static_cast<std::size_t>(
            tail_offset);
}

[[nodiscard]] bool sparse_find_link_index_slot(
    sparse_fixed_write_context& context,
    object_endpoint target,
    link_handle handle,
    std::byte*& output) noexcept {

    output = nullptr;

    const auto count =
        context.link_target_index.count;

    if (count == 0 ||
        (count &
            (count - 1)) != 0) {

        return false;
    }

    const auto hash =
        sparse_link_target_hash(
            target);

    const auto fingerprint =
        sparse_fingerprint(
            hash);

    const auto mask =
        count - 1;

    auto position =
        hash &
        mask;

    for (std::uint64_t probe = 0;
         probe < count;
         ++probe) {

        auto* slot =
            context.link_target_index.data +
            static_cast<std::size_t>(
                position) *
                sparse_index_record_size;

        const auto raw =
            sparse_read_u32(
                slot + 4);

        if (raw == 0) {
            return false;
        }

        if (sparse_read_u32(slot) ==
                fingerprint &&
            (raw &
                link_handle::maximum_slot) ==
                handle.value()) {

            const auto state =
                raw &
                sparse_link_state_mask;

            if (state != 0 &&
                state !=
                    sparse_link_inactive_flag) {

                return false;
            }

            output = slot;
            return true;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

[[nodiscard]] server_status sparse_write_type_change(
    void* opaque,
    const graph_delta_type_change& change) noexcept {

    auto& context =
        *static_cast<
            sparse_fixed_write_context*>(
                opaque);

    if (change.kind !=
            graph_delta_change_kind::patch ||
        !change.handle ||
        change.handle.value() >
            context.types.count ||
        change.handle.value() >
            context.type_identities.count ||
        !change.identity ||
        change.identity.kind() !=
            identity_kind::type ||
        change.identity.slot() >=
            context.graph_identity.count) {

        return server_status::
            project_artifact_invalid;
    }

    const auto physical =
        static_cast<std::size_t>(
            change.handle.value() - 1);

    if (sparse_read_u32(
            context.type_identities.data +
            physical * 4) !=
        change.identity.value()) {

        return server_status::
            project_artifact_invalid;
    }

    auto* location =
        context.graph_identity.data +
        static_cast<std::size_t>(
            change.identity.slot()) * 4;

    if (change.live) {
        type_entry persisted =
            change.value;

        const auto* old_record =
            context.types.data +
            physical *
                sparse_type_record_size;

        const graph_range old_members{
            sparse_read_u32(
                old_record),
            sparse_read_u32(
                old_record + 4),
        };

        const graph_range old_bases{
            sparse_read_u32(
                old_record + 8),
            sparse_read_u32(
                old_record + 12),
        };

        const auto member_append =
            context.graph->member_entries();

        const auto construction_append =
            context.graph->
                member_construction_entries();

        if (change.value.members.count != 0 &&
            change.value.members.begin >=
                context.members.count) {

            const auto source_begin =
                static_cast<std::size_t>(
                    change.value.members.begin -
                    context.members.count);

            const auto count =
                static_cast<std::size_t>(
                    change.value.members.count);

            if (old_members.count !=
                    change.value.members.count ||
                old_members.begin >
                    context.members.count ||
                count >
                    context.members.count -
                        old_members.begin ||
                source_begin >
                    member_append.size() ||
                count >
                    member_append.size() -
                        source_begin ||
                source_begin >
                    construction_append.size() ||
                count >
                    construction_append.size() -
                        source_begin) {

                return server_status::
                    project_artifact_invalid;
            }

            for (std::size_t index = 0;
                 index < count;
                 ++index) {

                const auto old_logical =
                    static_cast<std::size_t>(
                        old_members.begin) +
                    index;

                const auto* old_member =
                    context.members.data +
                    old_logical *
                        sparse_member_record_size;

                const auto& replacement =
                    member_append[
                        source_begin + index];

                if (sparse_read_u32(
                        old_member) !=
                    replacement.name.value()) {

                    return server_status::
                        project_artifact_invalid;
                }

                sparse_encode_member(
                    context.members.data +
                    old_logical *
                        sparse_member_record_size,
                    replacement);

                sparse_encode_construction(
                    context.member_construction.data +
                    old_logical *
                        sparse_construction_record_size,
                    construction_append[
                        source_begin + index]);
            }

            persisted.members =
                old_members;

            context.reused_members +=
                count;
        }

        const auto base_append =
            context.graph->base_entries();

        if (change.value.bases.count != 0 &&
            change.value.bases.begin >=
                context.bases.count) {

            const auto source_begin =
                static_cast<std::size_t>(
                    change.value.bases.begin -
                    context.bases.count);

            const auto count =
                static_cast<std::size_t>(
                    change.value.bases.count);

            if (old_bases.count !=
                    change.value.bases.count ||
                old_bases.begin >
                    context.bases.count ||
                count >
                    context.bases.count -
                        old_bases.begin ||
                source_begin >
                    base_append.size() ||
                count >
                    base_append.size() -
                        source_begin) {

                return server_status::
                    project_artifact_invalid;
            }

            for (std::size_t index = 0;
                 index < count;
                 ++index) {

                sparse_encode_base(
                    context.bases.data +
                    (static_cast<std::size_t>(
                         old_bases.begin) +
                     index) *
                        sparse_base_record_size,
                    base_append[
                        source_begin + index]);
            }

            persisted.bases =
                old_bases;

            context.reused_bases +=
                count;
        }

        sparse_encode_type(
            context.types.data +
            physical *
                sparse_type_record_size,
            persisted);

        sparse_write_u32(
            location,
            sparse_graph_location(
                1,
                change.handle.value()));
    }
    else {
        sparse_write_u32(
            location,
            sparse_graph_location(
                sparse_location_inactive_kind,
                change.handle.value()));
    }

    return server_status::success;
}

[[nodiscard]] server_status sparse_write_object_change(
    void* opaque,
    const graph_delta_object_change& change) noexcept {

    auto& context =
        *static_cast<
            sparse_fixed_write_context*>(
                opaque);

    if (change.kind !=
            graph_delta_change_kind::patch ||
        !change.handle ||
        change.handle.value() >
            context.objects.count ||
        change.handle.value() >
            context.object_identities.count ||
        !change.identity ||
        change.identity.kind() !=
            identity_kind::object ||
        change.identity.slot() >=
            context.graph_identity.count) {

        return server_status::
            project_artifact_invalid;
    }

    const auto physical =
        static_cast<std::size_t>(
            change.handle.value() - 1);

    if (sparse_read_u32(
            context.object_identities.data +
            physical * 4) !=
        change.identity.value()) {

        return server_status::
            project_artifact_invalid;
    }

    auto* location =
        context.graph_identity.data +
        static_cast<std::size_t>(
            change.identity.slot()) * 4;

    if (change.live) {
        object_entry persisted =
            change.value;

        const auto* old_record =
            context.objects.data +
            physical *
                sparse_object_record_size;

        const auto old_state =
            sparse_read_u32(
                old_record + 4);

        const auto old_construction_slot =
            old_state &
            graph_object_construction_slot_mask;

        const auto new_construction_slot =
            change.value.construction_slot();

        if (change.value.
                non_default_initializer() &&
            new_construction_slot >
                context.object_construction.count) {

            const auto construction_append =
                context.graph->
                    object_construction_entries();

            const auto source_index =
                static_cast<std::size_t>(
                    new_construction_slot -
                    context.object_construction.count -
                    1);

            if (source_index >=
                    construction_append.size() ||
                construction_append[
                    source_index] !=
                    change.construction) {

                return server_status::
                    project_artifact_invalid;
            }

            std::uint32_t persisted_locator = 0;

            if ((old_state &
                    graph_object_non_default_initializer) !=
                0) {

                auto* existing =
                    sparse_object_construction_record(
                        context,
                        old_construction_slot);

                if (existing == nullptr) {
                    return server_status::
                        project_artifact_invalid;
                }

                sparse_encode_construction(
                    existing,
                    change.construction);

                persisted_locator =
                    old_construction_slot;

                ++context.
                    reused_object_construction;
            }
            else {
                if (!context.allow_growth ||
                    (context.tail_cursor &
                        (sparse_construction_record_size -
                         1)) != 0 ||
                    context.tail_cursor >
                        context.graph_append.count ||
                    sparse_construction_record_size >
                        context.graph_append.count -
                            context.tail_cursor) {

                    return server_status::
                        project_artifact_invalid;
                }

                const auto locator =
                    static_cast<std::uint64_t>(
                        context.object_construction.count) +
                    1 +
                    context.tail_cursor /
                        sparse_construction_record_size;

                if (locator == 0 ||
                    locator >
                        graph_object_construction_slot_mask) {

                    return server_status::io_error;
                }

                sparse_encode_construction(
                    context.graph_append.data +
                        context.tail_cursor,
                    change.construction);

                persisted_locator =
                    static_cast<std::uint32_t>(
                        locator);

                context.tail_cursor +=
                    sparse_construction_record_size;

                ++context.
                    grown_object_construction;
            }

            persisted.state =
                (change.value.state &
                    ~graph_object_construction_slot_mask) |
                persisted_locator;
        }

        sparse_encode_object(
            context.objects.data +
            physical *
                sparse_object_record_size,
            persisted);

        sparse_write_u32(
            location,
            sparse_graph_location(
                2,
                change.handle.value()));
    }
    else {
        sparse_write_u32(
            location,
            sparse_graph_location(
                sparse_location_inactive_kind,
                change.handle.value()));
    }

    return server_status::success;
}

[[nodiscard]] server_status sparse_write_link_change(
    void* opaque,
    const graph_delta_link_change& change) noexcept {

    auto& context =
        *static_cast<
            sparse_fixed_write_context*>(
                opaque);

    if (change.kind !=
            graph_delta_change_kind::patch ||
        !change.handle ||
        change.handle.value() >
            context.links.count) {

        return server_status::
            project_artifact_invalid;
    }

    std::byte* index_slot = nullptr;

    if (!sparse_find_link_index_slot(
            context,
            change.value.target,
            change.handle,
            index_slot) ||
        index_slot == nullptr) {

        return server_status::
            project_artifact_invalid;
    }

    if (change.live) {
        sparse_encode_link(
            context.links.data +
            static_cast<std::size_t>(
                change.handle.value() - 1) *
                sparse_link_record_size,
            change.value);

        sparse_write_u32(
            index_slot + 4,
            change.handle.value());
    }
    else {
        sparse_write_u32(
            index_slot + 4,
            sparse_link_inactive_flag |
                change.handle.value());
    }

    return server_status::success;
}

}


compiled_project_image_result
prepare_compiled_project_graph_write_plan(
    const graph_delta& G,
    compiled_project_graph_write_plan& output) noexcept {

    output = {};

    const auto make_slot_append =
        [](
            std::size_t total,
            std::size_t appended,
            std::uint32_t maximum,
            compiled_project_slot_append& range) noexcept {

            range = {};

            if (appended > total ||
                total > maximum ||
                appended >
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {

                return false;
            }

            if (appended == 0) {
                return true;
            }

            const auto baseline =
                total - appended;

            if (baseline >= maximum) {
                return false;
            }

            range.first_slot =
                static_cast<std::uint32_t>(
                    baseline + 1);

            range.count =
                static_cast<std::uint32_t>(
                    appended);

            return true;
        };

    const auto make_index_append =
        [](
            std::size_t total,
            std::size_t appended,
            compiled_project_index_append& range) noexcept {

            range = {};

            if (appended > total ||
                total >
                    (std::numeric_limits<
                        std::uint32_t>::max)() ||
                appended >
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {

                return false;
            }

            range.begin =
                static_cast<std::uint32_t>(
                    total - appended);

            range.count =
                static_cast<std::uint32_t>(
                    appended);

            return true;
        };

    const auto type_append =
        G.type_entries();

    const auto type_identity_append =
        G.type_identity_entries();

    const auto member_append =
        G.member_entries();

    const auto member_construction_append =
        G.member_construction_entries();

    const auto base_append =
        G.base_entries();

    const auto object_append =
        G.object_entries();

    const auto object_identity_append =
        G.object_identity_entries();

    const auto object_construction_append =
        G.object_construction_entries();

    const auto link_append =
        G.link_entries();

    const auto derived_append =
        G.derived_type_entries();

    const auto endpoint_path_append =
        G.endpoint_path_entries();

    const auto endpoint_path_step_append =
        G.endpoint_path_step_entries();

    if (type_append.size() !=
            type_identity_append.size() ||
        member_append.size() !=
            member_construction_append.size() ||
        object_append.size() !=
            object_identity_append.size() ||
        G.type_patch_count() >
            (std::numeric_limits<
                std::uint32_t>::max)() ||
        G.object_patch_count() >
            (std::numeric_limits<
                std::uint32_t>::max)() ||
        G.link_patch_count() >
            (std::numeric_limits<
                std::uint32_t>::max)() ||
        G.initialization_change_count() >
            (std::numeric_limits<
                std::uint32_t>::max)()) {

        return compiled_project_image_result::
            invalid_state;
    }

    if (!make_slot_append(
            G.type_count(),
            type_append.size(),
            type_handle::maximum_slot,
            output.appended_types) ||
        !make_index_append(
            G.member_count(),
            member_append.size(),
            output.appended_members) ||
        !make_index_append(
            G.base_count(),
            base_append.size(),
            output.appended_bases) ||
        !make_slot_append(
            G.object_count(),
            object_append.size(),
            object_handle::maximum_slot,
            output.appended_objects) ||
        !make_slot_append(
            G.object_construction_count(),
            object_construction_append.size(),
            graph_object_construction_slot_mask,
            output.appended_object_construction) ||
        !make_slot_append(
            G.link_count(),
            link_append.size(),
            link_handle::maximum_slot,
            output.appended_links) ||
        !make_slot_append(
            G.derived_type_count(),
            derived_append.size(),
            type_ref::maximum_payload,
            output.appended_derived_types) ||
        !make_slot_append(
            G.endpoint_path_count(),
            endpoint_path_append.size(),
            endpoint_path_handle::maximum_slot,
            output.appended_endpoint_paths) ||
        !make_index_append(
            G.endpoint_path_step_count(),
            endpoint_path_step_append.size(),
            output.appended_endpoint_path_steps)) {

        output = {};
        return compiled_project_image_result::
            invalid_state;
    }

    output.type_patch_count =
        static_cast<std::uint32_t>(
            G.type_patch_count());

    output.object_patch_count =
        static_cast<std::uint32_t>(
            G.object_patch_count());

    output.link_patch_count =
        static_cast<std::uint32_t>(
            G.link_patch_count());

    output.initialization_change_count =
        static_cast<std::uint32_t>(
            G.initialization_change_count());

    std::size_t payload_bytes = 0;

    const auto add_bytes =
        [&payload_bytes](
            std::size_t count,
            std::size_t record_size) noexcept {

            if (count != 0 &&
                record_size >
                    (std::numeric_limits<
                        std::size_t>::max)() /
                        count) {

                return false;
            }

            const auto bytes =
                count * record_size;

            if (payload_bytes >
                (std::numeric_limits<
                    std::size_t>::max)() -
                    bytes) {

                return false;
            }

            payload_bytes +=
                bytes;

            return true;
        };

    if (!add_bytes(
            output.type_patch_count,
            sizeof(type_entry)) ||
        !add_bytes(
            output.object_patch_count,
            sizeof(object_entry)) ||
        !add_bytes(
            output.link_patch_count,
            sizeof(link_record)) ||
        !add_bytes(
            output.initialization_change_count,
            sizeof(object_initialization_record)) ||
        !add_bytes(
            type_append.size(),
            sizeof(type_entry) +
                sizeof(identity_ref)) ||
        !add_bytes(
            member_append.size(),
            sizeof(member_record) +
                sizeof(construction_value)) ||
        !add_bytes(
            base_append.size(),
            sizeof(base_record)) ||
        !add_bytes(
            object_append.size(),
            sizeof(object_entry) +
                sizeof(identity_ref)) ||
        !add_bytes(
            object_construction_append.size(),
            sizeof(construction_value)) ||
        !add_bytes(
            link_append.size(),
            sizeof(link_record)) ||
        !add_bytes(
            derived_append.size(),
            sizeof(derived_type_record)) ||
        !add_bytes(
            endpoint_path_append.size(),
            sizeof(endpoint_path_record)) ||
        !add_bytes(
            endpoint_path_step_append.size(),
            sizeof(endpoint_path_step))) {

        output = {};
        return compiled_project_image_result::
            failed;
    }

    output.graph_payload_bytes =
        payload_bytes;

    return compiled_project_image_result::
        success;
}


namespace {

struct sparse_growth_context final {
    const compiled_project_view* baseline = nullptr;
    const graph_delta* graph = nullptr;
    std::size_t compact_construction_count = 0;
    std::size_t accounted = 0;
    std::size_t growth_count = 0;
};

[[nodiscard]] server_status
sparse_measure_object_growth(
    void* opaque,
    const graph_delta_object_change& change) noexcept {

    auto& context =
        *static_cast<
            sparse_growth_context*>(
                opaque);

    if (change.kind !=
            graph_delta_change_kind::patch ||
        !change.handle) {

        return server_status::
            project_artifact_invalid;
    }

    if (!change.live ||
        !change.value.
            non_default_initializer() ||
        change.value.construction_slot() <=
            context.compact_construction_count) {

        return server_status::success;
    }

    const auto source_index =
        static_cast<std::size_t>(
            change.value.construction_slot() -
            context.compact_construction_count -
            1);

    const auto append =
        context.graph->
            object_construction_entries();

    if (source_index >=
            append.size() ||
        append[source_index] !=
            change.construction) {

        return server_status::
            project_artifact_invalid;
    }

    object_entry previous;

    if (!context.baseline->
            object_raw(
                change.handle,
                previous)) {

        return server_status::
            project_artifact_invalid;
    }

    ++context.accounted;

    if (!previous.
            non_default_initializer()) {

        ++context.growth_count;
    }

    return server_status::success;
}

[[nodiscard]] compiled_project_image_result
apply_sparse_graph_writes(
    const graph_delta& G,
    std::span<std::byte> image,
    bool allow_growth,
    std::size_t old_tail_count) noexcept {

    compiled_project_view validation;

    if (validation.bind(
            image) !=
        compiled_project_image_result::
            success) {

        return compiled_project_image_result::
            invalid_image;
    }

    compiled_project_graph_write_plan plan;

    const auto prepared =
        prepare_compiled_project_graph_write_plan(
            G,
            plan);

    if (prepared !=
        compiled_project_image_result::
            success) {

        return prepared;
    }

    if (plan.initialization_change_count != 0 ||
        plan.appended_types.count != 0 ||
        plan.appended_objects.count != 0 ||
        plan.appended_links.count != 0 ||
        plan.appended_derived_types.count != 0 ||
        plan.appended_endpoint_paths.count != 0 ||
        plan.appended_endpoint_path_steps.count != 0) {

        return compiled_project_image_result::
            invalid_state;
    }

    sparse_fixed_write_context context{
        image,
        &G,
    };

    if (!sparse_section_at(
            image,
            compiled_project_section::types,
            sparse_type_record_size,
            context.types) ||
        !sparse_section_at(
            image,
            compiled_project_section::
                type_identities,
            4,
            context.type_identities) ||
        !sparse_section_at(
            image,
            compiled_project_section::members,
            sparse_member_record_size,
            context.members) ||
        !sparse_section_at(
            image,
            compiled_project_section::
                member_construction,
            sparse_construction_record_size,
            context.member_construction) ||
        !sparse_section_at(
            image,
            compiled_project_section::bases,
            sparse_base_record_size,
            context.bases) ||
        !sparse_section_at(
            image,
            compiled_project_section::objects,
            sparse_object_record_size,
            context.objects) ||
        !sparse_section_at(
            image,
            compiled_project_section::
                object_identities,
            4,
            context.object_identities) ||
        !sparse_section_at(
            image,
            compiled_project_section::
                object_construction,
            sparse_construction_record_size,
            context.object_construction) ||
        !sparse_section_at(
            image,
            compiled_project_section::links,
            sparse_link_record_size,
            context.links) ||
        !sparse_section_at(
            image,
            compiled_project_section::
                graph_identity_index,
            sparse_graph_identity_record_size,
            context.graph_identity) ||
        !sparse_section_at(
            image,
            compiled_project_section::
                link_target_index,
            sparse_index_record_size,
            context.link_target_index) ||
        !sparse_section_at(
            image,
            compiled_project_section::
                graph_append_bytes,
            1,
            context.graph_append)) {

        return compiled_project_image_result::
            invalid_image;
    }

    if (old_tail_count >
        context.graph_append.count) {

        return compiled_project_image_result::
            invalid_state;
    }

    context.allow_growth =
        allow_growth;

    context.tail_cursor =
        old_tail_count;

    if (allow_growth) {
        if (context.tail_cursor >
            (std::numeric_limits<
                std::size_t>::max)() - 15) {

            return compiled_project_image_result::
                failed;
        }

        context.tail_cursor =
            (context.tail_cursor + 15) &
            ~std::size_t{15};
    }

    if (!succeeded(
            G.visit_type_changes(
                &context,
                sparse_write_type_change)) ||
        !succeeded(
            G.visit_object_changes(
                &context,
                sparse_write_object_change)) ||
        !succeeded(
            G.visit_link_changes(
                &context,
                sparse_write_link_change))) {

        return compiled_project_image_result::
            invalid_state;
    }

    if (context.reused_members !=
            G.member_entries().size() ||
        context.reused_members !=
            G.member_construction_entries().size() ||
        context.reused_bases !=
            G.base_entries().size() ||
        context.reused_object_construction +
                context.grown_object_construction !=
            G.object_construction_entries().size() ||
        (!allow_growth &&
         context.grown_object_construction != 0) ||
        (allow_growth &&
         context.tail_cursor !=
            context.graph_append.count)) {

        return compiled_project_image_result::
            invalid_state;
    }

    sparse_write_u64(
        image.data() +
            sparse_header_type_count_offset,
        G.live_type_count());

    sparse_write_u64(
        image.data() +
            sparse_header_object_count_offset,
        G.live_object_count());

    sparse_write_u64(
        image.data() +
            sparse_header_link_count_offset,
        G.live_link_count());

    std::array<
        std::byte,
        compiled_project_header_size>
        header{};

    std::memcpy(
        header.data(),
        image.data(),
        header.size());

    sparse_write_u64(
        header.data() +
            sparse_header_crc_offset,
        0);

    sparse_write_u64(
        image.data() +
            sparse_header_crc_offset,
        persistence_crc64(
            header));

    compiled_project_view rebound;

    return rebound.bind(
        image);
}

}


compiled_project_image_result
apply_compiled_project_graph_fixed_writes(
    const graph_delta& G,
    std::span<std::byte> image) noexcept {

    compiled_project_view baseline;

    if (baseline.bind(
            image) !=
        compiled_project_image_result::
            success) {

        return compiled_project_image_result::
            invalid_image;
    }

    return apply_sparse_graph_writes(
        G,
        image,
        false,
        baseline.graph_append_byte_size());
}


compiled_project_image_result
prepare_compiled_project_graph_growth(
    const graph_delta& G,
    const compiled_project_view& baseline,
    std::size_t& additional_bytes) noexcept {

    additional_bytes = 0;

    compiled_project_graph_write_plan plan;

    const auto prepared =
        prepare_compiled_project_graph_write_plan(
            G,
            plan);

    if (prepared !=
        compiled_project_image_result::
            success) {

        return prepared;
    }

    // This slice grows only object construction. Other genuine growth remains
    // fail-closed until its direct EOF representation is added.
    if (plan.initialization_change_count != 0 ||
        plan.appended_types.count != 0 ||
        plan.appended_members.count != 0 ||
        plan.appended_bases.count != 0 ||
        plan.appended_objects.count != 0 ||
        plan.appended_links.count != 0 ||
        plan.appended_derived_types.count != 0 ||
        plan.appended_endpoint_paths.count != 0 ||
        plan.appended_endpoint_path_steps.count != 0) {

        return compiled_project_image_result::
            invalid_state;
    }

    sparse_growth_context context{
        &baseline,
        &G,
        baseline.object_construction_count(),
    };

    if (!succeeded(
            G.visit_object_changes(
                &context,
                sparse_measure_object_growth)) ||
        context.accounted !=
            G.object_construction_entries().
                size()) {

        return compiled_project_image_result::
            invalid_state;
    }

    if (context.growth_count == 0) {
        return compiled_project_image_result::
            success;
    }

    const auto old_tail =
        baseline.graph_append_byte_size();

    if (old_tail >
        (std::numeric_limits<
            std::size_t>::max)() - 15) {

        return compiled_project_image_result::
            failed;
    }

    const auto aligned_tail =
        (old_tail + 15) &
        ~std::size_t{15};

    if (context.growth_count >
        ((std::numeric_limits<
              std::size_t>::max)() -
         aligned_tail) /
            sparse_construction_record_size) {

        return compiled_project_image_result::
            failed;
    }

    const auto final_tail =
        aligned_tail +
        context.growth_count *
            sparse_construction_record_size;

    const auto last_unit =
        (final_tail -
         sparse_construction_record_size) /
        sparse_construction_record_size;

    if (baseline.object_construction_count() >
            graph_object_construction_slot_mask ||
        last_unit >
            graph_object_construction_slot_mask ||
        baseline.object_construction_count() +
                1 +
                last_unit >
            graph_object_construction_slot_mask) {

        return compiled_project_image_result::
            failed;
    }

    additional_bytes =
        final_tail -
        old_tail;

    return compiled_project_image_result::
        success;
}


compiled_project_image_result
apply_compiled_project_graph_writes(
    const graph_delta& G,
    std::span<std::byte> image,
    std::size_t previous_size) noexcept {

    if (previous_size >
        image.size()) {

        return compiled_project_image_result::
            invalid_state;
    }

    compiled_project_view baseline;

    if (baseline.bind(
            image.first(
                previous_size)) !=
        compiled_project_image_result::
            success) {

        return compiled_project_image_result::
            invalid_image;
    }

    std::size_t growth = 0;

    const auto prepared =
        prepare_compiled_project_graph_growth(
            G,
            baseline,
            growth);

    if (prepared !=
        compiled_project_image_result::
            success) {

        return prepared;
    }

    if (growth == 0) {
        return image.size() ==
                previous_size
            ? apply_compiled_project_graph_fixed_writes(
                G,
                image)
            : compiled_project_image_result::
                invalid_state;
    }

    if (growth >
            (std::numeric_limits<
                std::size_t>::max)() -
                previous_size ||
        image.size() !=
            previous_size +
                growth) {

        return compiled_project_image_result::
            invalid_state;
    }

    const auto tail_index =
        sparse_section_index(
            compiled_project_section::
                graph_append_bytes);

    auto* tail_entry =
        image.data() +
        sparse_directory_offset +
        tail_index *
            compiled_project_directory_entry_size;

    if (sparse_read_u32(
            tail_entry) !=
            static_cast<std::uint32_t>(
                compiled_project_section::
                    graph_append_bytes) ||
        sparse_read_u32(
            tail_entry + 4) != 1) {

        return compiled_project_image_result::
            invalid_image;
    }

    const auto tail_offset =
        sparse_read_u64(
            tail_entry + 8);

    const auto old_tail_count =
        sparse_read_u64(
            tail_entry + 16);

    if (old_tail_count !=
            baseline.graph_append_byte_size() ||
        tail_offset >
            previous_size ||
        old_tail_count >
            previous_size -
                static_cast<std::size_t>(
                    tail_offset) ||
        tail_offset +
                old_tail_count !=
            previous_size) {

        return compiled_project_image_result::
            invalid_image;
    }

    std::fill(
        image.begin() +
            static_cast<std::ptrdiff_t>(
                previous_size),
        image.end(),
        std::byte{0});

    const auto new_tail_count =
        old_tail_count +
        growth;

    sparse_write_u64(
        tail_entry + 16,
        new_tail_count);

    sparse_write_u64(
        image.data() +
            sparse_header_file_size_offset,
        image.size());

    const auto directory =
        image.subspan(
            sparse_directory_offset,
            sparse_directory_bytes);

    sparse_write_u64(
        image.data() +
            sparse_header_directory_crc_offset,
        persistence_crc64(
            directory));

    std::array<
        std::byte,
        compiled_project_header_size>
        header{};

    std::memcpy(
        header.data(),
        image.data(),
        header.size());

    sparse_write_u64(
        header.data() +
            sparse_header_crc_offset,
        0);

    sparse_write_u64(
        image.data() +
            sparse_header_crc_offset,
        persistence_crc64(
            header));

    return apply_sparse_graph_writes(
        G,
        image,
        true,
        static_cast<std::size_t>(
            old_tail_count));
}

}
