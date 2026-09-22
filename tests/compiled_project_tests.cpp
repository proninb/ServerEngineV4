#include "project/persistence/compiled_project.hpp"
#include "project/persistence/crc64_ecma.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
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
    assign_table assigns;

    string_id namespace_name{};
    string_id type_name{};
    string_id value_name{};
    string_id peer_name{};
    string_id left_name{};
    string_id right_name{};

    identity_ref namespace_identity{};
    identity_ref type_identity{};
    identity_ref left_identity{};
    identity_ref right_identity{};

    type_handle type{};
    type_ref integer_type{};
    type_ref pointer_type{};
    type_ref named_type{};

    member_index value_member{};
    member_index peer_member{};

    object_handle left{};
    object_handle right{};
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
        !intern("right", fixture.right_name, "intern right object")) {

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
            "resolve right object")) {

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
                derived_type_kind::pointer,
                0,
                fixture.pointer_type),
            "derive pointer type")) {

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
            fixture.pointer_type,
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
                fixture.right,
                graph_object_non_default_initializer),
            "add right object") ||
        !success(
            tests,
            fixture.G.add_link(
                {
                    fixture.left,
                    fixture.peer_member,
                },
                {
                    fixture.right,
                    fixture.value_member,
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

    return true;
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
    const project_artifact_image& image) {

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
                fixture.pointer_type &&
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

    derived_type_record derived;

    tests.expect(
        view.derived(
            fixture.pointer_type,
            derived) &&
            derived.child ==
                fixture.integer_type &&
            derived.kind ==
                derived_type_kind::pointer &&
            derived.payload == 0,
        "derived type preservation");

    tests.expect(
        view.find_object(
            fixture.left_identity) ==
            fixture.left &&
        view.find_object(
            fixture.right_identity) ==
            fixture.right,
        "object handle preservation");

    object_entry right_object;

    tests.expect(
        view.object(
            fixture.right,
            right_object) &&
            right_object.type ==
                fixture.named_type &&
            right_object.non_default_initializer(),
        "object record preservation");

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
    const project_artifact_image& image) {

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

void test_hot_cold_boundary(
    test_state& tests,
    const project_artifact_image& image) {

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
    const project_artifact_image& image) {

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
}

}
}

int main() {
    using namespace cw::server;

    try {
        test_state tests;
        compiled_fixture fixture;

        if (!build_fixture(
                tests,
                fixture)) {

            return 1;
        }

        project_artifact_image first;
        project_artifact_image second;

        if (!tests.expect(
                build_compiled_project_image(
                    fixture.strings,
                    fixture.identities,
                    fixture.G,
                    fixture.assigns,
                    first) ==
                    compiled_project_image_result::success,
                "encode first compiled image") ||
            !tests.expect(
                build_compiled_project_image(
                    fixture.strings,
                    fixture.identities,
                    fixture.G,
                    fixture.assigns,
                    second) ==
                    compiled_project_image_result::success,
                "encode second compiled image")) {

            return 1;
        }

        tests.expect(
            first.bytes == second.bytes &&
                first.hash == second.hash,
            "deterministic compiled image");

        test_round_trip(
            tests,
            fixture,
            first);

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
