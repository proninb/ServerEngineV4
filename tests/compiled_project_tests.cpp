#include "project/project.hpp"
#include "project/persistence/compiled_project.hpp"
#include "project/persistence/crc64_ecma.hpp"
#include "project/file/file_context.hpp"
#include "project/source/source_map.hpp"
#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
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

    const auto directory = std::filesystem::temp_directory_path();
    for (const char *name : {"source_root.hpp", "shared.hpp", "other_root.hpp"}) {
        file_id file;
        if (!success(tests,
                     fixture.files.resolve(directory / name, file_kind::header, file),
                     "resolve source map file"))
            return false;
    }
    for (auto root : {file_id{1}, file_id{3}}) {
        if (!success(tests, fixture.sources.begin_root(root), "begin persisted root") ||
            !success(tests,
                     fixture.sources.add(file_id{2},
                                         source_data_ref::type_definition(fixture.type_identity)),
                     "shared persisted definition") ||
            !success(
                tests,
                fixture.sources.add(file_id{2}, source_data_ref::object(fixture.left_identity)),
                "shared persisted object") ||
            !success(tests,
                     fixture.sources.add(file_id{2}, source_data_ref::link(fixture.link)),
                     "shared persisted link") ||
            !success(tests, fixture.sources.end_root(), "end persisted root"))
            return false;
    }
    return tests.expect(succeeded(fixture.sources.finalize(fixture.files.size(), fixture.identities, fixture.G)),
                        "finalize empty Source Map fixture");
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

        {
            project resident{
                std::filesystem::temp_directory_path() /
                    "server_engine_v4_project.json",
                std::move(persisted),
                persisted_view};

            tests.expect(
                !persisted.valid() &&
                    resident.compiled().valid() &&
                    resident.compiled().find_type(
                        fixture.type_identity) ==
                        fixture.type,
                "resident Project owns compiled mmap and preserves Graph view lifetime");
        }
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
    tests.expect(view.source_file_count() == 3 && view.source_contribution_count() == 6 &&
                     view.source_root(file_id{1}, root) && root.count == 3 &&
                     view.source_file(file_id{2}, path, kind, file_range) &&
                     file_range.count == 6 && path.ends_with("shared.hpp") &&
                     kind == file_kind::header,
                 "mapped provenance and paths without reconstruction");
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
    corrupt(compiled_project_section::source_contributions,
            4,
            source_data_ref::from_raw(0xffffffffu).raw(),
            "reject nonexistent graph link");
    auto old = image.bytes;
    write_u32(old.data() + 8, 2);
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

        test_build_lineage_overlays(
            tests,
            fixture,
            first);

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
