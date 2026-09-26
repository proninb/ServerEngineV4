#include "project/persistence/compiled_project.hpp"
#include "project/persistence/crc64_ecma.hpp"
#include "project/runtime/fixed_direct_materializer.hpp"
#include "project/runtime/runtime_layout.hpp"
#include "project/file/file_context.hpp"
#include "project/graph/graph_delta.hpp"
#include "project/source/source_map.hpp"
#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

constexpr std::size_t directory_offset =
    compiled_project_header_size;

constexpr std::size_t directory_bytes =
    compiled_project_directory_count *
    compiled_project_directory_entry_size;

constexpr std::size_t header_directory_crc_offset = 240;
constexpr std::size_t header_crc_offset = 248;

struct compiled_test_image final {
    std::vector<std::byte> bytes;
};

struct test_state final {
    int failures = 0;

    bool expect(
        bool condition,
        std::string_view name) {

        if (condition) {
            return true;
        }

        ++failures;
        std::cerr << "FAILED: " << name << '\n';
        return false;
    }
};

struct compiled_fixture final {
    compiled_fixture()
        : identities(strings) {
    }

    string_table strings;
    identity_space identities;
    graph G;
    file_context files;
    source_map sources;
    assign_table assigns;

    string_id namespace_name{};
    string_id type_name{};
    string_id value_name{};
    string_id peer_name{};
    string_id left_name{};
    string_id right_name{};
    string_id scalar_name{};

    identity_ref namespace_identity{};
    identity_ref type_identity{};
    identity_ref left_identity{};
    identity_ref right_identity{};
    identity_ref scalar_identity{};

    type_handle type{};
    type_ref integer_type{};
    type_ref reference_type{};
    type_ref named_type{};

    member_index value_member{};
    member_index peer_member{};

    object_handle left{};
    object_handle right{};
    object_handle scalar{};
    link_handle link{};
};

[[nodiscard]] bool success(
    test_state& tests,
    server_status status,
    std::string_view name) {

    return tests.expect(
        status == server_status::success,
        name);
}

void test_graph_reference_invariants(
    test_state& tests) {

    string_table strings;
    identity_space identities{strings};
    graph G;

    const auto make_identity =
        [&](std::string_view name,
            identity_kind kind) {

            string_id text;
            identity_ref output;

            tests.expect(
                succeeded(
                    strings.intern(
                        name,
                        text)) &&
                succeeded(
                    identities.resolve(
                        identities.root(),
                        text,
                        kind,
                        output)),
                "create Graph invariant identity");

            return output;
        };

    string_id value_name;
    string_id real_name;
    string_id input_name;

    if (!tests.expect(
            succeeded(strings.intern("value", value_name)) &&
            succeeded(strings.intern("real", real_name)) &&
            succeeded(strings.intern("in", input_name)),
            "create Graph invariant member names")) {

        return;
    }

    const auto integer_type =
        G.intrinsic(
            intrinsic_type::signed_int);

    const auto real_type =
        G.intrinsic(
            intrinsic_type::double_type);

    type_ref integer_reference;
    type_ref real_reference;

    if (!tests.expect(
            succeeded(
                G.derive(
                    integer_type,
                    derived_type_kind::lvalue_reference,
                    0,
                    integer_reference)) &&
            succeeded(
                G.derive(
                    real_type,
                    derived_type_kind::lvalue_reference,
                    0,
                    real_reference)),
            "derive Graph invariant references")) {

        return;
    }

    object_handle static_integer;

    if (!tests.expect(
            succeeded(
                G.add_object(
                    make_identity(
                        "static_integer",
                        identity_kind::object),
                    integer_type,
                    static_integer,
                    graph_object_internal_static)),
            "add Graph invariant internal static")) {

        return;
    }

    type_handle bad_member;

    if (!tests.expect(
            succeeded(
                G.declare_record(
                    make_identity(
                        "BadMember",
                        identity_kind::type),
                    graph_record_kind::struct_type,
                    bad_member)),
            "declare incompatible member binding record")) {

        return;
    }

    const std::array<member_record, 2>
        bad_member_definition{{
            {
                value_name,
                integer_type,
                graph_member_access::public_access,
            },
            {
                input_name,
                real_reference,
                graph_member_access::public_access,
            },
        }};

    const std::array<construction_value, 2>
        bad_member_construction{{
            construction_value{},
            construction_value::member_binding(1),
        }};

    tests.expect(
        G.define_record(
            bad_member,
            graph_record_kind::struct_type,
            bad_member_definition,
            bad_member_construction) ==
                server_status::project_configuration_invalid &&
        G.find(bad_member) != nullptr &&
        !G.find(bad_member)->defined(),
        "Graph rejects incompatible member binding before mutation");

    type_handle bad_object;

    if (!tests.expect(
            succeeded(
                G.declare_record(
                    make_identity(
                        "BadObject",
                        identity_kind::type),
                    graph_record_kind::struct_type,
                    bad_object)),
            "declare incompatible object binding record")) {

        return;
    }

    const std::array<member_record, 1>
        bad_object_definition{{
            {
                input_name,
                real_reference,
                graph_member_access::public_access,
            },
        }};

    const std::array<construction_value, 1>
        bad_object_construction{{
            construction_value::object_binding(
                static_integer.value()),
        }};

    tests.expect(
        G.define_record(
            bad_object,
            graph_record_kind::struct_type,
            bad_object_definition,
            bad_object_construction) ==
            server_status::project_configuration_invalid,
        "Graph rejects incompatible object binding");

type_handle scalar_member_type;

if (!tests.expect(
        succeeded(
            G.declare_record(
                make_identity(
                    "BadScalarMember",
                    identity_kind::type),
                graph_record_kind::struct_type,
                scalar_member_type)),
        "declare scalar construction mismatch record")) {

    return;
}

const std::array<member_record, 1>
    scalar_member_definition{{
        {
            value_name,
            integer_type,
            graph_member_access::public_access,
        },
    }};

const std::array<construction_value, 1>
    scalar_member_construction{{
        construction_value::constant(
            construction_kind::real,
            0),
    }};

tests.expect(
    G.define_record(
        scalar_member_type,
        graph_record_kind::struct_type,
        scalar_member_definition,
        scalar_member_construction) ==
            server_status::project_configuration_invalid,
    "Graph rejects real construction for integral member");

    type_handle type;

    if (!tests.expect(
            succeeded(
                G.declare_record(
                    make_identity(
                        "T",
                        identity_kind::type),
                    graph_record_kind::struct_type,
                    type)),
            "declare valid Graph invariant record")) {

        return;
    }

    const std::array<member_record, 3> definition{{
        {
            value_name,
            integer_type,
            graph_member_access::public_access,
        },
        {
            real_name,
            real_type,
            graph_member_access::public_access,
        },
        {
            input_name,
            integer_reference,
            graph_member_access::public_access,
        },
    }};

    if (!tests.expect(
            succeeded(
                G.define_record(
                    type,
                    graph_record_kind::struct_type,
                    definition)),
            "define valid Graph invariant record")) {

        return;
    }

    const auto named_type =
        G.named(type);

    object_handle left;
    object_handle right;

    if (!tests.expect(
            succeeded(
                G.add_object(
                    make_identity(
                        "left",
                        identity_kind::object),
                    named_type,
                    left)) &&
            succeeded(
                G.add_object(
                    make_identity(
                        "right",
                        identity_kind::object),
                    named_type,
                    right)),
            "add Graph invariant objects")) {

        return;
    }

    const auto value =
        G.find_member(
            type,
            value_name);

    const auto real =
        G.find_member(
            type,
            real_name);

    const auto input =
        G.find_member(
            type,
            input_name);

    link_handle valid;

    tests.expect(
        succeeded(
            G.add_link(
                {
                    left,
                    value,
                },
                {
                    right,
                    input,
                },
                valid)) &&
        valid,
        "Graph accepts exact source-to-reference link");

object_handle invalid_named_object;

tests.expect(
    G.add_object(
        make_identity(
            "invalid_named_object",
            identity_kind::object),
        named_type,
        invalid_named_object,
        graph_object_non_default_initializer,
        construction_value::constant(
            construction_kind::unsigned_integer,
            7)) ==
            server_status::project_configuration_invalid &&
    !invalid_named_object,
    "Graph rejects scalar construction for record object");

type_handle bound_type;

if (!tests.expect(
        succeeded(
            G.declare_record(
                make_identity(
                    "Bound",
                    identity_kind::type),
                graph_record_kind::struct_type,
                bound_type)),
        "declare default reference binding record")) {

    return;
}

const std::array<member_record, 2>
    bound_definition{{
        {
            value_name,
            integer_type,
            graph_member_access::public_access,
        },
        {
            input_name,
            integer_reference,
            graph_member_access::public_access,
        },
    }};

const std::array<construction_value, 2>
    bound_construction{{
        {},
        construction_value::member_binding(1),
    }};

if (!tests.expect(
        succeeded(
            G.define_record(
                bound_type,
                graph_record_kind::struct_type,
                bound_definition,
                bound_construction)),
        "define default reference binding record")) {

    return;
}

const auto bound_named =
    G.named(
        bound_type);

object_handle bound_left;
object_handle bound_right;

if (!tests.expect(
        succeeded(
            G.add_object(
                make_identity(
                    "bound_left",
                    identity_kind::object),
                bound_named,
                bound_left)) &&
        succeeded(
            G.add_object(
                make_identity(
                    "bound_right",
                    identity_kind::object),
                bound_named,
                bound_right)),
        "add default reference binding objects")) {

    return;
}

link_handle override_link;

tests.expect(
    succeeded(
        G.add_link(
            {
                bound_left,
                G.find_member(
                    bound_type,
                    value_name),
            },
            {
                bound_right,
                G.find_member(
                    bound_type,
                    input_name),
            },
            override_link)) &&
    override_link,
    "Graph link overrides type-level default reference binding per object");

    const auto link_count =
        G.link_count();

    link_handle invalid_target;

    tests.expect(
        G.add_link(
            {
                left,
                value,
            },
            {
                right,
                value,
            },
            invalid_target) ==
                server_status::project_configuration_invalid &&
        !invalid_target &&
        G.link_count() ==
            link_count,
        "Graph rejects non-reference link target without mutation");

    link_handle invalid_source;

    tests.expect(
        G.add_link(
            {
                left,
                real,
            },
            {
                left,
                input,
            },
            invalid_source) ==
                server_status::project_configuration_invalid &&
        !invalid_source &&
        G.link_count() ==
            link_count,
        "Graph rejects incompatible link source without mutation");
}

[[nodiscard]] bool build_fixture(
    test_state& tests,
    compiled_fixture& fixture) {

    const auto intern =
        [&](std::string_view value,
            string_id& output,
            std::string_view name) {

        return success(
            tests,
            fixture.strings.intern(
                value,
                output),
            name);
    };

    if (!intern("demo", fixture.namespace_name, "intern namespace") ||
        !intern("Widget", fixture.type_name, "intern type") ||
        !intern("value", fixture.value_name, "intern value member") ||
        !intern("peer", fixture.peer_name, "intern peer member") ||
        !intern("left", fixture.left_name, "intern left object") ||
        !intern("right", fixture.right_name, "intern right object") ||
        !intern("scalar", fixture.scalar_name, "intern scalar object")) {

        return false;
    }

    if (!success(
            tests,
            fixture.identities.resolve(
                fixture.identities.root(),
                fixture.namespace_name,
                identity_kind::namespace_scope,
                fixture.namespace_identity),
            "resolve namespace") ||
        !success(
            tests,
            fixture.identities.resolve(
                fixture.namespace_identity,
                fixture.type_name,
                identity_kind::type,
                fixture.type_identity),
            "resolve type") ||
        !success(
            tests,
            fixture.identities.resolve(
                fixture.namespace_identity,
                fixture.left_name,
                identity_kind::object,
                fixture.left_identity),
            "resolve left object") ||
        !success(
            tests,
            fixture.identities.resolve(
                fixture.namespace_identity,
                fixture.right_name,
                identity_kind::object,
                fixture.right_identity),
            "resolve right object") ||
        !success(
            tests,
            fixture.identities.resolve(
                fixture.namespace_identity,
                fixture.scalar_name,
                identity_kind::object,
                fixture.scalar_identity),
            "resolve scalar object")) {

        return false;
    }

    if (!success(
            tests,
            fixture.G.declare_record(
                fixture.type_identity,
                graph_record_kind::struct_type,
                fixture.type),
            "declare record")) {

        return false;
    }

    fixture.integer_type =
        fixture.G.intrinsic(
            intrinsic_type::signed_int);

    if (!tests.expect(
            static_cast<bool>(fixture.integer_type),
            "create intrinsic type")) {

        return false;
    }

    if (!success(
            tests,
            fixture.G.derive(
                fixture.integer_type,
                derived_type_kind::lvalue_reference,
                0,
                fixture.reference_type),
            "derive reference type")) {

        return false;
    }

    const std::array<member_record, 2> members{{
        {
            fixture.value_name,
            fixture.integer_type,
            graph_member_access::public_access,
        },
        {
            fixture.peer_name,
            fixture.reference_type,
            graph_member_access::private_access,
        },
    }};

    const std::array<construction_value, 2> construction{{
        construction_value::constant(
            construction_kind::signed_integer,
            42),
        construction_value{},
    }};

    if (!success(
            tests,
            fixture.G.define_record(
                fixture.type,
                graph_record_kind::struct_type,
                std::span<const member_record>{members},
                std::span<const construction_value>{construction}),
            "define record")) {

        return false;
    }

    fixture.named_type =
        fixture.G.named(
            fixture.type);

    if (!tests.expect(
            static_cast<bool>(fixture.named_type),
            "create named type")) {

        return false;
    }

    fixture.value_member =
        fixture.G.find_member(
            fixture.type,
            fixture.value_name);

    fixture.peer_member =
        fixture.G.find_member(
            fixture.type,
            fixture.peer_name);

    if (!tests.expect(
            fixture.value_member &&
                fixture.value_member.value() == 0,
            "zero-based value member") ||
        !tests.expect(
            fixture.peer_member &&
                fixture.peer_member.value() == 1,
            "zero-based peer member")) {

        return false;
    }

    if (!success(
            tests,
            fixture.G.add_object(
                fixture.left_identity,
                fixture.named_type,
                fixture.left),
            "add left object") ||
        !success(
            tests,
            fixture.G.add_object(
                fixture.right_identity,
                fixture.named_type,
                fixture.right),
            "add right object") ||
        !success(
            tests,
            fixture.G.add_object(
                fixture.scalar_identity,
                fixture.integer_type,
                fixture.scalar,
                graph_object_non_default_initializer,
                construction_value::constant(
                    construction_kind::unsigned_integer,
                    7)),
            "add scalar object") ||
        !success(
            tests,
            fixture.G.add_link(
                {
                    fixture.left,
                    fixture.value_member,
                },
                {
                    fixture.right,
                    fixture.peer_member,
                },
                fixture.link),
            "add link") ||
        !success(
            tests,
            fixture.assigns.add(
                "sensor.value",
                "ui.value"),
            "add first Assign") ||
        !success(
            tests,
            fixture.assigns.add(
                "source.path",
                "target.path"),
            "add second Assign")) {

        return false;
    }

    const auto directory =
        std::filesystem::temp_directory_path();

    for (const auto& entry :
         std::array{
             std::pair{
                 "header_root.hpp",
                 file_kind::header},
             std::pair{
                 "shared.hpp",
                 file_kind::header},
             std::pair{
                 "other_header_root.hpp",
                 file_kind::header},
             std::pair{
                 "objects.source",
                 file_kind::source}}) {

        file_id file;

        if (!success(
                tests,
                fixture.files.resolve(
                    directory / entry.first,
                    entry.second,
                    file),
                "resolve source map file")) {

            return false;
        }
    }

    for (const auto root :
         {file_id{1},
          file_id{3}}) {

        if (!success(
                tests,
                fixture.sources.begin_root(root),
                "begin persisted Header root") ||
            !success(
                tests,
                fixture.sources.add(
                    file_id{2},
                    source_data_ref::type_definition(
                        fixture.type_identity)),
                "shared persisted Header definition") ||
            !success(
                tests,
                fixture.sources.end_root(),
                "end persisted Header root")) {

            return false;
        }
    }

    if (!success(
            tests,
            fixture.sources.begin_root(
                file_id{4}),
            "begin persisted Source root") ||
        !success(
            tests,
            fixture.sources.add(
                file_id{4},
                source_data_ref::object(
                    fixture.left_identity)),
            "persist left Source object") ||
        !success(
            tests,
            fixture.sources.add(
                file_id{4},
                source_data_ref::object(
                    fixture.right_identity)),
            "persist right Source object") ||
        !success(
            tests,
            fixture.sources.add(
                file_id{4},
                source_data_ref::object(
                    fixture.scalar_identity)),
            "persist scalar Source object") ||
        !success(
            tests,
            fixture.sources.add(
                file_id{4},
                source_data_ref::link(
                    fixture.link)),
            "persist Source link") ||
        !success(
            tests,
            fixture.sources.end_root(),
            "end persisted Source root")) {

        return false;
    }

    return tests.expect(
        succeeded(
            fixture.sources.finalize(
                fixture.files.size(),
                fixture.identities,
                fixture.G)),
        "finalize persisted Source Map fixture");
}

[[nodiscard]] std::uint32_t read_u32(
    const std::byte* source) noexcept {

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

void write_u32(
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

void write_u64(
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

[[nodiscard]] std::size_t section_directory_index(
    compiled_project_section section) noexcept {

    return
        static_cast<std::size_t>(
            static_cast<std::uint32_t>(section) - 1);
}

[[nodiscard]] std::size_t section_offset(
    const std::vector<std::byte>& image,
    compiled_project_section section) noexcept {

    const auto index =
        section_directory_index(
            section);

    const auto* entry =
        image.data() +
        directory_offset +
        index *
            compiled_project_directory_entry_size;

    return static_cast<std::size_t>(
        read_u64(
            entry + 8));
}

void rewrite_header_crc(
    std::vector<std::byte>& image) noexcept {

    write_u64(
        image.data() +
            header_crc_offset,
        0);

    write_u64(
        image.data() +
            header_crc_offset,
        persistence_crc64(
            std::span<const std::byte>{
                image.data(),
                compiled_project_header_size}));
}

void rewrite_directory_crc(
    std::vector<std::byte>& image) noexcept {

    write_u64(
        image.data() +
            header_directory_crc_offset,
        persistence_crc64(
            std::span<const std::byte>{
                image.data() +
                    directory_offset,
                directory_bytes}));

    rewrite_header_crc(
        image);
}

void rewrite_section_crc(
    std::vector<std::byte>& image,
    compiled_project_section section) noexcept {

    const auto index =
        section_directory_index(
            section);

    auto* entry =
        image.data() +
        directory_offset +
        index *
            compiled_project_directory_entry_size;

    const auto record_size =
        read_u32(
            entry + 4);

    const auto offset =
        read_u64(
            entry + 8);

    const auto count =
        read_u64(
            entry + 16);

    const auto byte_count =
        static_cast<std::size_t>(
            count * record_size);

    write_u64(
        entry + 24,
        persistence_crc64(
            std::span<const std::byte>{
                image.data() +
                    static_cast<std::size_t>(
                        offset),
                byte_count}));

    rewrite_directory_crc(
        image);
}

void test_round_trip(
    test_state& tests,
    const compiled_fixture& fixture,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                std::span<const std::byte>{
                    image.bytes.data(),
                    image.bytes.size()}) ==
                compiled_project_image_result::success,
            "bind canonical image")) {

        return;
    }

    if (!tests.expect(
            view.verify_contents() ==
                compiled_project_image_result::success,
            "verify canonical image")) {

        return;
    }

    tests.expect(
        view.string_count() ==
            fixture.strings.size(),
        "string count");

    tests.expect(
        view.identity_count() ==
            fixture.identities.size(),
        "identity count");

    tests.expect(
        view.type_count() ==
            fixture.G.type_count(),
        "type count");

    tests.expect(
        view.object_count() ==
            fixture.G.object_count(),
        "object count");

    tests.expect(
        view.link_count() ==
            fixture.G.link_count(),
        "link count");

    tests.expect(
        view.assign_count() ==
            fixture.assigns.size(),
        "Assign count");

    tests.expect(
        view.string(
            fixture.type_name) ==
            "Widget",
        "string slot preservation");

    tests.expect(
        view.find_string("Widget") ==
            fixture.type_name,
        "string lookup preservation");

    tests.expect(
        view.identity_root() ==
            fixture.identities.root(),
        "root identity preservation");

    tests.expect(
        view.find_identity(
            fixture.identities.root(),
            fixture.namespace_name,
            identity_kind::namespace_scope) ==
            fixture.namespace_identity,
        "namespace identity lookup");

    tests.expect(
        view.find_identity(
            fixture.namespace_identity,
            fixture.type_name,
            identity_kind::type) ==
            fixture.type_identity,
        "type identity lookup");

    tests.expect(
        view.find_type(
            fixture.type_identity) ==
            fixture.type,
        "type handle preservation");

    type_entry type_value;

    tests.expect(
        view.type(
            fixture.type,
            type_value),
        "read type");

    const auto* source_type =
        fixture.G.find(
            fixture.type);

    tests.expect(
        source_type != nullptr &&
            type_value.members.begin ==
                source_type->members.begin &&
            type_value.members.count ==
                source_type->members.count &&
            type_value.kind ==
                source_type->kind &&
            type_value.record_kind ==
                source_type->record_kind &&
            type_value.flags ==
                source_type->flags,
        "type record preservation");

    tests.expect(
        view.find_member(
            fixture.type,
            fixture.value_name) ==
            fixture.value_member,
        "zero-based member index preservation");

    member_record member_value;

    tests.expect(
        view.member(
            fixture.type,
            fixture.peer_member,
            member_value) &&
            member_value.name ==
                fixture.peer_name &&
            member_value.type ==
                fixture.reference_type &&
            member_value.access ==
                graph_member_access::private_access,
        "member record preservation");

    construction_value construction;

    tests.expect(
        view.construction(
            fixture.type,
            fixture.value_member,
            construction) &&
            construction ==
                construction_value::constant(
                    construction_kind::signed_integer,
                    42),
        "construction preservation");

    const auto value_global =
        static_cast<std::size_t>(
            type_value.members.begin) +
        fixture.value_member.value();

    tests.expect(
        view.construction_at(
            value_global,
            construction) &&
            construction ==
                construction_value::constant(
                    construction_kind::signed_integer,
                    42) &&
        !view.construction_at(
            view.member_count(),
            construction),
        "direct construction slot access");

    derived_type_record derived;

    tests.expect(
        view.derived(
            fixture.reference_type,
            derived) &&
            derived.child ==
                fixture.integer_type &&
            derived.kind ==
                derived_type_kind::lvalue_reference &&
            derived.payload == 0,
        "derived type preservation");

    tests.expect(
        view.find_derived(
            fixture.integer_type,
            derived_type_kind::lvalue_reference,
            0) ==
            fixture.reference_type &&
        !view.find_derived(
            fixture.integer_type,
            derived_type_kind::pointer,
            0),
        "persisted derived canonical index");

    tests.expect(
        view.find_object(
            fixture.left_identity) ==
            fixture.left &&
        view.find_object(
            fixture.right_identity) ==
            fixture.right &&
        view.find_object(
            fixture.scalar_identity) ==
            fixture.scalar,
        "object handle preservation");

    object_entry scalar_object;

    tests.expect(
        view.object(
            fixture.scalar,
            scalar_object) &&
            scalar_object.type ==
                fixture.integer_type &&
            scalar_object.non_default_initializer(),
        "scalar object record preservation");

    construction_value object_initial;

    tests.expect(
        view.construction(
            fixture.scalar,
            object_initial) &&
        object_initial ==
            construction_value::constant(
                construction_kind::unsigned_integer,
                7),
        "scalar object construction preservation");

    link_record link_value;

    const auto source_link =
        fixture.G.link_entries()[0];

    tests.expect(
        view.link(
            fixture.link,
            link_value) &&
            link_value.source ==
                source_link.source &&
            link_value.target ==
                source_link.target,
        "link preservation");

    tests.expect(
        view.find_link_target(
            source_link.target) ==
            fixture.link &&
        !view.find_link_target(
            source_link.source),
        "persisted link target index");

    std::string_view assign_source;
    std::string_view assign_target;

    tests.expect(
        view.assign(
            0,
            assign_source,
            assign_target) &&
            assign_source ==
                "sensor.value" &&
            assign_target ==
                "ui.value",
        "first raw Assign preservation");

    tests.expect(
        view.assign(
            1,
            assign_source,
            assign_target) &&
            assign_source ==
                "source.path" &&
            assign_target ==
                "target.path",
        "second raw Assign preservation");
}

void test_structural_corruption(
    test_state& tests,
    const compiled_test_image& image) {

    {
        auto corrupted =
            image.bytes;

        corrupted[0] ^=
            std::byte{0x01};

        compiled_project_view view;

        tests.expect(
            view.bind(
                std::span<const std::byte>{
                    corrupted.data(),
                    corrupted.size()}) ==
                compiled_project_image_result::invalid_image,
            "reject bad magic");
    }

    {
        auto corrupted =
            image.bytes;

        corrupted[
            compiled_project_header_size] ^=
                std::byte{0x01};

        compiled_project_view view;

        tests.expect(
            view.bind(
                std::span<const std::byte>{
                    corrupted.data(),
                    corrupted.size()}) ==
                compiled_project_image_result::invalid_image,
            "reject directory corruption");
    }

    if (image.bytes.size() > 1) {
        compiled_project_view view;

        tests.expect(
            view.bind(
                std::span<const std::byte>{
                    image.bytes.data(),
                    image.bytes.size() - 1}) ==
                compiled_project_image_result::invalid_image,
            "reject truncated image");
    }
}

compiled_project_image_result build_test_compiled_image(
    const compiled_fixture& fixture,
    compiled_test_image& output) {

    output = {};

    compiled_project_layout layout;

    const auto prepared = prepare_compiled_project_layout(fixture.strings,
                                                          fixture.identities,
                                                          fixture.G,
                                                          fixture.assigns,
                                                          fixture.files,
                                                          fixture.sources,
                                                          layout);

    if (prepared !=
        compiled_project_image_result::success) {

        return prepared;
    }

    try {
        output.bytes.assign(
            layout.size(),
            std::byte{0});
    }
    catch (...) {
        return compiled_project_image_result::
            failed;
    }

    const auto encoded = encode_compiled_project_image(fixture.strings,
                                                       fixture.identities,
                                                       fixture.G,
                                                       fixture.assigns,
                                                       fixture.files,
                                                       fixture.sources,
                                                       layout,
                                                       output.bytes);

    if (encoded !=
        compiled_project_image_result::success) {

        output = {};
        return encoded;
    }

    return compiled_project_image_result::
        success;
}

void test_direct_mmap_encoding(
    test_state& tests,
    const compiled_fixture& fixture) {

    compiled_project_layout layout;

    if (!tests.expect(prepare_compiled_project_layout(fixture.strings,
                                                      fixture.identities,
                                                      fixture.G,
                                                      fixture.assigns,
                                                      fixture.files,
                                                      fixture.sources,
                                                      layout) ==
                              compiled_project_image_result::success &&
                          layout.size() != 0,
                      "prepare direct compiled layout")) {

        return;
    }

    const auto path =
        std::filesystem::temp_directory_path() /
        "server_engine_v4_direct_compiled_test.bin";

    std::error_code error;
    (void)std::filesystem::remove(
        path,
        error);

    writable_file_mapping writable;

    if (!tests.expect(
            writable.create(
                path,
                layout.size()) ==
                writable_file_mapping_result::success,
            "create writable compiled mmap")) {

        return;
    }

    if (!tests.expect(encode_compiled_project_image(fixture.strings,
                                                    fixture.identities,
                                                    fixture.G,
                                                    fixture.assigns,
                                                    fixture.files,
                                                    fixture.sources,
                                                    layout,
                                                    writable.bytes()) ==
                          compiled_project_image_result::success,
                      "encode directly into compiled mmap")) {

        writable.reset();
        error.clear();
        (void)std::filesystem::remove(
            path,
            error);
        return;
    }

    compiled_project_view mapped_view;

    tests.expect(
        mapped_view.bind(
            writable.bytes()) ==
            compiled_project_image_result::success &&
        mapped_view.verify_contents() ==
            compiled_project_image_result::success,
        "validate writable compiled mmap");

    tests.expect(
        writable.flush() ==
            writable_file_mapping_result::success,
        "flush writable compiled mmap");

    writable.reset();

    read_only_file_mapping persisted;

    if (tests.expect(
            persisted.open(
                path) ==
                read_only_file_mapping_result::success,
            "reopen direct compiled mmap read-only")) {

        compiled_project_view persisted_view;

        tests.expect(
            persisted_view.bind(
                persisted.bytes()) ==
                compiled_project_image_result::success &&
            persisted_view.verify_contents() ==
                compiled_project_image_result::success,
            "validate persisted direct compiled mmap");

        tests.expect(
            persisted_view.find_type(
                fixture.type_identity) ==
                fixture.type,
            "direct compiled mmap preserves Graph lookup");

        tests.expect(
            persisted.valid() &&
                persisted_view.valid() &&
                persisted_view.find_type(
                    fixture.type_identity) ==
                    fixture.type,
            "compiled mmap preserves Graph view lifetime");
    }

    persisted.reset();

    error.clear();
    (void)std::filesystem::remove(
        path,
        error);

    tests.expect(
        !error,
        "remove direct compiled mmap test file");
}


void test_runtime_layout(
    test_state& tests,
    const compiled_fixture& fixture,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind Runtime layout image")) {

        return;
    }

    runtime_layout native;

    server_abi_configuration abi{
        abi_target::windows_x64,
        8,
    };

    if (!tests.expect(
            prepare_runtime_layout(
                view,
                abi,
                native) ==
                runtime_layout_result::success,
            "derive pack-8 Runtime layout")) {

        return;
    }

    runtime_value_layout type_layout;

    tests.expect(
        native.type(
            fixture.type,
            type_layout) &&
        type_layout.size == 16 &&
        type_layout.alignment == 8,
        "pack-8 record layout");

    type_entry type;

    if (!tests.expect(
            view.type(
                fixture.type,
                type),
            "read Runtime layout record")) {

        return;
    }

    std::uint64_t value_offset = 0;
    std::uint64_t peer_offset = 0;
    std::uint64_t left_offset = 0;
    std::uint64_t right_offset = 0;

    tests.expect(
        native.member_offset(
            static_cast<std::size_t>(
                type.members.begin) +
                fixture.value_member.value(),
            value_offset) &&
        value_offset == 0 &&
        native.member_offset(
            static_cast<std::size_t>(
                type.members.begin) +
                fixture.peer_member.value(),
            peer_offset) &&
        peer_offset == 8,
        "pack-8 direct member offsets");

    tests.expect(
        native.object_offset(
            fixture.left,
            left_offset) &&
        left_offset == 8 &&
        native.object_offset(
            fixture.right,
            right_offset) &&
        right_offset == 24 &&
        native.size() == 48 &&
        native.alignment() == 8,
        "pack-8 canonical sentinel plus dense object layout");

    abi.pack = 4;

    runtime_layout packed;

    if (!tests.expect(
            prepare_runtime_layout(
                view,
                abi,
                packed) ==
                runtime_layout_result::success,
            "derive pack-4 Runtime layout")) {

        return;
    }

    tests.expect(
        packed.type(
            fixture.type,
            type_layout) &&
        type_layout.size == 12 &&
        type_layout.alignment == 4,
        "pack-4 record layout");

    tests.expect(
        packed.member_offset(
            static_cast<std::size_t>(
                type.members.begin) +
                fixture.value_member.value(),
            value_offset) &&
        value_offset == 0 &&
        packed.member_offset(
            static_cast<std::size_t>(
                type.members.begin) +
                fixture.peer_member.value(),
            peer_offset) &&
        peer_offset == 4,
        "pack-4 direct member offsets");

    tests.expect(
        packed.object_offset(
            fixture.left,
            left_offset) &&
        left_offset == 4 &&
        packed.object_offset(
            fixture.right,
            right_offset) &&
        right_offset == 16 &&
        packed.size() == 32 &&
        packed.alignment() == 4,
        "pack-4 canonical sentinel plus dense object layout");

    std::uint64_t unconnected = 0;

    tests.expect(
        native.unconnected_offset(
            fixture.integer_type,
            unconnected) &&
        unconnected == 0,
        "Runtime layout places canonical unconnected<int> before Project objects");
}

void test_fixed_direct_materializer(
    test_state& tests) {

    string_table strings;
    identity_space identities{strings};
    graph G;
    assign_table assigns;
    file_context files;
    source_map sources;

    string_id a_name;
    string_id b_name;
    string_id in_name;
    string_id out_name;
    string_id source_name;
    string_id linked_name;
    string_id container_name;

    const auto intern =
        [&](std::string_view value,
            string_id& output) {

            return succeeded(
                strings.intern(
                    value,
                    output));
        };

    if (!tests.expect(
            intern("A", a_name) &&
            intern("B", b_name) &&
            intern("in", in_name) &&
            intern("out", out_name) &&
            intern("source", source_name) &&
            intern("linked", linked_name) &&
            intern("container", container_name),
            "prepare FIXED_DIRECT materializer strings")) {

        return;
    }

    identity_ref a_identity;
    identity_ref b_identity;
    identity_ref source_identity;
    identity_ref linked_identity;
    identity_ref container_identity;

    if (!tests.expect(
            succeeded(
                identities.resolve(
                    identities.root(),
                    a_name,
                    identity_kind::type,
                    a_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    b_name,
                    identity_kind::type,
                    b_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    source_name,
                    identity_kind::object,
                    source_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    linked_name,
                    identity_kind::object,
                    linked_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    container_name,
                    identity_kind::object,
                    container_identity)),
            "prepare FIXED_DIRECT materializer identities")) {

        return;
    }

    type_handle a_type;
    type_handle b_type;

    if (!tests.expect(
            succeeded(
                G.declare_record(
                    a_identity,
                    graph_record_kind::struct_type,
                    a_type)) &&
            succeeded(
                G.declare_record(
                    b_identity,
                    graph_record_kind::struct_type,
                    b_type)),
            "declare FIXED_DIRECT materializer records")) {

        return;
    }

    const auto integer =
        G.intrinsic(
            intrinsic_type::signed_int);

    type_ref integer_reference;

    if (!tests.expect(
            integer &&
            succeeded(
                G.derive(
                    integer,
                    derived_type_kind::lvalue_reference,
                    0,
                    integer_reference)),
            "prepare int reference type")) {

        return;
    }

    const std::array<member_record, 2>
        a_members{{
            {
                in_name,
                integer_reference,
                graph_member_access::public_access,
            },
            {
                out_name,
                integer,
                graph_member_access::public_access,
            },
        }};

    const std::array<construction_value, 2>
        a_construction{{
            construction_value::member_binding(2),
            construction_value::constant(
                construction_kind::signed_integer,
                42),
        }};

    if (!tests.expect(
            succeeded(
                G.define_record(
                    a_type,
                    graph_record_kind::struct_type,
                    a_members,
                    a_construction)),
            "define A materializer record")) {

        return;
    }

    const auto named_a =
        G.named(
            a_type);

    type_ref a_reference;

    if (!tests.expect(
            named_a &&
            succeeded(
                G.derive(
                    named_a,
                    derived_type_kind::lvalue_reference,
                    0,
                    a_reference)),
            "prepare A reference type")) {

        return;
    }

    const std::array<member_record, 2>
        b_members{{
            {
                in_name,
                a_reference,
                graph_member_access::public_access,
            },
            {
                out_name,
                named_a,
                graph_member_access::public_access,
            },
        }};

    if (!tests.expect(
            succeeded(
                G.define_record(
                    b_type,
                    graph_record_kind::struct_type,
                    b_members)),
            "define B materializer record")) {

        return;
    }

    const auto named_b =
        G.named(
            b_type);

    object_handle source_object;
    object_handle linked_object;
    object_handle container_object;

    if (!tests.expect(
            succeeded(
                G.add_object(
                    source_identity,
                    named_a,
                    source_object)) &&
            succeeded(
                G.add_object(
                    linked_identity,
                    named_a,
                    linked_object)) &&
            succeeded(
                G.add_object(
                    container_identity,
                    named_b,
                    container_object)),
            "add FIXED_DIRECT materializer objects")) {

        return;
    }

    const auto a_in =
        G.find_member(
            a_type,
            in_name);

    const auto a_out =
        G.find_member(
            a_type,
            out_name);

    link_handle link;

    if (!tests.expect(
            a_in &&
            a_out &&
            succeeded(
                G.add_link(
                    {
                        source_object,
                        a_out,
                    },
                    {
                        linked_object,
                        a_in,
                    },
                    link)),
            "add FIXED_DIRECT native reference link")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                sources.finalize(
                    files.size(),
                    identities,
                    G)),
            "finalize FIXED_DIRECT materializer Source Map")) {

        return;
    }

    compiled_project_layout persisted_layout;

    if (!tests.expect(
            prepare_compiled_project_layout(
                strings,
                identities,
                G,
                assigns,
                files,
                sources,
                persisted_layout) ==
                compiled_project_image_result::success,
            "prepare FIXED_DIRECT compiled image")) {

        return;
    }

    compiled_test_image image;

    try {
        image.bytes.assign(
            persisted_layout.size(),
            std::byte{0});
    }
    catch (...) {
        tests.expect(
            false,
            "allocate FIXED_DIRECT compiled image");
        return;
    }

    if (!tests.expect(
            encode_compiled_project_image(
                strings,
                identities,
                G,
                assigns,
                files,
                sources,
                persisted_layout,
                image.bytes) ==
                compiled_project_image_result::success,
            "encode FIXED_DIRECT compiled image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind FIXED_DIRECT compiled image")) {

        return;
    }

#if defined(_WIN32)
    const server_abi_configuration abi{
        abi_target::windows_x64,
        8,
    };
#else
    const server_abi_configuration abi{
        abi_target::posix_x64,
        8,
    };
#endif

    runtime_layout layout;

    if (!tests.expect(
            fixed_direct_host_compatible(
                abi) &&
            prepare_runtime_layout(
                view,
                abi,
                layout) ==
                runtime_layout_result::success,
            "prepare FIXED_DIRECT Runtime layout")) {

        return;
    }

    std::vector<std::byte> runtime;

    try {
        runtime.assign(
            static_cast<std::size_t>(
                layout.size()),
            std::byte{0xcc});
    }
    catch (...) {
        tests.expect(
            false,
            "allocate FIXED_DIRECT test Runtime");
        return;
    }

    if (!tests.expect(
            materialize_fixed_direct(
                view,
                layout,
                abi,
                runtime) ==
                fixed_direct_materialization_result::success,
            "materialize FIXED_DIRECT Runtime image")) {

        return;
    }

    std::uint64_t unconnected_int = 0;
    std::uint64_t unconnected_a = 0;
    std::uint64_t source_offset = 0;
    std::uint64_t linked_offset = 0;
    std::uint64_t container_offset = 0;

    type_entry a_record;
    type_entry b_record;

    std::uint64_t a_in_offset = 0;
    std::uint64_t a_out_offset = 0;
    std::uint64_t b_in_offset = 0;
    std::uint64_t b_out_offset = 0;

    if (!tests.expect(
            layout.unconnected_offset(
                integer,
                unconnected_int) &&
            layout.unconnected_offset(
                named_a,
                unconnected_a) &&
            layout.object_offset(
                source_object,
                source_offset) &&
            layout.object_offset(
                linked_object,
                linked_offset) &&
            layout.object_offset(
                container_object,
                container_offset) &&
            view.type(
                a_type,
                a_record) &&
            view.type(
                b_type,
                b_record) &&
            layout.member_offset(
                a_record.members.begin +
                    a_in.value(),
                a_in_offset) &&
            layout.member_offset(
                a_record.members.begin +
                    a_out.value(),
                a_out_offset) &&
            layout.member_offset(
                b_record.members.begin,
                b_in_offset) &&
            layout.member_offset(
                b_record.members.begin + 1,
                b_out_offset),
            "query FIXED_DIRECT Runtime offsets")) {

        return;
    }

    const auto base =
        reinterpret_cast<std::uintptr_t>(
            runtime.data());

    const auto read_address =
        [&](std::uint64_t offset) {
            std::uintptr_t value = 0;

            std::memcpy(
                &value,
                runtime.data() +
                    static_cast<std::size_t>(
                        offset),
                sizeof(value));

            return value;
        };

    const auto read_int =
        [&](std::uint64_t offset) {
            int value = 0;

            std::memcpy(
                &value,
                runtime.data() +
                    static_cast<std::size_t>(
                        offset),
                sizeof(value));

            return value;
        };

    tests.expect(
        read_address(
            unconnected_a +
                a_in_offset) ==
            base +
                unconnected_int &&
        read_int(
            unconnected_a +
                a_out_offset) == 0,
        "unconnected<A> recursively binds A.in to unconnected<int> and zeroes A.out");

    tests.expect(
        read_address(
            source_offset +
                a_in_offset) ==
            base +
                source_offset +
                a_out_offset &&
        read_int(
            source_offset +
                a_out_offset) == 42,
        "ordinary A uses its type-level A.in -> A.out default binding");

    tests.expect(
        read_address(
            linked_offset +
                a_in_offset) ==
            base +
                source_offset +
                a_out_offset,
        "Graph link overrides linked.A.in default and materializes source.A.out");

    tests.expect(
        read_address(
            container_offset +
                b_in_offset) ==
            base +
                unconnected_a,
        "B.in binds to canonical unconnected<A>");

    tests.expect(
        read_address(
            container_offset +
                b_out_offset +
                a_in_offset) ==
            base +
                container_offset +
                b_out_offset +
                a_out_offset &&
        read_int(
            container_offset +
                b_out_offset +
                a_out_offset) == 42,
        "B.out is a normal nested A and keeps A.in -> A.out default binding");
}



void test_fixed_direct_unplanned_reference_chain(
    test_state& tests) {

    string_table strings;
    identity_space identities{strings};
    graph G;
    assign_table assigns;
    file_context files;
    source_map sources;

    string_id type_name;
    string_id r0_name;
    string_id r1_name;
    string_id r2_name;
    string_id out_name;
    string_id object_name;

    if (!tests.expect(
            succeeded(strings.intern("ReferenceChain", type_name)) &&
            succeeded(strings.intern("r0", r0_name)) &&
            succeeded(strings.intern("r1", r1_name)) &&
            succeeded(strings.intern("r2", r2_name)) &&
            succeeded(strings.intern("out", out_name)) &&
            succeeded(strings.intern("x", object_name)),
            "prepare unplanned reference-chain strings")) {

        return;
    }

    identity_ref type_identity;
    identity_ref object_identity;

    if (!tests.expect(
            succeeded(
                identities.resolve(
                    identities.root(),
                    type_name,
                    identity_kind::type,
                    type_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    object_name,
                    identity_kind::object,
                    object_identity)),
            "prepare unplanned reference-chain identities")) {

        return;
    }

    type_handle type;

    if (!tests.expect(
            succeeded(
                G.declare_record(
                    type_identity,
                    graph_record_kind::struct_type,
                    type)),
            "declare unplanned reference-chain record")) {

        return;
    }

    const auto integer =
        G.intrinsic(
            intrinsic_type::signed_int);

    type_ref integer_reference;

    if (!tests.expect(
            integer &&
            succeeded(
                G.derive(
                    integer,
                    derived_type_kind::lvalue_reference,
                    0,
                    integer_reference)),
            "prepare unplanned reference-chain type")) {

        return;
    }

    const std::array<member_record, 4>
        members{{
            {
                r0_name,
                integer_reference,
                graph_member_access::public_access,
            },
            {
                r1_name,
                integer_reference,
                graph_member_access::public_access,
            },
            {
                r2_name,
                integer_reference,
                graph_member_access::public_access,
            },
            {
                out_name,
                integer,
                graph_member_access::public_access,
            },
        }};

    const std::array<construction_value, 4>
        construction{{
            construction_value::member_binding(2),
            construction_value::member_binding(3),
            construction_value::member_binding(4),
            construction_value::constant(
                construction_kind::signed_integer,
                17),
        }};

    if (!tests.expect(
            succeeded(
                G.define_record(
                    type,
                    graph_record_kind::struct_type,
                    members,
                    construction)),
            "define unplanned reference-chain record")) {

        return;
    }

    object_handle object;

    if (!tests.expect(
            succeeded(
                G.add_object(
                    object_identity,
                    G.named(type),
                    object)) &&
            succeeded(
                sources.finalize(
                    files.size(),
                    identities,
                    G)),
            "prepare unplanned reference-chain object")) {

        return;
    }

    compiled_project_layout persisted;

    if (!tests.expect(
            prepare_compiled_project_layout(
                strings,
                identities,
                G,
                assigns,
                files,
                sources,
                persisted) ==
                compiled_project_image_result::success,
            "prepare unplanned reference-chain compiled image")) {

        return;
    }

    compiled_test_image image;

    try {
        image.bytes.assign(
            persisted.size(),
            std::byte{0});
    }
    catch (...) {
        tests.expect(
            false,
            "allocate unplanned reference-chain image");
        return;
    }

    if (!tests.expect(
            encode_compiled_project_image(
                strings,
                identities,
                G,
                assigns,
                files,
                sources,
                persisted,
                image.bytes) ==
                compiled_project_image_result::success,
            "encode unplanned reference-chain image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind unplanned reference-chain image")) {

        return;
    }

#if defined(_WIN32)
    const server_abi_configuration abi{
        abi_target::windows_x64,
        8,
    };
#else
    const server_abi_configuration abi{
        abi_target::posix_x64,
        8,
    };
#endif

    runtime_layout layout;

    if (!tests.expect(
            prepare_runtime_layout(
                view,
                abi,
                layout) ==
                runtime_layout_result::success,
            "prepare unplanned reference-chain Runtime layout")) {

        return;
    }

    std::vector<std::byte> runtime;

    try {
        runtime.assign(
            static_cast<std::size_t>(
                layout.size()),
            std::byte{0xcc});
    }
    catch (...) {
        tests.expect(
            false,
            "allocate unplanned reference-chain Runtime");
        return;
    }

    if (!tests.expect(
            materialize_fixed_direct(
                view,
                layout,
                abi,
                runtime) ==
                fixed_direct_materialization_result::success,
            "materialize unplanned reference chain")) {

        return;
    }

    type_entry record;
    std::uint64_t object_offset = 0;
    std::uint64_t offsets[4]{};

    if (!tests.expect(
            view.type(
                type,
                record) &&
            layout.object_offset(
                object,
                object_offset),
            "query unplanned reference-chain record")) {

        return;
    }

    for (std::size_t index = 0;
         index < 4;
         ++index) {

        if (!tests.expect(
                layout.member_offset(
                    static_cast<std::size_t>(
                        record.members.begin) +
                        index,
                    offsets[index]),
                "query unplanned reference-chain member offset")) {

            return;
        }
    }

    const auto base =
        reinterpret_cast<std::uintptr_t>(
            runtime.data());

    const auto expected =
        base +
        static_cast<std::uintptr_t>(
            object_offset +
            offsets[3]);

    const auto read_address =
        [&](std::uint64_t offset) {
            std::uintptr_t value = 0;

            std::memcpy(
                &value,
                runtime.data() +
                    static_cast<std::size_t>(
                        offset),
                sizeof(value));

            return value;
        };

    int out = 0;

    std::memcpy(
        &out,
        runtime.data() +
            static_cast<std::size_t>(
                object_offset +
                offsets[3]),
        sizeof(out));

    tests.expect(
        read_address(
            object_offset +
                offsets[0]) ==
                expected &&
        read_address(
            object_offset +
                offsets[1]) ==
                expected &&
        read_address(
            object_offset +
                offsets[2]) ==
                expected &&
        out == 17,
        "unplanned reference chain resolves every hop to final value");
}

void test_fixed_direct_link_prebind(
    test_state& tests) {

    struct fixture final {
        fixture()
            : identities(strings) {
        }

        string_table strings;
        identity_space identities;
        graph G;
        assign_table assigns;
        file_context files;
        source_map sources;

        string_id type_name{};
        string_id out_name{};
        string_id in_name{};
        string_id a_name{};
        string_id b_name{};
        string_id c_name{};

        identity_ref type_identity{};
        identity_ref a_identity{};
        identity_ref b_identity{};
        identity_ref c_identity{};

        type_handle type{};
        type_ref integer{};
        type_ref integer_reference{};
        type_ref named_type{};

        member_index out{};
        member_index in{};

        object_handle a{};
        object_handle b{};
        object_handle c{};
    };

    const auto prepare =
        [&](fixture& value) {
            const auto intern =
                [&](std::string_view spelling,
                    string_id& output) {

                    return succeeded(
                        value.strings.intern(
                            spelling,
                            output));
                };

            if (!intern("T", value.type_name) ||
                !intern("out", value.out_name) ||
                !intern("in", value.in_name) ||
                !intern("a", value.a_name) ||
                !intern("b", value.b_name) ||
                !intern("c", value.c_name) ||
                !succeeded(
                    value.identities.resolve(
                        value.identities.root(),
                        value.type_name,
                        identity_kind::type,
                        value.type_identity)) ||
                !succeeded(
                    value.identities.resolve(
                        value.identities.root(),
                        value.a_name,
                        identity_kind::object,
                        value.a_identity)) ||
                !succeeded(
                    value.identities.resolve(
                        value.identities.root(),
                        value.b_name,
                        identity_kind::object,
                        value.b_identity)) ||
                !succeeded(
                    value.identities.resolve(
                        value.identities.root(),
                        value.c_name,
                        identity_kind::object,
                        value.c_identity)) ||
                !succeeded(
                    value.G.declare_record(
                        value.type_identity,
                        graph_record_kind::struct_type,
                        value.type))) {

                return false;
            }

            value.integer =
                value.G.intrinsic(
                    intrinsic_type::signed_int);

            if (!value.integer ||
                !succeeded(
                    value.G.derive(
                        value.integer,
                        derived_type_kind::lvalue_reference,
                        0,
                        value.integer_reference))) {

                return false;
            }

            const std::array<member_record, 2>
                members{{
                    {
                        value.out_name,
                        value.integer,
                        graph_member_access::public_access,
                    },
                    {
                        value.in_name,
                        value.integer_reference,
                        graph_member_access::public_access,
                    },
                }};

            if (!succeeded(
                    value.G.define_record(
                        value.type,
                        graph_record_kind::struct_type,
                        members))) {

                return false;
            }

            value.named_type =
                value.G.named(
                    value.type);

            value.out =
                value.G.find_member(
                    value.type,
                    value.out_name);

            value.in =
                value.G.find_member(
                    value.type,
                    value.in_name);

            return value.named_type &&
                value.out &&
                value.in &&
                succeeded(
                    value.G.add_object(
                        value.a_identity,
                        value.named_type,
                        value.a)) &&
                succeeded(
                    value.G.add_object(
                        value.b_identity,
                        value.named_type,
                        value.b)) &&
                succeeded(
                    value.G.add_object(
                        value.c_identity,
                        value.named_type,
                        value.c));
        };

    const auto encode =
        [&](fixture& value,
            compiled_test_image& image) {

            if (!succeeded(
                    value.sources.finalize(
                        value.files.size(),
                        value.identities,
                        value.G))) {

                return false;
            }

            compiled_project_layout persisted;

            if (prepare_compiled_project_layout(
                    value.strings,
                    value.identities,
                    value.G,
                    value.assigns,
                    value.files,
                    value.sources,
                    persisted) !=
                compiled_project_image_result::
                    success) {

                return false;
            }

            try {
                image.bytes.assign(
                    persisted.size(),
                    std::byte{0});
            }
            catch (...) {
                return false;
            }

            return encode_compiled_project_image(
                value.strings,
                value.identities,
                value.G,
                value.assigns,
                value.files,
                value.sources,
                persisted,
                image.bytes) ==
                    compiled_project_image_result::
                        success;
        };

#if defined(_WIN32)
    const server_abi_configuration abi{
        abi_target::windows_x64,
        8,
    };
#else
    const server_abi_configuration abi{
        abi_target::posix_x64,
        8,
    };
#endif

    const auto prepare_runtime =
        [&](compiled_test_image& image,
            compiled_project_view& view,
            runtime_layout& layout,
            std::vector<std::byte>& runtime) {

            if (view.bind(
                    image.bytes) !=
                        compiled_project_image_result::
                            success ||
                prepare_runtime_layout(
                    view,
                    abi,
                    layout) !=
                        runtime_layout_result::
                            success) {

                return false;
            }

            try {
                runtime.assign(
                    static_cast<std::size_t>(
                        layout.size()),
                    std::byte{0xcc});
            }
            catch (...) {
                return false;
            }

            return true;
        };

    {
        fixture value;

        if (!tests.expect(
                prepare(value),
                "prepare FIXED_DIRECT link dependency fixture")) {

            return;
        }

        link_handle first;
        link_handle second;

        if (!tests.expect(
                succeeded(
                    value.G.add_link(
                        {
                            value.b,
                            value.in,
                        },
                        {
                            value.a,
                            value.in,
                        },
                        first)) &&
                succeeded(
                    value.G.add_link(
                        {
                            value.c,
                            value.out,
                        },
                        {
                            value.b,
                            value.in,
                        },
                        second)),
                "build later-link reference dependency")) {

            return;
        }

        compiled_test_image image;

        if (!tests.expect(
                encode(
                    value,
                    image),
                "encode FIXED_DIRECT link dependency image")) {

            return;
        }

        compiled_project_view view;
        runtime_layout layout;
        std::vector<std::byte> runtime;

        if (!tests.expect(
                prepare_runtime(
                    image,
                    view,
                    layout,
                    runtime),
                "prepare FIXED_DIRECT link dependency Runtime")) {

            return;
        }

        if (!tests.expect(
                materialize_fixed_direct(
                    view,
                    layout,
                    abi,
                    runtime) ==
                    fixed_direct_materialization_result::
                        success,
                "materialize later-link reference dependency")) {

            return;
        }

        type_entry record;
        std::uint64_t out_offset = 0;
        std::uint64_t in_offset = 0;
        std::uint64_t a_offset = 0;
        std::uint64_t b_offset = 0;
        std::uint64_t c_offset = 0;

        if (!tests.expect(
                view.type(
                    value.type,
                    record) &&
                layout.member_offset(
                    static_cast<std::size_t>(
                        record.members.begin) +
                        value.out.value(),
                    out_offset) &&
                layout.member_offset(
                    static_cast<std::size_t>(
                        record.members.begin) +
                        value.in.value(),
                    in_offset) &&
                layout.object_offset(
                    value.a,
                    a_offset) &&
                layout.object_offset(
                    value.b,
                    b_offset) &&
                layout.object_offset(
                    value.c,
                    c_offset),
                "query FIXED_DIRECT dependency Runtime offsets")) {

            return;
        }

        const auto base =
            reinterpret_cast<std::uintptr_t>(
                runtime.data());

        const auto read_address =
            [&](std::uint64_t offset) {
                std::uintptr_t result = 0;

                std::memcpy(
                    &result,
                    runtime.data() +
                        static_cast<std::size_t>(
                            offset),
                    sizeof(result));

                return result;
            };

        const auto c_out =
            base +
            static_cast<std::uintptr_t>(
                c_offset + out_offset);

        tests.expect(
            read_address(
                a_offset +
                    in_offset) ==
                    c_out &&
            read_address(
                b_offset +
                    in_offset) ==
                    c_out,
            "dense link prebind resolves reference dependency through later link");
    }

    {
        fixture value;

        if (!tests.expect(
                prepare(value),
                "prepare FIXED_DIRECT link cycle fixture")) {

            return;
        }

        link_handle first;
        link_handle second;

        if (!tests.expect(
                succeeded(
                    value.G.add_link(
                        {
                            value.b,
                            value.in,
                        },
                        {
                            value.a,
                            value.in,
                        },
                        first)) &&
                succeeded(
                    value.G.add_link(
                        {
                            value.a,
                            value.in,
                        },
                        {
                            value.b,
                            value.in,
                        },
                        second)),
                "build FIXED_DIRECT link dependency cycle")) {

            return;
        }

        compiled_test_image image;

        if (!tests.expect(
                encode(
                    value,
                    image),
                "encode FIXED_DIRECT link cycle image")) {

            return;
        }

        compiled_project_view view;
        runtime_layout layout;
        std::vector<std::byte> runtime;

        if (!tests.expect(
                prepare_runtime(
                    image,
                    view,
                    layout,
                    runtime),
                "prepare FIXED_DIRECT link cycle Runtime")) {

            return;
        }

        tests.expect(
            materialize_fixed_direct(
                view,
                layout,
                abi,
                runtime) ==
                fixed_direct_materialization_result::
                    invalid_input,
            "FIXED_DIRECT rejects Graph link dependency cycle");
    }

    {
        fixture value;

        if (!tests.expect(
                prepare(value),
                "prepare FIXED_DIRECT duplicate link target fixture")) {

            return;
        }

        link_handle first;
        link_handle second;

        if (!tests.expect(
                succeeded(
                    value.G.add_link(
                        {
                            value.c,
                            value.out,
                        },
                        {
                            value.a,
                            value.in,
                        },
                        first)) &&
                succeeded(
                    value.G.add_link(
                        {
                            value.c,
                            value.out,
                        },
                        {
                            value.b,
                            value.in,
                        },
                        second)),
                "build two distinct FIXED_DIRECT link targets")) {

            return;
        }

        compiled_test_image image;

        if (!tests.expect(
                encode(
                    value,
                    image),
                "encode FIXED_DIRECT duplicate-target baseline")) {

            return;
        }

        compiled_project_view view;
        runtime_layout layout;
        std::vector<std::byte> runtime;

        if (!tests.expect(
                prepare_runtime(
                    image,
                    view,
                    layout,
                    runtime),
                "prepare FIXED_DIRECT duplicate-target Runtime")) {

            return;
        }

        constexpr std::size_t persisted_link_size =
            sizeof(std::uint32_t) * 4;

        const auto links =
            section_offset(
                image.bytes,
                compiled_project_section::
                    links);

        auto* first_link =
            image.bytes.data() +
            links;

        auto* second_link =
            first_link +
            persisted_link_size;

        std::memcpy(
            second_link + 8,
            first_link + 8,
            8);

        tests.expect(
            materialize_fixed_direct(
                view,
                layout,
                abi,
                runtime) ==
                fixed_direct_materialization_result::
                    invalid_input,
            "FIXED_DIRECT rejects duplicate persisted Graph link target without cold audit");
    }
}

void test_runtime_layout_tail_alignment(
    test_state& tests) {

    compiled_fixture fixture;

    string_id wide_name;
    string_id tail_name;

    identity_ref wide_identity;
    identity_ref tail_identity;

    if (!tests.expect(
            succeeded(
                fixture.strings.intern(
                    "wide",
                    wide_name)) &&
            succeeded(
                fixture.strings.intern(
                    "tail",
                    tail_name)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    wide_name,
                    identity_kind::object,
                    wide_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    tail_name,
                    identity_kind::object,
                    tail_identity)),
            "prepare Runtime tail-alignment identities")) {

        return;
    }

    object_handle wide;
    object_handle tail;

    if (!tests.expect(
            succeeded(
                fixture.G.add_object(
                    wide_identity,
                    fixture.G.intrinsic(
                        intrinsic_type::double_type),
                    wide)) &&
            succeeded(
                fixture.G.add_object(
                    tail_identity,
                    fixture.G.intrinsic(
                        intrinsic_type::char_type),
                    tail)),
            "prepare Runtime tail-alignment objects")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.sources.finalize(
                    fixture.files.size(),
                    fixture.identities,
                    fixture.G)),
            "finalize empty Runtime tail-alignment Source Map")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::success,
            "encode Runtime tail-alignment image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind Runtime tail-alignment image")) {

        return;
    }

    runtime_layout layout;

    const server_abi_configuration abi{
        abi_target::windows_x64,
        8,
    };

    if (!tests.expect(
            prepare_runtime_layout(
                view,
                abi,
                layout) ==
                runtime_layout_result::success,
            "derive Runtime tail-alignment layout")) {

        return;
    }

    std::uint64_t wide_offset = 0;
    std::uint64_t tail_offset = 0;

    tests.expect(
        layout.object_offset(
            wide,
            wide_offset) &&
        layout.object_offset(
            tail,
            tail_offset) &&
        wide_offset == 0 &&
        tail_offset == 8 &&
        layout.alignment() == 8 &&
        layout.size() == 16,
        "Runtime total size is aligned to maximum object alignment");
}

void test_hot_cold_boundary(
    test_state& tests,
    const compiled_test_image& image) {

    auto corrupted =
        image.bytes;

    const auto offset =
        section_offset(
            corrupted,
            compiled_project_section::string_bytes);

    corrupted[offset] ^=
        std::byte{0x01};

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                std::span<const std::byte>{
                    corrupted.data(),
                    corrupted.size()}) ==
                compiled_project_image_result::success,
            "bind does not scan section payload CRC")) {

        return;
    }

    tests.expect(
        view.verify_contents() ==
            compiled_project_image_result::invalid_image,
        "cold audit detects section payload corruption");
}

void test_semantic_corruption(
    test_state& tests,
    const compiled_fixture& fixture,
    const compiled_test_image& image) {

    auto corrupted =
        image.bytes;

    const auto offset =
        section_offset(
            corrupted,
            compiled_project_section::graph_identity_index) +
        static_cast<std::size_t>(
            fixture.type_identity.slot()) *
            sizeof(std::uint32_t);

    write_u32(
        corrupted.data() + offset,
        0);

    rewrite_section_crc(
        corrupted,
        compiled_project_section::graph_identity_index);

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                std::span<const std::byte>{
                    corrupted.data(),
                    corrupted.size()}) ==
                compiled_project_image_result::success,
            "bind accepts CRC-consistent semantic corruption")) {

        return;
    }

    tests.expect(
        view.verify_contents() ==
            compiled_project_image_result::invalid_image,
        "cold audit detects graph identity corruption");

    const auto* type =
        fixture.G.find(
            fixture.type);

    if (!tests.expect(
            type != nullptr,
            "read fixture type for link semantic corruption")) {

        return;
    }

    auto invalid_link_type =
        image.bytes;

    const auto member_slot =
        static_cast<std::size_t>(
            type->members.begin) +
        fixture.peer_member.value();

    const auto member_type_offset =
        section_offset(
            invalid_link_type,
            compiled_project_section::members) +
        member_slot *
            sizeof(member_record) +
        offsetof(
            member_record,
            type);

    write_u32(
        invalid_link_type.data() +
            member_type_offset,
        fixture.integer_type.value());

    rewrite_section_crc(
        invalid_link_type,
        compiled_project_section::members);

    compiled_project_view semantic_view;

    tests.expect(
        semantic_view.bind(
            invalid_link_type) ==
                compiled_project_image_result::success &&
        semantic_view.verify_contents() ==
            compiled_project_image_result::invalid_image,
        "cold audit detects link target reference-type corruption");
}


void test_string_intern_growth_and_aliasing(test_state& tests) {
    string_table strings;
    const std::string text(4096, 'x');
    string_id full, suffix, duplicate;
    if (!tests.expect(succeeded(strings.intern(text, full)), "intern long spelling")) {
        return;
    }
    tests.expect(succeeded(strings.intern(strings.get(full).substr(1), suffix)) &&
                 full != suffix && strings.get(full) == text &&
                 strings.get(suffix) == std::string_view{text}.substr(1),
                 "single-hash miss preserves an aliased spelling through arena growth");
    tests.expect(succeeded(strings.intern(text, duplicate)) && duplicate == full,
                 "single-hash hit preserves canonical string ID");
    for (unsigned index = 0; index < 1024; ++index) {
        string_id id;
        const auto name = "name_" + std::to_string(index);
        tests.expect(succeeded(strings.intern(name, id)) && strings.find(name) == id,
                     "lookup and insertion agree through index growth");
    }
    tests.expect(strings.find(text) == full && strings.find(std::string_view{text}.substr(1)) == suffix &&
                 !strings.find("missing") && !strings.find({}),
                 "existing and absent strings remain correct after rehash");
    tests.expect(strings.intern({}, duplicate) == server_status::project_configuration_invalid && !duplicate,
                 "empty intern rejects input and clears output");
}

void test_build_lineage_overlays(
    test_state& tests,
    const compiled_fixture& fixture,
    const compiled_test_image& image) {

    compiled_project_view baseline;

    if (!tests.expect(
            baseline.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind BUILD semantic baseline")) {
        return;
    }

    string_table strings;
    identity_space identities{
        strings};

    if (!tests.expect(
            succeeded(
                strings.bind_baseline(
                    baseline)),
            "bind string BUILD baseline") ||
        !tests.expect(
            succeeded(
                identities.bind_baseline(
                    baseline)),
            "bind identity BUILD baseline")) {
        return;
    }

    tests.expect(
        strings.size() ==
            baseline.string_count() &&
        strings.byte_size() ==
            baseline.string_byte_size(),
        "BUILD string overlay starts without baseline copy");

    tests.expect(
        identities.size() ==
            baseline.identity_count(),
        "BUILD identity overlay starts without baseline copy");

    tests.expect(
        strings.find("Widget") ==
            fixture.type_name &&
        strings.get(
            fixture.type_name) ==
            "Widget",
        "BUILD string lookup preserves baseline string_id");

    tests.expect(
        identities.find(
            fixture.namespace_identity,
            fixture.type_name,
            identity_kind::type) ==
            fixture.type_identity,
        "BUILD identity lookup preserves baseline identity_ref");

    identity_record persisted_type;

    tests.expect(
        identities.record(
            fixture.type_identity,
            persisted_type) &&
        persisted_type.parent ==
            fixture.namespace_identity &&
        persisted_type.name ==
            fixture.type_name,
        "BUILD identity record reads mmap baseline");

    string_id repeated_type;

    tests.expect(
        succeeded(
            strings.intern(
                "Widget",
                repeated_type)) &&
        repeated_type ==
            fixture.type_name,
        "BUILD reintern keeps persisted string_id");

    string_id appended_name;

    if (!tests.expect(
            succeeded(
                strings.intern(
                    "post_build",
                    appended_name)) &&
            appended_name.value() ==
                baseline.string_count() + 1,
            "BUILD appends string_id after baseline")) {
        return;
    }

    identity_ref repeated_type_identity;

    tests.expect(
        succeeded(
            identities.resolve(
                fixture.namespace_identity,
                fixture.type_name,
                identity_kind::type,
                repeated_type_identity)) &&
        repeated_type_identity ==
            fixture.type_identity,
        "BUILD resolve keeps persisted identity_ref");

    identity_ref appended_identity;

    if (!tests.expect(
            succeeded(
                identities.resolve(
                    fixture.namespace_identity,
                    appended_name,
                    identity_kind::object,
                    appended_identity)) &&
            appended_identity.slot() ==
                baseline.identity_count() + 1,
            "BUILD appends identity_ref after baseline")) {
        return;
    }

    tests.expect(
        identities.at_slot(
            fixture.type_identity.slot()) ==
            fixture.type_identity &&
        identities.at_slot(
            appended_identity.slot()) ==
            appended_identity,
        "BUILD identity slot view spans baseline and overlay");

    graph_delta G;

    if (!tests.expect(
            succeeded(
                G.bind_baseline(
                    baseline)),
            "bind BUILD graph_delta baseline")) {
        return;
    }

    tests.expect(
        G.baseline_bound() &&
        G.type_count() ==
            baseline.type_count() &&
        G.live_type_count() ==
            baseline.type_count() &&
        G.member_count() ==
            baseline.member_count() &&
        G.object_count() ==
            baseline.object_count() &&
        G.live_object_count() ==
            baseline.object_count() &&
        G.link_count() ==
            baseline.link_count() &&
        G.live_link_count() ==
            baseline.link_count() &&
        G.derived_type_count() ==
            baseline.derived_type_count(),
        "BUILD graph_delta binds baseline without dense reconstruction");

    type_entry baseline_type;

    tests.expect(
        G.find_type(
            fixture.type_identity) ==
            fixture.type &&
        G.type(
            fixture.type,
            baseline_type) &&
        baseline_type.defined(),
        "BUILD graph_delta reads unchanged type directly from baseline");

    type_ref repeated_reference;

    tests.expect(
        succeeded(
            G.derive(
                fixture.integer_type,
                derived_type_kind::lvalue_reference,
                0,
                repeated_reference)) &&
        repeated_reference ==
            fixture.reference_type,
        "BUILD graph_delta reuses persisted derived slot");

    if (!tests.expect(
            succeeded(
                G.clear_definition(
                    fixture.type)),
            "BUILD graph_delta clears baseline type definition")) {
        return;
    }

    const std::array<member_record, 2>
        replacement_members{{
            {
                fixture.value_name,
                fixture.integer_type,
                graph_member_access::public_access,
            },
            {
                fixture.peer_name,
                fixture.reference_type,
                graph_member_access::private_access,
            },
        }};

    const std::array<construction_value, 2>
        replacement_construction{{
            construction_value::constant(
                construction_kind::signed_integer,
                99),
            construction_value{},
        }};

    if (!tests.expect(
            succeeded(
                G.define_record(
                    fixture.type,
                    graph_record_kind::struct_type,
                    replacement_members,
                    replacement_construction)),
            "BUILD graph_delta replaces baseline type definition")) {
        return;
    }

    construction_value replacement_value;

    tests.expect(
        G.find_type(
            fixture.type_identity) ==
            fixture.type &&
        G.construction(
            fixture.type,
            fixture.value_member,
            replacement_value) &&
        replacement_value ==
            construction_value::constant(
                construction_kind::signed_integer,
                99),
        "graph_delta type replacement preserves type_handle");

    const auto old_live_objects =
        G.live_object_count();

    if (!tests.expect(
            succeeded(
                G.retire(
                    fixture.scalar)) &&
            !G.contains(
                fixture.scalar) &&
            !G.find_object(
                fixture.scalar_identity) &&
            G.live_object_count() + 1 ==
                old_live_objects,
            "BUILD graph_delta tombstones baseline object")) {
        return;
    }

    object_handle restored_scalar;

    if (!tests.expect(
            succeeded(
                G.add_object(
                    fixture.scalar_identity,
                    fixture.integer_type,
                    restored_scalar,
                    graph_object_non_default_initializer,
                    construction_value::constant(
                        construction_kind::unsigned_integer,
                        9))) &&
            restored_scalar ==
                fixture.scalar,
            "BUILD graph_delta reuses retired scalar object slot")) {
        return;
    }

    construction_value restored_initial;

    object_entry restored_entry;

    tests.expect(
        G.object(
            restored_scalar,
            restored_entry) &&
        restored_entry.non_default_initializer() &&
        restored_entry.construction_slot() >
            baseline.object_construction_count() &&
        G.construction(
            restored_scalar,
            restored_initial) &&
        restored_initial ==
            construction_value::constant(
                construction_kind::unsigned_integer,
                9),
        "BUILD graph_delta patches scalar object construction without baseline copy");

    const auto old_live_links =
        G.live_link_count();

    if (!tests.expect(
            succeeded(
                G.retire(
                    fixture.link)) &&
            !G.contains(
                fixture.link) &&
            G.live_link_count() + 1 ==
                old_live_links,
            "BUILD graph_delta tombstones baseline link")) {
        return;
    }

    link_handle restored_link;

    const link_record old_link =
        fixture.G.link_entries()[0];

    tests.expect(
        succeeded(
            G.add_link(
                old_link.source,
                old_link.target,
                restored_link)) &&
        restored_link ==
            fixture.link &&
        G.live_link_count() ==
            old_live_links,
        "BUILD graph_delta reuses retired link target slot");

    object_handle appended_object;

    if (!tests.expect(
            succeeded(
                G.add_object(
                    appended_identity,
                    fixture.named_type,
                    appended_object)) &&
            appended_object.value() ==
                baseline.object_count() + 1 &&
            G.find_object(
                appended_identity) ==
                appended_object,
            "BUILD graph_delta appends new object after baseline slots")) {
        return;
    }

    type_ref appended_derived;

    tests.expect(
        succeeded(
            G.derive(
                fixture.named_type,
                derived_type_kind::lvalue_reference,
                0,
                appended_derived)) &&
        appended_derived.kind() ==
            type_ref_kind::derived &&
        appended_derived.payload() ==
            baseline.derived_type_count() + 1,
        "BUILD graph_delta appends derived type after baseline slots");

    link_handle appended_link;

    tests.expect(
        succeeded(
            G.add_link(
                {
                    fixture.left,
                    fixture.value_member,
                },
                {
                    appended_object,
                    fixture.peer_member,
                },
                appended_link)) &&
        appended_link.value() ==
            baseline.link_count() + 1,
        "BUILD graph_delta appends link after baseline slots");

    compiled_project_layout merged_layout;

    if (!tests.expect(prepare_compiled_project_layout(strings,
                                                      identities,
                                                      fixture.G,
                                                      fixture.assigns,
                                                      fixture.files,
                                                      fixture.sources,
                                                      merged_layout) ==
                          compiled_project_image_result::success,
                      "prepare merged baseline+overlay compiled image")) {
        return;
    }

    std::vector<std::byte> merged;

    try {
        merged.assign(
            merged_layout.size(),
            std::byte{0});
    }
    catch (...) {
        tests.expect(
            false,
            "allocate merged overlay test image");
        return;
    }

    if (!tests.expect(encode_compiled_project_image(strings,
                                                    identities,
                                                    fixture.G,
                                                    fixture.assigns,
                                                    fixture.files,
                                                    fixture.sources,
                                                    merged_layout,
                                                    merged) ==
                          compiled_project_image_result::success,
                      "encode merged baseline+overlay compiled image")) {
        return;
    }

    compiled_project_view merged_view;

    if (!tests.expect(
            merged_view.bind(
                merged) ==
                compiled_project_image_result::success &&
            merged_view.verify_contents() ==
                compiled_project_image_result::success,
            "validate merged baseline+overlay compiled image")) {
        return;
    }

    tests.expect(
        merged_view.find_string(
            "Widget") ==
            fixture.type_name &&
        merged_view.find_string(
            "post_build") ==
            appended_name,
        "merged compiled image preserves and appends string IDs");

    tests.expect(
        merged_view.find_identity(
            fixture.namespace_identity,
            fixture.type_name,
            identity_kind::type) ==
            fixture.type_identity &&
        merged_view.find_identity(
            fixture.namespace_identity,
            appended_name,
            identity_kind::object) ==
            appended_identity,
        "merged compiled image preserves and appends identity refs");
}

void test_persisted_sources(test_state &tests, const compiled_test_image &image) {
    compiled_project_view view;
    if (!tests.expect(view.bind(image.bytes) == compiled_project_image_result::success,
                      "bind persisted provenance"))
        return;
    source_map_range root, file_range;
    std::string_view path;
    file_kind kind;
    tests.expect(
        view.source_file_count() == 4 &&
        view.source_contribution_count() == 6 &&
        view.source_root(
            file_id{1},
            root) &&
        root.count == 1 &&
        view.source_file(
            file_id{2},
            path,
            kind,
            file_range) &&
        file_range.count == 2 &&
        path.ends_with("shared.hpp") &&
        kind == file_kind::header &&
        view.source_root(
            file_id{4},
            root) &&
        root.count == 4 &&
        view.source_file(
            file_id{4},
            path,
            kind,
            file_range) &&
        file_range.count == 4 &&
        path.ends_with("objects.source") &&
        kind == file_kind::source,
        "mapped Header/Source provenance without reconstruction");
    const auto corrupt = [&](compiled_project_section section,
                             std::size_t offset,
                             std::uint32_t value,
                             std::string_view name) {
        auto bytes = image.bytes;
        write_u32(bytes.data() + section_offset(bytes, section) + offset, value);
        rewrite_section_crc(bytes, section);
        compiled_project_view altered;
        tests.expect(altered.bind(bytes) == compiled_project_image_result::success &&
                         altered.verify_contents() == compiled_project_image_result::invalid_image,
                     name);
    };
    corrupt(compiled_project_section::source_file_indices,
            4,
            0,
            "reject duplicate file contribution index");
    corrupt(compiled_project_section::source_contributions,
            3 * 8 + 4,
            read_u32(image.bytes.data() +
                     section_offset(image.bytes, compiled_project_section::source_contributions) +
                     2 * 8 + 4),
            "reject duplicate semantic contribution within a root with valid CRC");
    corrupt(compiled_project_section::source_contributions,
            0,
            0,
            "reject invalid contribution physical file");
    corrupt(compiled_project_section::source_roots,
            8,
            UINT32_MAX,
            "reject out-of-bounds empty root range");
    corrupt(compiled_project_section::source_roots, 16, 0, "reject overlapping ownership ranges");
    corrupt(compiled_project_section::source_files,
            24,
            UINT32_MAX,
            "reject invalid persisted path offset");
    corrupt(
        compiled_project_section::source_files,
        0,
        static_cast<std::uint32_t>(
            file_kind::source),
        "reject Header-owned semantics under a Source root");
    corrupt(
        compiled_project_section::source_files,
        20,
        static_cast<std::uint32_t>(
            file_kind::source),
        "reject Header semantic contribution from a Source file");
    corrupt(
        compiled_project_section::source_files,
        60,
        static_cast<std::uint32_t>(
            file_kind::header),
        "reject Source semantic contribution from a Header file");
    corrupt(compiled_project_section::source_contributions,
            4,
            source_data_ref::from_raw(0xffffffffu).raw(),
            "reject nonexistent graph link");
    auto old = image.bytes;
    write_u32(
        old.data() + 8,
        compiled_project_format_version - 1);
    rewrite_header_crc(old);
    tests.expect(view.bind(old) == compiled_project_image_result::invalid_image,
                 "reject previous compiled format");
}

void test_source_map_provenance(test_state &tests, const compiled_fixture &fixture) {

    source_map sources;

    if (!tests.expect(succeeded(sources.begin_root(file_id{1})), "Source Map begin first root") ||
        !tests.expect(succeeded(sources.add(
                          file_id{2}, source_data_ref::type_definition(fixture.type_identity))),
                      "Source Map first contribution") ||
        !tests.expect(succeeded(sources.end_root()), "Source Map end first root") ||
        !tests.expect(succeeded(sources.begin_root(file_id{3})), "Source Map begin second root") ||
        !tests.expect(succeeded(sources.add(
                          file_id{2}, source_data_ref::type_definition(fixture.type_identity))),
                      "Source Map shared physical contribution") ||
        !tests.expect(succeeded(sources.end_root()), "Source Map end second root") ||
        !tests.expect(succeeded(sources.finalize(3, fixture.identities, fixture.G)), "Source Map finalize")) {
        return;
    }

    const auto contributions = sources.contribution_entries();

    const auto roots = sources.root_entries();

    const auto files = sources.file_entries();

    const auto presence = sources.type_presence_entries();

    tests.expect(contributions.size() == 2 &&
                     sources.file_index_entries().size() == 2 && roots.size() == 3 &&
                     roots[0].begin == 0 && roots[0].count == 1 &&
                     roots[2].begin == 1 && roots[2].count == 1 && files.size() == 3 &&
                     files[1].count == 2,
                 "Source Map canonical root-owned payload and physical secondary index");

    tests.expect(fixture.type.value() <= presence.size() &&
                     presence[fixture.type.value() - 1].declarations == 2 &&
                     presence[fixture.type.value() - 1].definitions == 2,
                 "Source Map presence counts root ownership");
}
}
}

int main() {
    using namespace cw::server;

    try {
        test_state tests;

        test_graph_reference_invariants(
            tests);

        compiled_fixture fixture;

        if (!build_fixture(
                tests,
                fixture)) {

            return 1;
        }

        test_source_map_provenance(tests, fixture);

        compiled_test_image first;
        compiled_test_image second;

        if (!tests.expect(
                build_test_compiled_image(
                    fixture,
                    first) ==
                    compiled_project_image_result::success,
                "encode first compiled image") ||
            !tests.expect(
                build_test_compiled_image(
                    fixture,
                    second) ==
                    compiled_project_image_result::success,
                "encode second compiled image")) {

            return 1;
        }

        tests.expect(
            first.bytes == second.bytes,
            "deterministic compiled image");

        test_persisted_sources(tests, first);

        test_round_trip(
            tests,
            fixture,
            first);

        test_runtime_layout(
            tests,
            fixture,
            first);

        test_runtime_layout_tail_alignment(
            tests);

        test_fixed_direct_materializer(
            tests);

        test_fixed_direct_unplanned_reference_chain(
            tests);

        test_fixed_direct_link_prebind(
            tests);

        test_build_lineage_overlays(
            tests,
            fixture,
            first);

        test_string_intern_growth_and_aliasing(tests);

        test_direct_mmap_encoding(
            tests,
            fixture);

        test_structural_corruption(
            tests,
            first);

        test_hot_cold_boundary(
            tests,
            first);

        test_semantic_corruption(
            tests,
            fixture,
            first);

        if (tests.failures != 0) {
            std::cerr
                << tests.failures
                << " compiled_project test(s) failed\n";
            return 1;
        }

        std::cout
            << "compiled_project tests passed\n";

        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "UNEXPECTED EXCEPTION: "
            << error.what()
            << '\n';
        return 1;
    }
    catch (...) {
        std::cerr
            << "UNEXPECTED NON-STANDARD EXCEPTION\n";
        return 1;
    }
}
