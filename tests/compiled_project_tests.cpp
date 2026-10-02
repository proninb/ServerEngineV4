#include "project/persistence/compiled_project.hpp"
#include "project/persistence/compiled_project_build.hpp"
#include "project/persistence/crc64_ecma.hpp"
#include "project/runtime/fixed_direct_materializer.hpp"
#include "project/runtime/runtime_layout.hpp"
#include "project/runtime/runtime_system.hpp"
#include "project/runtime/runtime_query.hpp"
#include "project/runtime/runtime_ic.hpp"
#include "project/runtime/runtime_ic_codec.hpp"
#include "project/runtime/runtime_ic_snapshot.hpp"
#include "project/runtime/runtime_ic_reset.hpp"
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



compiled_project_image_result build_test_compiled_image(
    const compiled_fixture& fixture,
    compiled_test_image& output);

void test_class_abi_persistence(
    test_state& tests) {

    compiled_fixture fixture;

    string_id a_name;
    string_id b_name;
    string_id c_name;
    string_id a_member_name;
    string_id b_member_name;
    string_id c_member_name;
    string_id instance_name;

    const auto intern =
        [&](std::string_view value,
            string_id& output) {

            return succeeded(
                fixture.strings.intern(
                    value,
                    output));
        };

    if (!tests.expect(
            intern("AbiA", a_name) &&
            intern("AbiB", b_name) &&
            intern("AbiC", c_name) &&
            intern("a", a_member_name) &&
            intern("b", b_member_name) &&
            intern("c", c_member_name) &&
            intern("instance", instance_name),
            "prepare class ABI persistence strings")) {

        return;
    }

    identity_ref a_identity;
    identity_ref b_identity;
    identity_ref c_identity;
    identity_ref instance_identity;

    if (!tests.expect(
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    a_name,
                    identity_kind::type,
                    a_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    b_name,
                    identity_kind::type,
                    b_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    c_name,
                    identity_kind::type,
                    c_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    instance_name,
                    identity_kind::object,
                    instance_identity)),
            "prepare class ABI persistence identities")) {

        return;
    }

    type_handle a;
    type_handle b;
    type_handle c;

    if (!tests.expect(
            succeeded(
                fixture.G.declare_record(
                    a_identity,
                    graph_record_kind::struct_type,
                    a)) &&
            succeeded(
                fixture.G.declare_record(
                    b_identity,
                    graph_record_kind::struct_type,
                    b)) &&
            succeeded(
                fixture.G.declare_record(
                    c_identity,
                    graph_record_kind::struct_type,
                    c)),
            "declare class ABI persistence records")) {

        return;
    }

    const auto integer =
        fixture.G.intrinsic(
            intrinsic_type::signed_int);

    const std::array<member_record, 1>
        a_members{{
            {
                a_member_name,
                integer,
                graph_member_access::public_access,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    a,
                    graph_record_kind::struct_type,
                    a_members,
                    {},
                    {},
                    true)),
            "define polymorphic base record")) {

        return;
    }

    const std::array<base_record, 1>
        b_bases{{
            {
                a_identity,
                graph_member_access::public_access,
                0,
                0,
            },
        }};

    const std::array<member_record, 1>
        b_members{{
            {
                b_member_name,
                integer,
                graph_member_access::public_access,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    b,
                    graph_record_kind::struct_type,
                    b_members,
                    {},
                    b_bases,
                    false)),
            "define inherited polymorphic record")) {

        return;
    }

    const std::array<member_record, 1>
        c_members{{
            {
                c_member_name,
                integer,
                graph_member_access::public_access,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    c,
                    graph_record_kind::struct_type,
                    c_members)),
            "define unrelated plain record")) {

        return;
    }

    object_handle instance;

    if (!tests.expect(
            succeeded(
                fixture.G.add_object(
                    instance_identity,
                    fixture.G.named(b),
                    instance)),
            "add inherited polymorphic Runtime object")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.sources.finalize(
                    fixture.files.size(),
                    fixture.identities,
                    fixture.G)),
            "finalize class ABI persistence Source Map")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::success,
            "encode class ABI persistence image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success &&
            view.verify_contents() ==
                compiled_project_image_result::success,
            "bind and audit class ABI persistence image")) {

        return;
    }

    type_entry persisted_a;
    type_entry persisted_b;
    type_entry persisted_c;
    base_record persisted_base;

    tests.expect(
        view.type(
            a,
            persisted_a) &&
        view.type(
            b,
            persisted_b) &&
        view.type(
            c,
            persisted_c) &&
        persisted_a.polymorphic() &&
        persisted_a.bases.count == 0 &&
        persisted_b.polymorphic() &&
        persisted_b.bases.count == 1 &&
        !persisted_c.polymorphic() &&
        persisted_c.bases.count == 0 &&
        view.base_at(
            persisted_b.bases.begin,
            persisted_base) &&
        persisted_base.type ==
            a_identity &&
        persisted_base.access ==
            graph_member_access::
                public_access &&
        !persisted_base.virtual_base(),
        "compiled.bin preserves type-local polymorphism and base ranges");

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

#if defined(_WIN32)
    tests.expect(
        prepare_runtime_layout(
            view,
            abi,
            layout) ==
            runtime_layout_result::
                success,
        "Runtime class ABI derives compiler-validated Windows layout");
#else
    tests.expect(
        prepare_runtime_layout(
            view,
            abi,
            layout) ==
            runtime_layout_result::
                unsupported_type,
        "non-Windows class ABI remains fail-closed without compiler oracle");
#endif
}

void test_endpoint_path_persistence(
    test_state& tests) {

    compiled_fixture fixture;

    string_id type_name;
    string_id values_name;
    string_id input_name;
    string_id a_name;
    string_id b_name;

    const auto intern =
        [&](std::string_view value,
            string_id& output) {

            return succeeded(
                fixture.strings.intern(
                    value,
                    output));
        };

    if (!tests.expect(
            intern("PathRecord", type_name) &&
            intern("values", values_name) &&
            intern("in", input_name) &&
            intern("a", a_name) &&
            intern("b", b_name),
            "prepare endpoint-path persistence strings")) {

        return;
    }

    identity_ref type_identity;
    identity_ref a_identity;
    identity_ref b_identity;

    if (!tests.expect(
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    type_name,
                    identity_kind::type,
                    type_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    a_name,
                    identity_kind::object,
                    a_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    b_name,
                    identity_kind::object,
                    b_identity)),
            "prepare endpoint-path persistence identities")) {

        return;
    }

    type_handle type;

    if (!tests.expect(
            succeeded(
                fixture.G.declare_record(
                    type_identity,
                    graph_record_kind::struct_type,
                    type)),
            "declare endpoint-path persistence record")) {

        return;
    }

    const auto integer =
        fixture.G.intrinsic(
            intrinsic_type::signed_int);

    type_ref array_type;
    type_ref reference_type;

    if (!tests.expect(
            integer &&
            succeeded(
                fixture.G.derive(
                    integer,
                    derived_type_kind::bounded_array,
                    4,
                    array_type)) &&
            succeeded(
                fixture.G.derive(
                    integer,
                    derived_type_kind::lvalue_reference,
                    0,
                    reference_type)),
            "derive endpoint-path persistence types")) {

        return;
    }

    const std::array<member_record, 2>
        members{{
            {
                values_name,
                array_type,
                graph_member_access::public_access,
            },
            {
                input_name,
                reference_type,
                graph_member_access::public_access,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    type,
                    graph_record_kind::struct_type,
                    members)),
            "define endpoint-path persistence record")) {

        return;
    }

    const auto named =
        fixture.G.named(type);

    object_handle a;
    object_handle b;

    if (!tests.expect(
            named &&
            succeeded(
                fixture.G.add_object(
                    a_identity,
                    named,
                    a)) &&
            succeeded(
                fixture.G.add_object(
                    b_identity,
                    named,
                    b)),
            "add endpoint-path persistence objects")) {

        return;
    }

    const auto values =
        fixture.G.find_member(
            type,
            values_name);

    const auto input =
        fixture.G.find_member(
            type,
            input_name);

    const std::array<endpoint_path_step, 2>
        steps{{
            {
                values.value(),
                endpoint_path_step_kind::member,
                {},
            },
            {
                2,
                endpoint_path_step_kind::array_index,
                {},
            },
        }};

    endpoint_path_handle path;
    type_ref path_type;

    if (!tests.expect(
            values &&
            input &&
            succeeded(
                fixture.G.intern_endpoint_path(
                    named,
                    steps,
                    path,
                    &path_type)) &&
            path &&
            path_type ==
                integer,
            "intern endpoint-path persistence source")) {

        return;
    }

    endpoint_path_handle repeated;
    type_ref repeated_type;

    tests.expect(
        succeeded(
            fixture.G.intern_endpoint_path(
                named,
                steps,
                repeated,
                &repeated_type)) &&
        repeated ==
            path &&
        repeated_type ==
            integer &&
        fixture.G.endpoint_path_count() ==
            1 &&
        fixture.G.endpoint_path_step_count() ==
            2,
        "endpoint paths are canonical in G");

    link_handle link;

    if (!tests.expect(
            succeeded(
                fixture.G.add_link(
                    {
                        a_identity,
                        endpoint_ref::from_path(
                            path),
                    },
                    {
                        b_identity,
                        input,
                    },
                    link)),
            "add link with endpoint-path source")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.sources.finalize(
                    fixture.files.size(),
                    fixture.identities,
                    fixture.G)),
            "finalize endpoint-path persistence Source Map")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::success,
            "encode endpoint-path persistence image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success &&
            view.verify_contents() ==
                compiled_project_image_result::success,
            "bind and audit endpoint-path persistence image")) {

        return;
    }

    link_record persisted_link;
    endpoint_path_record persisted_path;
    endpoint_path_step first;
    endpoint_path_step second;

    tests.expect(
        view.endpoint_path_count() ==
            1 &&
        view.endpoint_path_step_count() ==
            2 &&
        view.link(
            link,
            persisted_link) &&
        persisted_link.source.object ==
            a_identity &&
        persisted_link.source.member.is_path() &&
        persisted_link.source.member.path() ==
            path &&
        persisted_link.target.object ==
            b_identity &&
        persisted_link.target.member.direct_member() ==
            input &&
        view.endpoint_path(
            path,
            persisted_path) &&
        persisted_path.root_type ==
            named &&
        persisted_path.value_type ==
            integer &&
        persisted_path.steps.begin ==
            0 &&
        persisted_path.steps.count ==
            2 &&
        view.endpoint_path_step_at(
            0,
            first) &&
        first.kind ==
            endpoint_path_step_kind::member &&
        first.value ==
            values.value() &&
        view.endpoint_path_step_at(
            1,
            second) &&
        second.kind ==
            endpoint_path_step_kind::array_index &&
        second.value ==
            2,
        "compiled.bin preserves canonical endpoint path");

    tests.expect(
        view.find_endpoint_path(
            named,
            steps) ==
            path,
        "compiled mmap endpoint-path index resolves canonical path");

    graph_delta delta;

    if (!tests.expect(
            succeeded(
                delta.bind_baseline(
                    view)),
            "bind endpoint-path BUILD baseline")) {

        return;
    }

    endpoint_path_handle baseline_path;
    type_ref baseline_path_type;

    if (!tests.expect(
            succeeded(
                delta.intern_endpoint_path(
                    named,
                    steps,
                    baseline_path,
                    &baseline_path_type)) &&
            baseline_path ==
                path &&
            baseline_path_type ==
                integer &&
            delta.endpoint_path_count() ==
                view.endpoint_path_count() &&
            delta.endpoint_path_step_count() ==
                view.endpoint_path_step_count(),
            "BUILD endpoint-path interning reuses mmap baseline path")) {

        return;
    }

    const object_endpoint path_initialization_target{
        a_identity,
        endpoint_ref::from_path(
            baseline_path),
    };

    bool path_initialization_replaced = true;
    object_initialization_record path_initialization;

    tests.expect(
        succeeded(
            delta.add_initialization(
                path_initialization_target,
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    17),
                path_initialization_replaced)) &&
        !path_initialization_replaced &&
        delta.initialization_count() == 1 &&
        delta.initialization(
            path_initialization_target,
            path_initialization) &&
        path_initialization.value ==
            construction_value::constant(
                construction_kind::
                    signed_integer,
                17),
        "BUILD graph_delta initializes scalar endpoint paths with Graph semantics");

    const std::array<endpoint_path_step, 2>
        appended_steps{{
            {
                values.value(),
                endpoint_path_step_kind::member,
                {},
            },
            {
                1,
                endpoint_path_step_kind::array_index,
                {},
            },
        }};

    endpoint_path_handle appended_path;
    endpoint_path_handle repeated_appended_path;
    type_ref appended_type;

    if (!tests.expect(
            succeeded(
                delta.intern_endpoint_path(
                    named,
                    appended_steps,
                    appended_path,
                    &appended_type)) &&
            appended_path.value() ==
                view.endpoint_path_count() + 1 &&
            appended_type ==
                integer &&
            succeeded(
                delta.intern_endpoint_path(
                    named,
                    appended_steps,
                    repeated_appended_path,
                    nullptr)) &&
            repeated_appended_path ==
                appended_path &&
            delta.endpoint_path_count() ==
                view.endpoint_path_count() + 1 &&
            delta.endpoint_path_step_count() ==
                view.endpoint_path_step_count() +
                    appended_steps.size(),
            "BUILD endpoint paths append canonically after mmap baseline")) {

        return;
    }

    endpoint_path_record appended_record;
    endpoint_path_step appended_first;
    endpoint_path_step appended_second;

    tests.expect(
        delta.endpoint_path(
            appended_path,
            appended_record) &&
        appended_record.root_type ==
            named &&
        appended_record.value_type ==
            integer &&
        appended_record.steps.begin ==
            view.endpoint_path_step_count() &&
        appended_record.steps.count ==
            2 &&
        delta.endpoint_path_step_at(
            appended_record.steps.begin,
            appended_first) &&
        appended_first ==
            appended_steps[0] &&
        delta.endpoint_path_step_at(
            static_cast<std::size_t>(
                appended_record.steps.begin) + 1,
            appended_second) &&
        appended_second ==
            appended_steps[1],
        "BUILD exposes logical baseline plus appended endpoint paths");
}

void test_graph_derived_type_invariants(
    test_state& tests) {

    graph G;

    const auto integer =
        G.intrinsic(
            intrinsic_type::signed_int);

    const auto void_value =
        G.intrinsic(
            intrinsic_type::void_type);

    type_ref integer_reference;

    if (!tests.expect(
            integer &&
            void_value &&
            succeeded(
                G.derive(
                    integer,
                    derived_type_kind::lvalue_reference,
                    0,
                    integer_reference)),
            "prepare Graph derived-type invariant inputs")) {

        return;
    }

    type_ref output;

    tests.expect(
        G.derive(
            integer,
            derived_type_kind::bounded_array,
            0,
            output) ==
                server_status::
                    project_configuration_invalid &&
        !output,
        "Graph rejects zero bounded-array extent");

    tests.expect(
        G.derive(
            integer,
            derived_type_kind::pointer,
            1,
            output) ==
                server_status::
                    project_configuration_invalid &&
        !output,
        "Graph rejects payload on non-array derived type");

    tests.expect(
        G.derive(
            integer_reference,
            derived_type_kind::bounded_array,
            4,
            output) ==
                server_status::
                    project_configuration_invalid &&
        !output,
        "Graph rejects array of references");

    tests.expect(
        G.derive(
            integer_reference,
            derived_type_kind::pointer,
            0,
            output) ==
                server_status::
                    project_configuration_invalid &&
        !output,
        "Graph rejects pointer to reference");

    tests.expect(
        G.derive(
            void_value,
            derived_type_kind::lvalue_reference,
            0,
            output) ==
                server_status::
                    project_configuration_invalid &&
        !output,
        "Graph rejects reference to void");

    type_ref inner;
    type_ref outer;
    type_ref array_reference;

    tests.expect(
        succeeded(
            G.derive(
                integer,
                derived_type_kind::bounded_array,
                3,
                inner)) &&
        succeeded(
            G.derive(
                inner,
                derived_type_kind::bounded_array,
                2,
                outer)) &&
        succeeded(
            G.derive(
                outer,
                derived_type_kind::lvalue_reference,
                0,
                array_reference)) &&
        array_reference,
        "Graph accepts reference to bounded multidimensional array");
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
                G.identity(
                    static_integer).value()),
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
                    G.identity(left),
                    value,
                },
                {
                    G.identity(right),
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
                G.identity(bound_left),
                G.find_member(
                    bound_type,
                    value_name),
            },
            {
                G.identity(bound_right),
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
                G.identity(left),
                value,
            },
            {
                G.identity(right),
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
                G.identity(left),
                real,
            },
            {
                G.identity(left),
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
                    fixture.left_identity,
                    fixture.value_member,
                },
                {
                    fixture.right_identity,
                    fixture.peer_member,
                },
                fixture.link),
            "add link") ||
        !success(
            tests,
            fixture.assigns.add(
                file_id{5},
                "sensor.value",
                "ui.value"),
            "add first Assign") ||
        !success(
            tests,
            fixture.assigns.add(
                file_id{5},
                "source.path",
                "target.path"),
            "add second Assign") ||
        !success(
            tests,
            fixture.assigns.add(
                file_id{5},
                "demo::right.value",
                "demo::left.peer"),
            "add object-member Assign") ||
        !success(
            tests,
            fixture.assigns.add(
                file_id{5},
                "remote",
                "demo::left"),
            "add whole-object Assign")) {

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
                 file_kind::source},
             std::pair{
                 "wiring.assign",
                 file_kind::assign}}) {

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

[[nodiscard]] std::uint64_t section_count(
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

    return read_u64(
        entry + 16);
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


void test_class_abi_multiple_base_persistence(
    test_state& tests) {

    compiled_fixture fixture;

    string_id a_name;
    string_id b_name;
    string_id c_name;
    string_id d_name;

    if (!tests.expect(
            succeeded(
                fixture.strings.intern(
                    "MultiA",
                    a_name)) &&
            succeeded(
                fixture.strings.intern(
                    "MultiB",
                    b_name)) &&
            succeeded(
                fixture.strings.intern(
                    "MultiC",
                    c_name)) &&
            succeeded(
                fixture.strings.intern(
                    "MultiD",
                    d_name)),
            "prepare multiple-base persistence strings")) {

        return;
    }

    identity_ref a_identity;
    identity_ref b_identity;
    identity_ref c_identity;
    identity_ref d_identity;

    if (!tests.expect(
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    a_name,
                    identity_kind::type,
                    a_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    b_name,
                    identity_kind::type,
                    b_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    c_name,
                    identity_kind::type,
                    c_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    d_name,
                    identity_kind::type,
                    d_identity)),
            "prepare multiple-base persistence identities")) {

        return;
    }

    type_handle a;
    type_handle b;
    type_handle c;
    type_handle d;

    if (!tests.expect(
            succeeded(
                fixture.G.declare_record(
                    a_identity,
                    graph_record_kind::struct_type,
                    a)) &&
            succeeded(
                fixture.G.declare_record(
                    b_identity,
                    graph_record_kind::struct_type,
                    b)) &&
            succeeded(
                fixture.G.declare_record(
                    c_identity,
                    graph_record_kind::struct_type,
                    c)) &&
            succeeded(
                fixture.G.declare_record(
                    d_identity,
                    graph_record_kind::struct_type,
                    d)),
            "declare multiple-base persistence records")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    a,
                    graph_record_kind::struct_type,
                    {})),
            "define multiple-base root")) {

        return;
    }

    const std::array<base_record, 1>
        b_bases{{
            {
                a_identity,
                graph_member_access::public_access,
                0,
                0,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    b,
                    graph_record_kind::struct_type,
                    {},
                    {},
                    b_bases)),
            "define first inheritance edge")) {

        return;
    }

    const std::array<base_record, 1>
        c_bases{{
            {
                b_identity,
                graph_member_access::public_access,
                0,
                0,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    c,
                    graph_record_kind::struct_type,
                    {},
                    {},
                    c_bases)),
            "define second inheritance edge")) {

        return;
    }

    const std::array<base_record, 2>
        d_bases{{
            {
                a_identity,
                graph_member_access::public_access,
                0,
                0,
            },
            {
                b_identity,
                graph_member_access::protected_access,
                0,
                0,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    d,
                    graph_record_kind::class_type,
                    {},
                    {},
                    d_bases)),
            "G preserves multiple direct bases")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.sources.finalize(
                    fixture.files.size(),
                    fixture.identities,
                    fixture.G)),
            "finalize multiple-base persistence Source Map")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::success,
            "encode multiple-base persistence image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success &&
            view.verify_contents() ==
                compiled_project_image_result::success,
            "cold audit accepts valid multiple-base G")) {

        return;
    }

    type_entry persisted_b;
    type_entry persisted_c;
    type_entry persisted_d;
    base_record d_base0;
    base_record d_base1;

    if (!tests.expect(
            view.type(
                b,
                persisted_b) &&
            view.type(
                c,
                persisted_c) &&
            view.type(
                d,
                persisted_d) &&
            persisted_d.bases.count == 2 &&
            view.base_at(
                persisted_d.bases.begin,
                d_base0) &&
            view.base_at(
                static_cast<std::size_t>(
                    persisted_d.bases.begin) +
                    1,
                d_base1) &&
            d_base0.type == a_identity &&
            d_base1.type == b_identity,
            "compiled G preserves ordered 0..N direct bases")) {

        return;
    }

    auto cyclic =
        image.bytes;

    const auto bases_offset =
        section_offset(
            cyclic,
            compiled_project_section::bases);

    write_u32(
        cyclic.data() +
            bases_offset +
            static_cast<std::size_t>(
                persisted_b.bases.begin) *
                sizeof(base_record) +
            offsetof(
                base_record,
                type),
        c_identity.value());

    rewrite_section_crc(
        cyclic,
        compiled_project_section::bases);

    compiled_project_view cyclic_view;

    tests.expect(
        cyclic_view.bind(
            cyclic) ==
                compiled_project_image_result::success &&
        cyclic_view.verify_contents() ==
            compiled_project_image_result::invalid_image,
        "cold audit rejects persisted inheritance cycle");

    auto duplicate =
        image.bytes;

    write_u32(
        duplicate.data() +
            section_offset(
                duplicate,
                compiled_project_section::bases) +
            (static_cast<std::size_t>(
                 persisted_d.bases.begin) +
             1) *
                sizeof(base_record) +
            offsetof(
                base_record,
                type),
        a_identity.value());

    rewrite_section_crc(
        duplicate,
        compiled_project_section::bases);

    compiled_project_view duplicate_view;

    tests.expect(
        duplicate_view.bind(
            duplicate) ==
                compiled_project_image_result::success &&
        duplicate_view.verify_contents() ==
            compiled_project_image_result::invalid_image,
        "cold audit rejects duplicate direct base");
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
            fixture.G.type_count() &&
        view.type_slot_count() ==
            view.type_count() &&
        view.live_type_count() ==
            view.type_count(),
        "type live/slot count");

    tests.expect(
        view.object_count() ==
            fixture.G.object_count() &&
        view.object_slot_count() ==
            view.object_count() &&
        view.live_object_count() ==
            view.object_count(),
        "object live/slot count");

    tests.expect(
        view.link_count() ==
            fixture.G.link_count() &&
        view.link_slot_count() ==
            view.link_count() &&
        view.live_link_count() ==
            view.link_count(),
        "link live/slot count");

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
    type_entry raw_type_value;

    tests.expect(
        view.type_slot_live(
            fixture.type) &&
        view.find_type_lineage(
            fixture.type_identity) ==
            fixture.type &&
        view.type_raw(
            fixture.type,
            raw_type_value) &&
        view.type(
            fixture.type,
            type_value) &&
        raw_type_value.members.begin ==
            type_value.members.begin &&
        raw_type_value.members.count ==
            type_value.members.count,
        "read live and lineage type slot");

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
            fixture.value_member &&
        view.find_member(
            fixture.type,
            fixture.peer_name) ==
            fixture.peer_member &&
        !view.find_member(
            fixture.type,
            fixture.scalar_name),
        "persisted member-name index preserves zero-based lookup");

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
    object_entry raw_scalar_object;

    tests.expect(
        view.object_slot_live(
            fixture.scalar) &&
        view.find_object_lineage(
            fixture.scalar_identity) ==
            fixture.scalar &&
        view.object_raw(
            fixture.scalar,
            raw_scalar_object) &&
        view.object(
            fixture.scalar,
            scalar_object) &&
        raw_scalar_object.type ==
            scalar_object.type &&
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

    link_record raw_link_value;

    tests.expect(
        view.link_slot_live(
            fixture.link) &&
        view.link_raw(
            fixture.link,
            raw_link_value) &&
        view.link(
            fixture.link,
            link_value) &&
        raw_link_value.source ==
            link_value.source &&
        raw_link_value.target ==
            link_value.target &&
        link_value.source ==
            source_link.source &&
        link_value.target ==
            source_link.target,
        "link live/raw slot preservation");

    tests.expect(
        view.find_link_target_lineage(
            source_link.target) ==
            fixture.link &&
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

    file_id assign_file;

    std::string_view assign_path;
    file_kind assign_kind;
    source_map_range assign_range;

    tests.expect(
        view.assign_file(
            0,
            assign_file) &&
        assign_file ==
            file_id{5} &&
        view.source_file(
            assign_file,
            assign_path,
            assign_kind,
            assign_range) &&
        assign_kind ==
            file_kind::assign &&
        assign_path.ends_with(
            "wiring.assign"),
        "Assign preserves physical file provenance");
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



void test_writable_mapping_grow_existing(
    test_state& tests) {

    const auto path =
        std::filesystem::temp_directory_path() /
        "server_engine_v4_grow_existing_mapping_test.bin";

    std::error_code error;
    (void)std::filesystem::remove(
        path,
        error);

    writable_file_mapping writable;

    if (!tests.expect(
            writable.create(
                path,
                64) ==
                writable_file_mapping_result::success,
            "create grow-existing mapping baseline")) {

        return;
    }

    for (auto& value :
         writable.bytes()) {

        value = std::byte{0x5a};
    }

    if (!tests.expect(
            writable.flush() ==
                writable_file_mapping_result::success,
            "flush grow-existing mapping baseline")) {

        writable.reset();
        error.clear();
        (void)std::filesystem::remove(
            path,
            error);
        return;
    }

    writable.reset();

    if (!tests.expect(
            writable.open_existing(
                path,
                128) ==
                writable_file_mapping_result::success &&
            writable.size() == 128,
            "grow existing mapping without truncating baseline")) {

        error.clear();
        (void)std::filesystem::remove(
            path,
            error);
        return;
    }

    bool baseline_preserved = true;

    for (std::size_t index = 0;
         index < 64;
         ++index) {

        if (writable.bytes()[index] !=
            std::byte{0x5a}) {

            baseline_preserved = false;
            break;
        }
    }

    tests.expect(
        baseline_preserved,
        "grow existing mapping preserves old bytes");

    for (std::size_t index = 64;
         index < 128;
         ++index) {

        writable.bytes()[index] =
            std::byte{0xa5};
    }

    tests.expect(
        writable.flush() ==
            writable_file_mapping_result::success,
        "flush appended grow-existing tail");

    writable.reset();

    if (!tests.expect(
            writable.open_existing(
                path,
                32) ==
                writable_file_mapping_result::success &&
            writable.size() == 128,
            "smaller minimum does not shrink existing mapping")) {

        error.clear();
        (void)std::filesystem::remove(
            path,
            error);
        return;
    }

    bool all_preserved = true;

    for (std::size_t index = 0;
         index < 128;
         ++index) {

        const auto expected =
            index < 64
            ? std::byte{0x5a}
            : std::byte{0xa5};

        if (writable.bytes()[index] !=
            expected) {

            all_preserved = false;
            break;
        }
    }

    tests.expect(
        all_preserved,
        "reopened grow-existing mapping preserves baseline and tail");

    writable.reset();

    error.clear();
    (void)std::filesystem::remove(
        path,
        error);

    tests.expect(
        !error,
        "remove grow-existing mapping test file");
}


void test_object_initialization_persistence_runtime(
    test_state& tests) {

    compiled_fixture fixture;

    if (!build_fixture(
            tests,
            fixture)) {

        return;
    }

    bool replaced = false;

    if (!tests.expect(
            succeeded(
                fixture.G.add_initialization(
                    {
                        fixture.left_identity,
                        endpoint_ref{
                            fixture.value_member},
                    },
                    construction_value::constant(
                        construction_kind::
                            signed_integer,
                        55),
                    replaced)) &&
            !replaced,
            "add first canonical object initialization")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.G.add_initialization(
                    {
                        fixture.left_identity,
                        endpoint_ref{
                            fixture.value_member},
                    },
                    construction_value::constant(
                        construction_kind::
                            signed_integer,
                        91),
                    replaced)) &&
            replaced &&
            fixture.G.initialization_count() == 1,
            "last object initialization replaces canonical value")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::success,
            "persist canonical object initialization")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success &&
            view.verify_contents() ==
                compiled_project_image_result::success &&
            view.initialization_count() == 1,
            "bind and audit persisted object initialization")) {

        return;
    }

    object_initialization_record persisted;

    const object_endpoint target{
        fixture.left_identity,
        endpoint_ref{
            fixture.value_member},
    };

    if (!tests.expect(
            view.initialization(
                target,
                persisted) &&
            persisted.target ==
                target &&
            persisted.value.kind ==
                construction_kind::
                    signed_integer &&
            persisted.value.bits() == 91,
            "compiled mmap exposes canonical final object init")) {

        return;
    }

    {
        auto corrupted =
            image.bytes;

        const auto offset =
            section_offset(
                corrupted,
                compiled_project_section::
                    object_initializations);

        write_u32(
            corrupted.data() +
                offset,
            0);

        rewrite_section_crc(
            corrupted,
            compiled_project_section::
                object_initializations);

        compiled_project_view corrupted_view;

        tests.expect(
            corrupted_view.bind(
                corrupted) ==
                    compiled_project_image_result::
                        success &&
            corrupted_view.verify_contents() ==
                    compiled_project_image_result::
                        invalid_image,
            "cold audit rejects invalid object initialization target");
    }

    {
        auto corrupted =
            image.bytes;

        const auto section =
            compiled_project_section::
                object_initialization_target_index;

        const auto directory_index =
            section_directory_index(
                section);

        const auto* directory_entry =
            corrupted.data() +
            directory_offset +
            directory_index *
                compiled_project_directory_entry_size;

        const auto count =
            read_u64(
                directory_entry + 16);

        const auto offset =
            section_offset(
                corrupted,
                section);

        bool changed = false;

        for (std::uint64_t index = 0;
             index < count;
             ++index) {

            auto* slot =
                corrupted.data() +
                offset +
                static_cast<std::size_t>(
                    index) * 8;

            if (read_u32(
                    slot + 4) == 0) {

                continue;
            }

            write_u32(
                slot,
                read_u32(slot) ^
                    0x00000001u);

            changed = true;
            break;
        }

        if (!tests.expect(
                changed,
                "find persisted object initialization index slot")) {

            return;
        }

        rewrite_section_crc(
            corrupted,
            section);

        compiled_project_view corrupted_view;

        tests.expect(
            corrupted_view.bind(
                corrupted) ==
                    compiled_project_image_result::
                        success &&
            corrupted_view.verify_contents() ==
                    compiled_project_image_result::
                        invalid_image,
            "cold audit rejects object initialization target-index corruption");
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
            "prepare Runtime layout with object init")) {

        return;
    }

    std::vector<std::byte> runtime(
        static_cast<std::size_t>(
            layout.size()),
        std::byte{0xcc});

    if (!tests.expect(
            materialize_fixed_direct(
                view,
                layout,
                abi,
                runtime) ==
                fixed_direct_materialization_result::
                    success,
            "materialize default init link order")) {

        return;
    }

    runtime_binding_index bindings;

    if (!tests.expect(
            layout.release_bindings(
                bindings),
            "release object-init Runtime bindings")) {

        return;
    }

    runtime_value left;
    runtime_value peer;

    tests.expect(
        get_runtime_value(
            view,
            bindings,
            runtime,
            "demo::left.value",
            left) ==
                runtime_query_result::success &&
        left.bits == 91,
        "object init overrides type member default");

    tests.expect(
        get_runtime_value(
            view,
            bindings,
            runtime,
            "demo::right.peer",
            peer) ==
                runtime_query_result::success &&
        peer.bits == 91,
        "reference link observes final initialized value");
}


void test_assign_overlay(
    test_state& tests) {

    compiled_fixture fixture;

    if (!build_fixture(
            tests,
            fixture)) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::
                    success,
            "prepare Assign overlay baseline")) {

        return;
    }

    compiled_project_view baseline;

    if (!tests.expect(
            baseline.bind(
                image.bytes) ==
                    compiled_project_image_result::
                        success &&
            baseline.assign_count() == 4,
            "bind Assign overlay baseline")) {

        return;
    }

    struct collected_assigns final {
        std::vector<file_id> files;
        std::vector<std::string> sources;
        std::vector<std::string> targets;
    };

    const auto visitor =
        [](void* context,
           file_id file,
           std::string_view source,
           std::string_view target) noexcept
        -> server_status {

            auto& output =
                *static_cast<
                    collected_assigns*>(
                        context);

            try {
                output.files.push_back(
                    file);

                output.sources.emplace_back(
                    source);

                output.targets.emplace_back(
                    target);

                return server_status::success;
            }
            catch (...) {
                return server_status::io_error;
            }
        };

    {
        assign_table replacements;

        tests.expect(
            succeeded(
                replacements.add(
                    file_id{5},
                    "replacement.source",
                    "replacement.target")),
            "prepare replacement Assign group");

        const std::array<file_id, 1>
            replaced{file_id{5}};

        assign_overlay_view overlay;
        collected_assigns values;

        tests.expect(
            succeeded(
                overlay.bind(
                    baseline,
                    replacements,
                    replaced)) &&
            overlay.size() == 1 &&
            overlay.byte_size() ==
                std::string_view{
                    "replacement.source"}.size() +
                std::string_view{
                    "replacement.target"}.size() &&
            succeeded(
                overlay.visit(
                    &values,
                    visitor)) &&
            values.files.size() == 1 &&
            values.files[0] ==
                file_id{5} &&
            values.sources[0] ==
                "replacement.source" &&
            values.targets[0] ==
                "replacement.target",
            "changed Assign file atomically replaces all OLD records");
    }

    {
        assign_table replacements;

        const std::array<file_id, 1>
            replaced{file_id{5}};

        assign_overlay_view overlay;
        collected_assigns values;

        tests.expect(
            succeeded(
                overlay.bind(
                    baseline,
                    replacements,
                    replaced)) &&
            overlay.size() == 0 &&
            overlay.byte_size() == 0 &&
            succeeded(
                overlay.visit(
                    &values,
                    visitor)) &&
            values.files.empty(),
            "removed or empty Assign file removes OLD group");
    }

    {
        assign_table replacements;

        tests.expect(
            succeeded(
                replacements.add(
                    file_id{6},
                    "new.source",
                    "new.target")),
            "prepare appended Assign group");

        const std::array<file_id, 1>
            replaced{file_id{6}};

        assign_overlay_view overlay;
        collected_assigns values;

        tests.expect(
            succeeded(
                overlay.bind(
                    baseline,
                    replacements,
                    replaced)) &&
            overlay.size() == 5 &&
            succeeded(
                overlay.visit(
                    &values,
                    visitor)) &&
            values.files.size() == 5 &&
            values.files[0] ==
                file_id{5} &&
            values.files[3] ==
                file_id{5} &&
            values.files[4] ==
                file_id{6} &&
            values.sources[4] ==
                "new.source",
            "unaffected mmap Assign groups stay ordered before appended replacement group");
    }
}


void test_graph_dense_projection(
    test_state& tests) {

    string_table strings;
    identity_space identities{
        strings};
    graph_delta G;

    string_id dead_name;
    string_id live_name;
    string_id value_name;
    string_id input_name;
    string_id a_name;
    string_id b_name;
    string_id c_name;

    const auto intern =
        [&](std::string_view text,
            string_id& output) {
            return succeeded(
                strings.intern(
                    text,
                    output));
        };

    if (!tests.expect(
            intern("Dead", dead_name) &&
            intern("Live", live_name) &&
            intern("value", value_name) &&
            intern("input", input_name) &&
            intern("a", a_name) &&
            intern("b", b_name) &&
            intern("c", c_name),
            "prepare dense projection strings")) {

        return;
    }

    identity_ref dead_identity;
    identity_ref live_identity;
    identity_ref a_identity;
    identity_ref b_identity;
    identity_ref c_identity;

    if (!tests.expect(
            succeeded(
                identities.resolve(
                    identities.root(),
                    dead_name,
                    identity_kind::type,
                    dead_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    live_name,
                    identity_kind::type,
                    live_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    a_name,
                    identity_kind::object,
                    a_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    b_name,
                    identity_kind::object,
                    b_identity)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    c_name,
                    identity_kind::object,
                    c_identity)),
            "prepare dense projection identities")) {

        return;
    }

    type_handle dead_type;
    type_handle live_type;

    if (!tests.expect(
            succeeded(
                G.declare_record(
                    dead_identity,
                    graph_record_kind::struct_type,
                    dead_type)) &&
            succeeded(
                G.define_record(
                    dead_type,
                    graph_record_kind::struct_type,
                    {})) &&
            succeeded(
                G.declare_record(
                    live_identity,
                    graph_record_kind::struct_type,
                    live_type)),
            "prepare dense projection lineage types")) {

        return;
    }

    const auto integer =
        G.intrinsic(
            intrinsic_type::signed_int);

    type_ref integer_reference;

    if (!tests.expect(
            succeeded(
                G.derive(
                    integer,
                    derived_type_kind::lvalue_reference,
                    0,
                    integer_reference)),
            "prepare dense projection reference type")) {

        return;
    }

    const std::array<member_record, 2>
        members{{
            {
                value_name,
                integer,
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
                    live_type,
                    graph_record_kind::struct_type,
                    members)) &&
            succeeded(
                G.retire(
                    dead_type)),
            "retire first lineage type before dense projection")) {

        return;
    }

    const auto live_named =
        G.named(
            live_type);

    type_ref live_pointer;

    if (!tests.expect(
            succeeded(
                G.derive(
                    live_named,
                    derived_type_kind::pointer,
                    0,
                    live_pointer)),
            "prepare derived type that references remapped named type")) {

        return;
    }

    object_handle a;
    object_handle b;
    object_handle c;

    if (!tests.expect(
            succeeded(
                G.add_object(
                    a_identity,
                    live_named,
                    a)) &&
            succeeded(
                G.add_object(
                    b_identity,
                    live_named,
                    b)) &&
            succeeded(
                G.add_object(
                    c_identity,
                    live_named,
                    c)),
            "prepare dense projection objects")) {

        return;
    }

    const auto value_member =
        G.find_member(
            live_type,
            value_name);

    const auto input_member =
        G.find_member(
            live_type,
            input_name);

    link_handle first_link;
    link_handle second_link;

    if (!tests.expect(
            value_member &&
            input_member &&
            succeeded(
                G.add_link(
                    {
                        a_identity,
                        endpoint_ref{
                            value_member},
                    },
                    {
                        b_identity,
                        endpoint_ref{
                            input_member},
                    },
                    first_link)) &&
            succeeded(
                G.add_link(
                    {
                        c_identity,
                        endpoint_ref{
                            value_member},
                    },
                    {
                        a_identity,
                        endpoint_ref{
                            input_member},
                    },
                    second_link)),
            "prepare dense projection links")) {

        return;
    }

    bool replaced = false;

    if (!tests.expect(
            succeeded(
                G.add_initialization(
                    {
                        c_identity,
                        endpoint_ref{
                            value_member},
                    },
                    construction_value::constant(
                        construction_kind::
                            signed_integer,
                        17),
                    replaced)) &&
            !replaced,
            "prepare dense projection initialization")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                G.retire(
                    first_link)) &&
            succeeded(
                G.retire(
                    b)),
            "create object/link tombstones before final projection")) {

        return;
    }

    graph_dense_projection projection;

    if (!tests.expect(
            succeeded(
                projection.prepare(
                    G)),
            "prepare dense final G projection")) {

        return;
    }

    const auto remapped_live_type =
        projection.remap(
            live_type);

    const auto remapped_live_named =
        projection.remap(
            live_named);

    const auto remapped_reference =
        projection.remap(
            integer_reference);

    const auto remapped_pointer =
        projection.remap(
            live_pointer);

    tests.expect(
        projection.type_count() == 1 &&
        projection.object_count() == 2 &&
        projection.link_count() == 1 &&
        projection.derived_type_count() == 2 &&
        !projection.remap(
            dead_type) &&
        remapped_live_type &&
        remapped_live_type.value() == 1 &&
        remapped_live_named &&
        remapped_live_named.kind() ==
            type_ref_kind::named &&
        remapped_live_named ==
            live_named &&
        remapped_reference &&
        remapped_pointer,
        "dense projection compacts type lineage without runtime tombstones");

    tests.expect(
        projection.remap(a).value() == 1 &&
        !projection.remap(b) &&
        projection.remap(c).value() == 2 &&
        !projection.remap(first_link) &&
        projection.remap(second_link).value() == 1,
        "dense projection compacts object/link lineage in original order");

    object_endpoint remapped_endpoint;

    tests.expect(
        projection.remap(
            {
                c_identity,
                endpoint_ref{
                    value_member},
            },
            remapped_endpoint) &&
        remapped_endpoint.object ==
            c_identity &&
        remapped_endpoint.member ==
            endpoint_ref{
                value_member},
        "dense projection remaps object endpoints and preserves local member indices");

    construction_value remapped_binding;

    tests.expect(
        projection.remap(
            construction_value::
                object_binding(
                    c_identity.value()),
            remapped_binding) &&
        remapped_binding.kind ==
            construction_kind::
                object_binding &&
        remapped_binding.operand ==
            c_identity.value(),
        "dense projection preserves object-binding semantic identity");
}


void test_graph_delta_initialization_overlay(
    test_state& tests) {

    compiled_fixture fixture;

    if (!build_fixture(
            tests,
            fixture)) {

        return;
    }

    const object_endpoint baseline_target{
        fixture.left_identity,
        endpoint_ref{
            fixture.value_member},
    };

    bool replaced = false;

    if (!tests.expect(
            succeeded(
                fixture.G.add_initialization(
                    baseline_target,
                    construction_value::constant(
                        construction_kind::
                            signed_integer,
                        11),
                    replaced)) &&
            !replaced,
            "prepare graph_delta initialization baseline")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::
                    success,
            "persist graph_delta initialization baseline")) {

        return;
    }

    compiled_project_view baseline;
    graph_delta delta;

    if (!tests.expect(
            baseline.bind(
                image.bytes) ==
                    compiled_project_image_result::
                        success &&
            baseline.initialization_count() == 1 &&
            succeeded(
                delta.bind_baseline(
                    baseline)) &&
            delta.initialization_count() == 1,
            "bind mmap initialization baseline without reconstruction")) {

        return;
    }

    object_initialization_record value;

    tests.expect(
        delta.initialization(
            baseline_target,
            value) &&
        value.value ==
            construction_value::constant(
                construction_kind::
                    signed_integer,
                11),
        "graph_delta reads unchanged initialization from compiled mmap");

    std::vector<
        graph_delta_initialization_change>
        initialization_changes;

    const auto collect_initialization_change =
        [](void* context,
           const graph_delta_initialization_change& change) noexcept
        -> server_status {

            try {
                static_cast<
                    std::vector<
                        graph_delta_initialization_change>*>(
                            context)->push_back(
                                change);

                return server_status::success;
            }
            catch (...) {
                return server_status::io_error;
            }
        };

    tests.expect(
        succeeded(
            delta.visit_initialization_changes(
                &initialization_changes,
                collect_initialization_change)) &&
        initialization_changes.empty() &&
        delta.initialization_change_count() == 0,
        "sparse initialization write set ignores unchanged mmap baseline");

    replaced = false;

    tests.expect(
        succeeded(
            delta.add_initialization(
                baseline_target,
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    22),
                replaced)) &&
        replaced &&
        delta.initialization_count() == 1 &&
        delta.initialization(
            baseline_target,
            value) &&
        value.value ==
            construction_value::constant(
                construction_kind::
                    signed_integer,
                22),
        "graph_delta sparsely replaces one baseline initialization");

    tests.expect(
        succeeded(
            delta.invalidate_initialization(
                baseline_target)) &&
        !delta.initialization(
            baseline_target,
            value) &&
        delta.initialization_count() == 0 &&
        succeeded(
            delta.invalidate_initialization(
                baseline_target)) &&
        delta.initialization_count() == 0,
        "graph_delta initialization invalidation is idempotent for shared producers");

    initialization_changes.clear();

    tests.expect(
        succeeded(
            delta.visit_initialization_changes(
                &initialization_changes,
                collect_initialization_change)) &&
        initialization_changes.size() == 1 &&
        initialization_changes[0].kind ==
            graph_delta_change_kind::patch &&
        initialization_changes[0].value.target ==
            baseline_target &&
        !initialization_changes[0].live,
        "sparse initialization write set carries baseline invalidation");

    replaced = true;

    tests.expect(
        succeeded(
            delta.add_initialization(
                baseline_target,
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    33),
                replaced)) &&
        !replaced &&
        delta.initialization_count() == 1 &&
        delta.initialization(
            baseline_target,
            value) &&
        value.value ==
            construction_value::constant(
                construction_kind::
                    signed_integer,
                33),
        "replay after tombstone restores target without false duplicate warning");

    const object_endpoint appended_target{
        fixture.right_identity,
        endpoint_ref{
            fixture.value_member},
    };

    replaced = true;

    tests.expect(
        succeeded(
            delta.add_initialization(
                appended_target,
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    44),
                replaced)) &&
        !replaced &&
        delta.initialization_count() == 2 &&
        delta.initialization(
            appended_target,
            value) &&
        value.value ==
            construction_value::constant(
                construction_kind::
                    signed_integer,
                44),
        "graph_delta appends new canonical initialization without baseline scan");

    initialization_changes.clear();

    tests.expect(
        succeeded(
            delta.visit_initialization_changes(
                &initialization_changes,
                collect_initialization_change)) &&
        initialization_changes.size() == 2 &&
        initialization_changes[0].kind ==
            graph_delta_change_kind::patch &&
        initialization_changes[0].live &&
        initialization_changes[1].kind ==
            graph_delta_change_kind::append &&
        initialization_changes[1].live &&
        initialization_changes[1].value.target ==
            appended_target,
        "sparse initialization write set separates baseline patch from append");

    replaced = false;

    tests.expect(
        succeeded(
            delta.add_initialization(
                appended_target,
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    55),
                replaced)) &&
        replaced &&
        delta.initialization_count() == 2 &&
        delta.initialization(
            appended_target,
            value) &&
        value.value ==
            construction_value::constant(
                construction_kind::
                    signed_integer,
                55),
        "graph_delta preserves canonical last-wins initialization semantics");
}


void test_runtime_query(
    test_state& tests,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind Runtime query image")) {

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
            "prepare Runtime query layout")) {

        return;
    }

    std::vector<std::byte> runtime(
        static_cast<std::size_t>(
            layout.size()),
        std::byte{0xcc});

    if (!tests.expect(
            materialize_fixed_direct(
                view,
                layout,
                abi,
                runtime) ==
                fixed_direct_materialization_result::success,
            "materialize Runtime query image")) {

        return;
    }

    runtime_binding_index bindings;

    if (!tests.expect(
            layout.release_bindings(
                bindings),
            "publish Runtime query bindings")) {

        return;
    }

    auto& system =
        *reinterpret_cast<runtime_system*>(
            runtime.data());

    if (!tests.expect(
            system.state == 0 &&
            system.current_cycle == 0 &&
            system.snapshot_generation == 0,
            "FIXED_DIRECT materializes zero-initialized System at SHM offset zero")) {

        return;
    }

    system.state = 2;
    system.current_cycle = 123;
    system.completed_cycle = 122;
    system.target_cycle = 200;
    system.server_datetime_ns = 456;
    system.value_generation = 7;
    system.snapshot_generation = 8;
    system.current_ic_generation = 9;

    const auto expect_value =
        [&](std::string_view name,
            std::uint64_t expected,
            std::string_view label) {

            runtime_value value;

            tests.expect(
                get_runtime_value(
                    view,
                    bindings,
                    runtime,
                    name,
                    value) ==
                    runtime_query_result::success &&
                value.type ==
                    intrinsic_type::signed_int &&
                value.size == 4 &&
                value.bits == expected,
                label);
        };

    expect_value(
        "demo::left.value",
        42,
        "GET_VALUE resolves direct member");

    expect_value(
        "demo::right.peer",
        42,
        "GET_VALUE follows native reference");

    expect_value(
        "demo::scalar",
        7,
        "GET_VALUE resolves scalar object");

    runtime_value value;

    tests.expect(
        get_runtime_value(
            view,
            bindings,
            runtime,
            "System.current_cycle",
            value) ==
                runtime_query_result::success &&
        value.type ==
            intrinsic_type::unsigned_long_long &&
        value.size ==
            sizeof(std::uint64_t) &&
        value.bits == 123,
        "GET_VALUE reads built-in System directly from SHM");

    tests.expect(
        get_runtime_value(
            view,
            bindings,
            runtime,
            "::System.snapshot_generation",
            value) ==
                runtime_query_result::success &&
        value.bits == 8,
        "GET_VALUE accepts global System name");

    tests.expect(
        get_runtime_value(
            view,
            bindings,
            runtime,
            "System.missing",
            value) ==
                runtime_query_result::not_found,
        "GET_VALUE reports missing System member");

    tests.expect(
        get_runtime_value(
            view,
            bindings,
            runtime,
            "demo::missing.value",
            value) ==
            runtime_query_result::not_found,
        "GET_VALUE reports missing object");

    tests.expect(
        get_runtime_value(
            view,
            bindings,
            runtime,
            "demo::left",
            value) ==
            runtime_query_result::unsupported_type,
        "GET_VALUE rejects aggregate leaf");

#if defined(_WIN32)
    const server_abi_configuration abi32{
        abi_target::windows_x86,
        8,
    };

    runtime_layout layout32;

    if (!tests.expect(
            prepare_runtime_layout(
                view,
                abi32,
                layout32) ==
                runtime_layout_result::success,
            "prepare windows-x86 Runtime query layout")) {

        return;
    }

    std::vector<std::byte> runtime32(
        static_cast<std::size_t>(
            layout32.size()),
        std::byte{0xcc});

    constexpr std::uint64_t
        target_base32 =
            0x10000000ull;

    if (!tests.expect(
            materialize_fixed_direct(
                view,
                layout32,
                abi32,
                target_base32,
                runtime32) ==
                    fixed_direct_materialization_result::
                        success,
            "materialize windows-x86 Runtime query image")) {

        return;
    }

    runtime_binding_index bindings32;

    if (!tests.expect(
            layout32.release_bindings(
                bindings32,
                target_base32),
            "publish windows-x86 Runtime query bindings")) {

        return;
    }

    runtime_value direct32;
    runtime_value reference32;

    tests.expect(
        get_runtime_value(
            view,
            bindings32,
            runtime32,
            "demo::left.value",
            direct32) ==
                runtime_query_result::success &&
        direct32.bits == 42 &&
        get_runtime_value(
            view,
            bindings32,
            runtime32,
            "demo::right.peer",
            reference32) ==
                runtime_query_result::success &&
        reference32.bits == 42,
        "GET_VALUE decodes windows-x86 reference slots with target ABI width");
#endif
}


void test_runtime_object_query(
    test_state& tests,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind GET_OBJECT query image")) {

        return;
    }

    runtime_object_query result;

    if (!tests.expect(
            get_runtime_object(
                view,
                "System",
                {},
                result) ==
                    runtime_query_result::success &&
            !result.pattern &&
            result.name ==
                "System" &&
            result.type ==
                "System",
            "GET_OBJECT exact exposes built-in System")) {

        return;
    }

    if (!tests.expect(
            get_runtime_object(
                view,
                "*",
                {},
                result) ==
                    runtime_query_result::success &&
            result.pattern &&
            result.objects.size() == 4 &&
            result.objects[0].name ==
                "System" &&
            result.objects[0].file.empty(),
            "GET_OBJECT wildcard exposes System first without fake source provenance")) {

        return;
    }

    if (!tests.expect(
            get_runtime_object(
                view,
                "demo::left",
                {},
                result) ==
                    runtime_query_result::success &&
            !result.pattern &&
            result.name ==
                "demo::left" &&
            result.type ==
                "demo::Widget" &&
            result.objects.empty(),
            "GET_OBJECT exact returns canonical name and type")) {

        return;
    }

    tests.expect(
        get_runtime_object(
            view,
            "::demo::left",
            "::demo::Widget",
            result) ==
                runtime_query_result::success &&
        !result.pattern &&
        result.name ==
            "demo::left" &&
        result.type ==
            "demo::Widget",
        "GET_OBJECT exact normalizes leading global scope and type filter");

    tests.expect(
        get_runtime_object(
            view,
            "demo::left",
            "int",
            result) ==
                runtime_query_result::not_found,
        "GET_OBJECT exact rejects nonmatching type filter");

    if (!tests.expect(
            get_runtime_object(
                view,
                "demo::*",
                {},
                result) ==
                    runtime_query_result::success &&
            result.pattern &&
            result.name.empty() &&
            result.type.empty() &&
            result.objects.size() == 3,
            "GET_OBJECT wildcard returns matching object list")) {

        return;
    }

    tests.expect(
        result.objects[0].name ==
                "demo::left" &&
        result.objects[1].name ==
                "demo::right" &&
        result.objects[2].name ==
                "demo::scalar",
        "GET_OBJECT wildcard preserves G object order");

    bool provenance_valid = true;

    for (const auto& object :
         result.objects) {

        provenance_valid =
            provenance_valid &&
            std::string_view{
                object.file}.ends_with(
                    "objects.source");
    }

    tests.expect(
        provenance_valid,
        "GET_OBJECT wildcard returns physical source provenance");

    if (!tests.expect(
            get_runtime_object(
                view,
                "demo::ri??t",
                {},
                result) ==
                    runtime_query_result::success &&
            result.pattern &&
            result.objects.size() == 1 &&
            result.objects[0].name ==
                "demo::right",
            "GET_OBJECT question-mark wildcard matches one character")) {

        return;
    }

    if (!tests.expect(
            get_runtime_object(
                view,
                "*",
                "demo::Widget",
                result) ==
                    runtime_query_result::success &&
            result.pattern &&
            result.objects.size() == 2 &&
            result.objects[0].name ==
                "demo::left" &&
            result.objects[1].name ==
                "demo::right",
            "GET_OBJECT wildcard applies named-type filter")) {

        return;
    }

    tests.expect(
        get_runtime_object(
            view,
            "*",
            "int",
            result) ==
                runtime_query_result::success &&
        result.pattern &&
        result.objects.size() == 1 &&
        result.objects[0].name ==
            "demo::scalar",
        "GET_OBJECT wildcard applies intrinsic-type filter");

    tests.expect(
        get_runtime_object(
            view,
            "::demo::*",
            {},
            result) ==
                runtime_query_result::success &&
        result.pattern &&
        result.objects.size() == 3,
        "GET_OBJECT wildcard accepts leading global scope");

    tests.expect(
        get_runtime_object(
            view,
            {},
            {},
            result) ==
                runtime_query_result::invalid_input,
        "GET_OBJECT rejects empty name");
}


void test_runtime_type_query(
    test_state& tests,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind GET_TYPE query image")) {

        return;
    }

    const std::vector<std::string>
        objects{
            "demo::left",
            "demo::right",
            "demo::scalar",
        };

    runtime_type_query result;

    const std::vector<std::string>
        system_object{
            "::System",
        };

    if (!tests.expect(
            get_runtime_type(
                view,
                system_object,
                result) ==
                    runtime_query_result::success &&
            result.types.size() == 1 &&
            result.types[0].object ==
                "System" &&
            result.types[0].type.name ==
                "System" &&
            result.types[0].type.bases.empty() &&
            result.types[0].type.members.size() == 10 &&
            result.types[0].type.members[0].name ==
                "state" &&
            result.types[0].type.members[2].name ==
                "current_cycle" &&
            result.types[0].type.members[9].name ==
                "current_ic_generation",
            "GET_TYPE exposes built-in System ABI")) {

        return;
    }

    if (!tests.expect(
            get_runtime_type(
                view,
                objects,
                result) ==
                    runtime_query_result::success &&
            result.types.size() == 3,
            "GET_TYPE returns one type spec per object")) {

        return;
    }

    tests.expect(
        result.types[0].object ==
                "demo::left" &&
        result.types[0].type.name ==
                "demo::Widget" &&
        result.types[0].type.bases.empty() &&
        result.types[0].type.members.size() == 2 &&
        result.types[0].type.members[0].name ==
                "value" &&
        result.types[0].type.members[0].type ==
                "int" &&
        result.types[0].type.members[1].name ==
                "peer" &&
        result.types[0].type.members[1].type ==
                "int&",
        "GET_TYPE returns named record specification");

    tests.expect(
        result.types[1].object ==
                "demo::right" &&
        result.types[1].type.name ==
                "demo::Widget" &&
        result.types[1].type.members.size() == 2,
        "GET_TYPE preserves input order and repeated type specs");

    tests.expect(
        result.types[2].object ==
                "demo::scalar" &&
        result.types[2].type.name ==
                "int" &&
        result.types[2].type.bases.empty() &&
        result.types[2].type.members.empty(),
        "GET_TYPE returns intrinsic type specification");

    const std::vector<std::string>
        global{
            "::demo::left",
        };

    tests.expect(
        get_runtime_type(
            view,
            global,
            result) ==
                runtime_query_result::success &&
        result.types.size() == 1 &&
        result.types[0].object ==
            "demo::left",
        "GET_TYPE accepts leading global scope");

    const std::vector<std::string>
        missing{
            "demo::missing",
        };

    tests.expect(
        get_runtime_type(
            view,
            missing,
            result) ==
                runtime_query_result::not_found &&
        result.types.empty(),
        "GET_TYPE fails closed on missing object");

    const std::vector<std::string>
        wildcard{
            "demo::*",
        };

    tests.expect(
        get_runtime_type(
            view,
            wildcard,
            result) ==
                runtime_query_result::not_found,
        "GET_TYPE treats wildcard text as an ordinary exact object name");

    const std::vector<std::string> empty;

    tests.expect(
        get_runtime_type(
            view,
            empty,
            result) ==
                runtime_query_result::invalid_input,
        "GET_TYPE requires at least one object");
}


void test_runtime_link_query(
    test_state& tests,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind GET_LINK query image")) {

        return;
    }

    const std::vector<std::string>
        objects{
            "demo::left",
            "demo::right",
            "demo::scalar",
        };

    runtime_link_query result;

    if (!tests.expect(
            get_runtime_link(
                view,
                objects,
                result) ==
                    runtime_query_result::success &&
            result.objects.size() == 3,
            "GET_LINK returns one result per object")) {

        return;
    }

    tests.expect(
        result.objects[0].object ==
                "demo::left" &&
        result.objects[0].links.size() == 1 &&
        result.objects[0].links[0].source ==
                "demo::left.value" &&
        result.objects[0].links[0].target ==
                "demo::right.peer" &&
        std::string_view{
            result.objects[0].links[0].file}.ends_with(
                "objects.source"),
        "GET_LINK returns all Graph links touching object");

    tests.expect(
        result.objects[1].object ==
                "demo::right" &&
        result.objects[1].links.size() == 1 &&
        result.objects[1].links[0].source ==
                "demo::left.value" &&
        result.objects[1].links[0].target ==
                "demo::right.peer",
        "GET_LINK reports same Graph link for both participating objects");

    tests.expect(
        result.objects[0].assigns.size() == 2 &&
        result.objects[0].assigns[0].source ==
                "demo::right.value" &&
        result.objects[0].assigns[0].target ==
                "demo::left.peer" &&
        result.objects[0].assigns[1].source ==
                "remote" &&
        result.objects[0].assigns[1].target ==
                "demo::left" &&
        std::string_view{
            result.objects[0].assigns[0].file}.ends_with(
                "wiring.assign") &&
        std::string_view{
            result.objects[0].assigns[1].file}.ends_with(
                "wiring.assign"),
        "GET_LINK associates raw Assign endpoints textually");

    tests.expect(
        result.objects[1].assigns.size() == 1 &&
        result.objects[1].assigns[0].source ==
                "demo::right.value" &&
        result.objects[1].assigns[0].target ==
                "demo::left.peer",
        "GET_LINK associates Assign source object");

    tests.expect(
        result.objects[2].links.empty() &&
        result.objects[2].assigns.empty(),
        "GET_LINK returns empty collections for unconnected object");

    const std::vector<std::string>
        missing{
            "demo::left",
            "demo::missing",
        };

    tests.expect(
        get_runtime_link(
            view,
            missing,
            result) ==
                runtime_query_result::not_found &&
        result.objects.empty(),
        "GET_LINK batch fails closed on missing object");

    const std::vector<std::string> empty;

    tests.expect(
        get_runtime_link(
            view,
            empty,
            result) ==
                runtime_query_result::invalid_input,
        "GET_LINK requires at least one object");
}


void test_runtime_ic_cross_generation(
    test_state& tests,
    const compiled_fixture& first_fixture,
    const compiled_test_image& first_image) {

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
        [&](const compiled_test_image& image,
            compiled_project_view& view,
            runtime_binding_index& bindings,
            std::vector<std::byte>& runtime) {

            if (view.bind(
                    image.bytes) !=
                    compiled_project_image_result::success) {

                return false;
            }

            runtime_layout layout;

            if (!fixed_direct_host_compatible(
                    abi) ||
                prepare_runtime_layout(
                    view,
                    abi,
                    layout) !=
                    runtime_layout_result::success) {

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

            if (materialize_fixed_direct(
                    view,
                    layout,
                    abi,
                    runtime) !=
                    fixed_direct_materialization_result::
                        success) {

                return false;
            }

            return layout.release_bindings(
                bindings);
        };

    compiled_project_view first_view;
    runtime_binding_index first_bindings;
    std::vector<std::byte> first_runtime;

    if (!tests.expect(
            prepare_runtime(
                first_image,
                first_view,
                first_bindings,
                first_runtime),
            "prepare G1 Runtime IC fixture")) {

        return;
    }

    const std::array<std::string_view, 2>
        left_object{{
            "demo",
            "left",
        }};

    const std::array<std::string_view, 1>
        value_member{{
            "value",
        }};

    const runtime_ic_path_view left_value{
        left_object,
        value_member,
    };

    runtime_ic_scalar_target first_target;
    runtime_ic_scalar_value snapshot;

    if (!tests.expect(
            resolve_runtime_ic_scalar(
                first_view,
                first_bindings,
                first_runtime.size(),
                left_value,
                first_target) ==
                    runtime_ic_result::success &&
            snapshot_runtime_ic_scalar(
                first_view,
                first_bindings,
                first_runtime,
                left_value,
                snapshot) ==
                    runtime_ic_result::success &&
            snapshot.type ==
                intrinsic_type::signed_int &&
            snapshot.size == sizeof(std::int32_t),
            "SNAP IC resolves G1 semantic scalar")) {

        return;
    }

    std::int32_t snapped = 0;

    std::memcpy(
        &snapped,
        snapshot.bytes.data(),
        sizeof(snapped));

    if (!tests.expect(
            snapped == 42,
            "SNAP IC reads G1 Runtime value")) {

        return;
    }

    compiled_fixture second_fixture;

    string_id prefix_name;
    identity_ref prefix_identity;
    object_handle prefix_object;

    if (!tests.expect(
            succeeded(
                second_fixture.strings.intern(
                    "__ic_generation_prefix",
                    prefix_name)) &&
            succeeded(
                second_fixture.identities.resolve(
                    second_fixture.identities.root(),
                    prefix_name,
                    identity_kind::object,
                    prefix_identity)) &&
            succeeded(
                second_fixture.G.add_object(
                    prefix_identity,
                    second_fixture.G.intrinsic(
                        intrinsic_type::signed_int),
                    prefix_object,
                    graph_object_non_default_initializer,
                    construction_value::constant(
                        construction_kind::signed_integer,
                        99))),
            "prepare G2 slot-shift prefix")) {

        return;
    }

    if (!build_fixture(
            tests,
            second_fixture)) {

        return;
    }

    compiled_test_image second_image;

    if (!tests.expect(
            build_test_compiled_image(
                second_fixture,
                second_image) ==
                compiled_project_image_result::success,
            "build G2 Runtime IC image")) {

        return;
    }

    compiled_project_view second_view;
    runtime_binding_index second_bindings;
    std::vector<std::byte> second_runtime;

    if (!tests.expect(
            prepare_runtime(
                second_image,
                second_view,
                second_bindings,
                second_runtime),
            "prepare G2 Runtime IC fixture")) {

        return;
    }

    runtime_ic_scalar_target second_target;

    if (!tests.expect(
            resolve_runtime_ic_scalar(
                second_view,
                second_bindings,
                second_runtime.size(),
                left_value,
                second_target) ==
                    runtime_ic_result::success,
            "RESET IC resolves same semantic scalar in G2")) {

        return;
    }

    tests.expect(
        first_fixture.left.value() !=
                second_fixture.left.value() &&
            first_fixture.left_name.value() !=
                second_fixture.left_name.value(),
        "IC semantic identity does not depend on G-local slots");

    // Runtime offsets are layout results, not semantic identity. A generation
    // change may move a value, but equal offsets are also valid by coincidence.
    // The RESET below is the actual proof that IC resolution uses current G2
    // bindings rather than any persisted G1 Runtime offset.

    const std::int32_t changed = 5;

    std::memcpy(
        second_runtime.data() +
            static_cast<std::size_t>(
                second_target.offset),
        &changed,
        sizeof(changed));

    if (!tests.expect(
            reset_runtime_ic_scalar(
                second_view,
                second_bindings,
                second_runtime,
                left_value,
                snapshot) ==
                runtime_ic_result::success,
            "RESET IC applies G1 value through current G2 bindings")) {

        return;
    }

    runtime_ic_scalar_value observed;

    if (!tests.expect(
            snapshot_runtime_ic_scalar(
                second_view,
                second_bindings,
                second_runtime,
                left_value,
                observed) ==
                runtime_ic_result::success,
            "read G2 value after RESET IC")) {

        return;
    }

    std::int32_t restored = 0;

    std::memcpy(
        &restored,
        observed.bytes.data(),
        sizeof(restored));

    tests.expect(
        restored == 42,
        "RESET IC restores G1 value into G2 Runtime");

    std::memcpy(
        second_runtime.data() +
            static_cast<std::size_t>(
                second_target.offset),
        &changed,
        sizeof(changed));

    auto incompatible = snapshot;
    incompatible.type =
        intrinsic_type::unsigned_int;

    tests.expect(
        reset_runtime_ic_scalar(
            second_view,
            second_bindings,
            second_runtime,
            left_value,
            incompatible) ==
            runtime_ic_result::type_mismatch,
        "RESET IC rejects incompatible G2 type before write");

    std::int32_t unchanged = 0;

    std::memcpy(
        &unchanged,
        second_runtime.data() +
            static_cast<std::size_t>(
                second_target.offset),
        sizeof(unchanged));

    tests.expect(
        unchanged == changed,
        "failed RESET IC leaves Runtime value unchanged");

    const std::array<std::string_view, 2>
        right_object{{
            "demo",
            "right",
        }};

    const std::array<std::string_view, 1>
        peer_member{{
            "peer",
        }};

    runtime_ic_scalar_value reference_value;

    tests.expect(
        snapshot_runtime_ic_scalar(
            second_view,
            second_bindings,
            second_runtime,
            {
                right_object,
                peer_member,
            },
            reference_value) ==
            runtime_ic_result::unsupported_type,
        "SNAP IC never persists native reference slots");

    const std::array<std::string_view, 2>
        scalar_object{{
            "demo",
            "scalar",
        }};

    runtime_ic_scalar_value scalar_value;

    if (!tests.expect(
            snapshot_runtime_ic_scalar(
                second_view,
                second_bindings,
                second_runtime,
                {
                    scalar_object,
                    {},
                },
                scalar_value) ==
                    runtime_ic_result::success,
            "SNAP IC supports scalar objects")) {

        return;
    }

    std::int32_t scalar = 0;

    std::memcpy(
        &scalar,
        scalar_value.bytes.data(),
        sizeof(scalar));

    tests.expect(
        scalar == 7,
        "SNAP IC reads scalar-object value");
}



void test_runtime_ic_codec(
    test_state& tests) {

    const std::array<std::string_view, 2>
        left_object{{
            "demo",
            "left",
        }};

    const std::array<std::string_view, 1>
        value_member{{
            "value",
        }};

    const std::array<std::string_view, 2>
        scalar_object{{
            "demo",
            "scalar",
        }};

    runtime_ic_scalar_value left_value;
    left_value.type =
        intrinsic_type::signed_int;
    left_value.size =
        sizeof(std::int32_t);

    const std::int32_t left_number = 42;

    std::memcpy(
        left_value.bytes.data(),
        &left_number,
        sizeof(left_number));

    runtime_ic_scalar_value scalar_value;
    scalar_value.type =
        intrinsic_type::signed_int;
    scalar_value.size =
        sizeof(std::int32_t);

    const std::int32_t scalar_number = 7;

    std::memcpy(
        scalar_value.bytes.data(),
        &scalar_number,
        sizeof(scalar_number));

    const std::array<runtime_ic_record_source, 2>
        records{{
            {
                {
                    left_object,
                    value_member,
                },
                left_value,
            },
            {
                {
                    scalar_object,
                    {},
                },
                scalar_value,
            },
        }};

    runtime_ic_binary_plan plan;

    if (!tests.expect(
            prepare_runtime_ic_binary(
                records,
                plan) ==
                    runtime_ic_codec_result::success &&
            plan.size() != 0 &&
            plan.string_count() == 4 &&
            plan.component_count() == 5 &&
            plan.record_count() == 2,
            "prepare canonical binary IC layout")) {

        return;
    }

    std::vector<std::byte> image;

    try {
        image.assign(
            plan.size(),
            std::byte{0});
    }
    catch (...) {
        tests.expect(
            false,
            "allocate binary IC image");
        return;
    }

    if (!tests.expect(
            encode_runtime_ic_binary(
                records,
                plan,
                image) ==
                runtime_ic_codec_result::success,
            "encode binary IC image")) {

        return;
    }

    runtime_ic_binary_view view;

    if (!tests.expect(
            view.bind(image) ==
                    runtime_ic_codec_result::success &&
            view.string_count() == 4 &&
            view.component_count() == 5 &&
            view.record_count() == 2,
            "bind mmap-style binary IC view")) {

        return;
    }

    runtime_ic_binary_record_view first;

    if (!tests.expect(
            view.record(
                0,
                first) &&
            first.object_count == 2 &&
            first.member_count == 1 &&
            first.type ==
                intrinsic_type::signed_int &&
            first.value.size() ==
                sizeof(std::int32_t),
            "read binary IC record without reconstruction")) {

        return;
    }

    tests.expect(
        view.string(
            view.component(
                first.object_begin)) ==
                "demo" &&
        view.string(
            view.component(
                first.object_begin + 1)) ==
                "left" &&
        view.string(
            view.component(
                first.member_begin)) ==
                "value",
        "binary IC uses file-local string IDs");

    std::int32_t observed = 0;

    std::memcpy(
        &observed,
        first.value.data(),
        sizeof(observed));

    tests.expect(
        observed == left_number,
        "binary IC preserves scalar bytes");

    auto corrupted = image;
    corrupted.back() ^=
        std::byte{1};

    runtime_ic_binary_view invalid;

    tests.expect(
        invalid.bind(
            corrupted) ==
            runtime_ic_codec_result::
                invalid_image,
        "binary IC rejects payload CRC corruption");

    std::string text;

    if (!tests.expect(
            encode_runtime_ic_text(
                records,
                text) ==
                    runtime_ic_codec_result::success &&
            text ==
                "::demo::left.value = 42\n"
                "::demo::scalar = 7\n",
            "encode canonical text IC")) {

        return;
    }

    struct parse_state final {
        std::array<std::string_view, 2> paths{};
        std::array<std::string_view, 2> values{};
        std::size_t count = 0;
    } parsed;

    const auto callback =
        +[](void* context,
            std::string_view path,
            std::string_view value) noexcept {

            auto& state =
                *static_cast<parse_state*>(
                    context);

            if (state.count >=
                state.paths.size()) {

                return false;
            }

            state.paths[state.count] =
                path;

            state.values[state.count] =
                value;

            ++state.count;
            return true;
        };

    std::size_t error_line = 0;

    if (!tests.expect(
            parse_runtime_ic_text(
                text,
                &parsed,
                callback,
                &error_line) ==
                    runtime_ic_codec_result::success &&
            error_line == 0 &&
            parsed.count == 2 &&
            parsed.paths[0] ==
                "::demo::left.value" &&
            parsed.values[0] == "42" &&
            parsed.paths[1] ==
                "::demo::scalar" &&
            parsed.values[1] == "7",
            "parse text IC without document reconstruction")) {

        return;
    }

    runtime_ic_scalar_value parsed_value;

    if (!tests.expect(
            parse_runtime_ic_text_scalar(
                intrinsic_type::signed_int,
                sizeof(std::int32_t),
                parsed.values[0],
                parsed_value) ==
                    runtime_ic_codec_result::success,
            "parse text IC scalar against current target type")) {

        return;
    }

    std::int32_t parsed_number = 0;

    std::memcpy(
        &parsed_number,
        parsed_value.bytes.data(),
        sizeof(parsed_number));

    tests.expect(
        parsed_number == left_number,
        "text IC scalar round-trips");

    parsed = {};
    error_line = 0;

    tests.expect(
        parse_runtime_ic_text(
            "::demo::left.value = 42\n"
            "broken\n",
            &parsed,
            callback,
            &error_line) ==
                runtime_ic_codec_result::
                    invalid_image &&
        parsed.count == 1 &&
        error_line == 2,
        "text IC reports malformed line");
}



void test_runtime_ic_snapshot(
    test_state& tests,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind Runtime IC snapshot image")) {

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
            "prepare Runtime IC snapshot layout")) {

        return;
    }

    std::vector<std::byte> runtime(
        static_cast<std::size_t>(
            layout.size()),
        std::byte{0xcc});

    if (!tests.expect(
            materialize_fixed_direct(
                view,
                layout,
                abi,
                runtime) ==
                fixed_direct_materialization_result::success,
            "materialize Runtime IC snapshot")) {

        return;
    }

    runtime_binding_index bindings;

    if (!tests.expect(
            layout.release_bindings(
                bindings),
            "publish Runtime IC snapshot bindings")) {

        return;
    }

    runtime_ic_snapshot snapshot;

    if (!tests.expect(
            snapshot_runtime_ic_project(
                view,
                bindings,
                runtime,
                snapshot) ==
                    runtime_ic_snapshot_result::success,
            "traverse whole Project Runtime IC snapshot")) {

        return;
    }

    const auto records =
        snapshot.records();

    if (!tests.expect(
            snapshot.stats().objects == 3 &&
            snapshot.stats().scalars == 3 &&
            snapshot.stats().skipped_structural == 2 &&
            snapshot.stats().skipped_unaddressable == 0 &&
            records.size() == 3,
            "Runtime IC snapshot selects semantic scalar leaves")) {

        return;
    }

    std::string text;

    if (!tests.expect(
            encode_runtime_ic_text(
                records,
                text) ==
                    runtime_ic_codec_result::success,
            "encode whole Project Runtime IC snapshot")) {

        return;
    }

    tests.expect(
        text ==
            "::demo::left.value = 42\n"
            "::demo::right.value = 42\n"
            "::demo::scalar = 7\n",
        "Runtime IC snapshot is deterministic semantic order");

    runtime_ic_binary_plan plan;

    if (!tests.expect(
            prepare_runtime_ic_binary(
                records,
                plan) ==
                    runtime_ic_codec_result::success,
            "prepare whole Project binary Runtime IC")) {

        return;
    }

    std::vector<std::byte> binary(
        plan.size(),
        std::byte{0});

    if (!tests.expect(
            encode_runtime_ic_binary(
                records,
                plan,
                binary) ==
                    runtime_ic_codec_result::success,
            "encode whole Project binary Runtime IC")) {

        return;
    }

    runtime_ic_binary_view binary_view;

    tests.expect(
        binary_view.bind(
            binary) ==
                runtime_ic_codec_result::success &&
        binary_view.record_count() ==
            records.size(),
        "whole Project binary Runtime IC is mmap-readable");
}



void test_runtime_ic_reset(
    test_state& tests,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind Runtime IC RESET image")) {

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
            "prepare Runtime IC RESET layout")) {

        return;
    }

    std::vector<std::byte> runtime(
        static_cast<std::size_t>(
            layout.size()),
        std::byte{0xcc});

    if (!tests.expect(
            materialize_fixed_direct(
                view,
                layout,
                abi,
                runtime) ==
                fixed_direct_materialization_result::success,
            "materialize Runtime IC RESET image")) {

        return;
    }

    runtime_binding_index bindings;

    if (!tests.expect(
            layout.release_bindings(
                bindings),
            "publish Runtime IC RESET bindings")) {

        return;
    }

    runtime_ic_snapshot snapshot;

    if (!tests.expect(
            snapshot_runtime_ic_project(
                view,
                bindings,
                runtime,
                snapshot) ==
                    runtime_ic_snapshot_result::success,
            "capture Runtime IC RESET baseline")) {

        return;
    }

    runtime_ic_binary_plan binary_plan;

    if (!tests.expect(
            prepare_runtime_ic_binary(
                snapshot.records(),
                binary_plan) ==
                    runtime_ic_codec_result::success,
            "prepare Runtime IC RESET binary baseline")) {

        return;
    }

    std::vector<std::byte> binary(
        binary_plan.size(),
        std::byte{0});

    if (!tests.expect(
            encode_runtime_ic_binary(
                snapshot.records(),
                binary_plan,
                binary) ==
                    runtime_ic_codec_result::success,
            "encode Runtime IC RESET binary baseline")) {

        return;
    }

    runtime_ic_binary_view binary_view;

    if (!tests.expect(
            binary_view.bind(
                binary) ==
                    runtime_ic_codec_result::success,
            "bind Runtime IC RESET binary baseline")) {

        return;
    }

    std::string text;

    if (!tests.expect(
            encode_runtime_ic_text(
                snapshot.records(),
                text) ==
                    runtime_ic_codec_result::success,
            "encode Runtime IC RESET text baseline")) {

        return;
    }

    const std::array<std::string_view, 2>
        left_object{{
            "demo",
            "left",
        }};

    const std::array<std::string_view, 1>
        value_member{{
            "value",
        }};

    runtime_ic_scalar_value changed;
    changed.type =
        intrinsic_type::signed_int;
    changed.size =
        sizeof(std::int32_t);

    const std::int32_t changed_number = 99;

    std::memcpy(
        changed.bytes.data(),
        &changed_number,
        sizeof(changed_number));

    if (!tests.expect(
            reset_runtime_ic_scalar(
                view,
                bindings,
                runtime,
                {
                    left_object,
                    value_member,
                },
                changed) ==
                    runtime_ic_result::success,
            "mutate Runtime before binary RESET")) {

        return;
    }

    runtime_ic_reset_stats reset_stats;

    if (!tests.expect(
            reset_runtime_ic_binary(
                view,
                bindings,
                runtime,
                binary_view,
                &reset_stats) ==
                    runtime_ic_reset_result::success &&
            reset_stats.records == 3 &&
            reset_stats.bytes == 12,
            "binary RESET validates then applies whole IC")) {

        return;
    }

    runtime_value observed;

    if (!tests.expect(
            get_runtime_value(
                view,
                bindings,
                runtime,
                "demo::left.value",
                observed) ==
                    runtime_query_result::success &&
            observed.bits == 42,
            "binary RESET restores Runtime value")) {

        return;
    }

    if (!tests.expect(
            reset_runtime_ic_scalar(
                view,
                bindings,
                runtime,
                {
                    left_object,
                    value_member,
                },
                changed) ==
                    runtime_ic_result::success,
            "mutate Runtime before filtered binary RESET")) {

        return;
    }

    if (!tests.expect(
            reset_runtime_ic_binary(
                view,
                bindings,
                runtime,
                binary_view,
                &reset_stats,
                reset_ic_options::variables) ==
                    runtime_ic_reset_result::success &&
            reset_stats.records == 0 &&
            reset_stats.bytes == 0 &&
            get_runtime_value(
                view,
                bindings,
                runtime,
                "demo::left.value",
                observed) ==
                    runtime_query_result::success &&
            observed.bits == 99,
            "RESET variables option preserves variable values")) {

        return;
    }

    if (!tests.expect(
            reset_runtime_ic_binary(
                view,
                bindings,
                runtime,
                binary_view,
                &reset_stats) ==
                    runtime_ic_reset_result::success &&
            get_runtime_value(
                view,
                bindings,
                runtime,
                "demo::left.value",
                observed) ==
                    runtime_query_result::success &&
            observed.bits == 42,
            "unfiltered binary RESET restores preserved variable")) {

        return;
    }

    if (!tests.expect(
            reset_runtime_ic_scalar(
                view,
                bindings,
                runtime,
                {
                    left_object,
                    value_member,
                },
                changed) ==
                    runtime_ic_result::success,
            "mutate Runtime before text RESET")) {

        return;
    }

    std::size_t error_line = 0;

    if (!tests.expect(
            reset_runtime_ic_text(
                view,
                bindings,
                runtime,
                text,
                &reset_stats,
                &error_line) ==
                    runtime_ic_reset_result::success &&
            reset_stats.records == 3 &&
            error_line == 0,
            "text RESET resolves current semantic targets")) {

        return;
    }

    if (!tests.expect(
            get_runtime_value(
                view,
                bindings,
                runtime,
                "demo::left.value",
                observed) ==
                    runtime_query_result::success &&
            observed.bits == 42,
            "text RESET restores Runtime value")) {

        return;
    }

    runtime_ic_scalar_value pending_value;
    pending_value.type =
        intrinsic_type::signed_int;
    pending_value.size =
        sizeof(std::int32_t);

    const std::int32_t pending_number = 11;

    std::memcpy(
        pending_value.bytes.data(),
        &pending_number,
        sizeof(pending_number));

    const std::array<std::string_view, 2>
        missing_object{{
            "demo",
            "missing",
        }};

    const std::array<runtime_ic_record_source, 2>
        invalid_records{{
            {
                {
                    left_object,
                    value_member,
                },
                pending_value,
            },
            {
                {
                    missing_object,
                    value_member,
                },
                pending_value,
            },
        }};

    if (!tests.expect(
            reset_runtime_ic_records(
                view,
                bindings,
                runtime,
                invalid_records) ==
                    runtime_ic_reset_result::not_found,
            "RESET rejects invalid semantic record before apply")) {

        return;
    }

    tests.expect(
        get_runtime_value(
            view,
            bindings,
            runtime,
            "demo::left.value",
            observed) ==
                runtime_query_result::success &&
        observed.bits == 42,
        "failed RESET leaves Runtime unchanged");

    tests.expect(
        reset_runtime_ic_text(
            view,
            bindings,
            runtime,
            "::demo::left.value = 17\n"
            "::demo::missing.value = 18\n",
            nullptr,
            &error_line) ==
                runtime_ic_reset_result::not_found &&
        error_line == 2 &&
        get_runtime_value(
            view,
            bindings,
            runtime,
            "demo::left.value",
            observed) ==
                runtime_query_result::success &&
        observed.bits == 42,
        "failed text RESET is atomic");
}


void test_fixed_direct_arrays(
    test_state& tests) {

    compiled_fixture fixture;

    string_id type_name;
    string_id out_name;
    string_id in_name;
    string_id a_name;
    string_id b_name;
    string_id objects_name;

    const auto intern =
        [&](std::string_view value,
            string_id& output) {

            return succeeded(
                fixture.strings.intern(
                    value,
                    output));
        };

    if (!tests.expect(
            intern("ArrayRecord", type_name) &&
            intern("out", out_name) &&
            intern("in", in_name) &&
            intern("a", a_name) &&
            intern("b", b_name) &&
            intern("objects", objects_name),
            "prepare FIXED_DIRECT array strings")) {

        return;
    }

    identity_ref type_identity;
    identity_ref a_identity;
    identity_ref b_identity;
    identity_ref objects_identity;

    if (!tests.expect(
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    type_name,
                    identity_kind::type,
                    type_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    a_name,
                    identity_kind::object,
                    a_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    b_name,
                    identity_kind::object,
                    b_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    objects_name,
                    identity_kind::object,
                    objects_identity)),
            "prepare FIXED_DIRECT array identities")) {

        return;
    }

    type_handle type;

    if (!tests.expect(
            succeeded(
                fixture.G.declare_record(
                    type_identity,
                    graph_record_kind::struct_type,
                    type)),
            "declare FIXED_DIRECT array record")) {

        return;
    }

    const auto integer =
        fixture.G.intrinsic(
            intrinsic_type::signed_int);

    type_ref inner_array;
    type_ref outer_array;
    type_ref array_reference;

    if (!tests.expect(
            integer &&
            succeeded(
                fixture.G.derive(
                    integer,
                    derived_type_kind::bounded_array,
                    3,
                    inner_array)) &&
            succeeded(
                fixture.G.derive(
                    inner_array,
                    derived_type_kind::bounded_array,
                    2,
                    outer_array)) &&
            succeeded(
                fixture.G.derive(
                    outer_array,
                    derived_type_kind::lvalue_reference,
                    0,
                    array_reference)),
            "derive FIXED_DIRECT array types")) {

        return;
    }

    const std::array<member_record, 2>
        members{{
            {
                out_name,
                outer_array,
                graph_member_access::public_access,
            },
            {
                in_name,
                array_reference,
                graph_member_access::public_access,
            },
        }};

    const std::array<construction_value, 2>
        construction{{
            {},
            construction_value::member_binding(1),
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    type,
                    graph_record_kind::struct_type,
                    members,
                    construction)),
            "define FIXED_DIRECT array record")) {

        return;
    }

    const auto named_type =
        fixture.G.named(type);

    type_ref object_array_type;

    if (!tests.expect(
            named_type &&
            succeeded(
                fixture.G.derive(
                    named_type,
                    derived_type_kind::bounded_array,
                    2,
                    object_array_type)),
            "derive FIXED_DIRECT record-array object type")) {

        return;
    }

    object_handle a;
    object_handle b;
    object_handle objects;

    if (!tests.expect(
            succeeded(
                fixture.G.add_object(
                    a_identity,
                    named_type,
                    a)) &&
            succeeded(
                fixture.G.add_object(
                    b_identity,
                    named_type,
                    b)) &&
            succeeded(
                fixture.G.add_object(
                    objects_identity,
                    object_array_type,
                    objects)),
            "add FIXED_DIRECT array objects")) {

        return;
    }

    const auto out =
        fixture.G.find_member(
            type,
            out_name);

    const auto in =
        fixture.G.find_member(
            type,
            in_name);

    link_handle link;

    if (!tests.expect(
            out &&
            in &&
            succeeded(
                fixture.G.add_link(
                    {
                        fixture.G.identity(a),
                        out,
                    },
                    {
                        fixture.G.identity(b),
                        in,
                    },
                    link)),
            "add whole-array FIXED_DIRECT reference link")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.sources.finalize(
                    fixture.files.size(),
                    fixture.identities,
                    fixture.G)),
            "finalize FIXED_DIRECT array Source Map")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::success,
            "encode FIXED_DIRECT array image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success,
            "bind FIXED_DIRECT array image")) {

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
            "prepare FIXED_DIRECT array Runtime layout")) {

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
            "allocate FIXED_DIRECT array Runtime");
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
            "materialize FIXED_DIRECT arrays")) {

        return;
    }

    type_entry record;
    record_offset out_offset = 0;
    record_offset in_offset = 0;
    runtime_offset a_offset = 0;
    runtime_offset b_offset = 0;
    runtime_offset objects_offset = 0;
    runtime_value_layout record_layout;

    if (!tests.expect(
            view.type(
                type,
                record) &&
            layout.member_offset(
                static_cast<std::size_t>(
                    record.members.begin) +
                    out.value(),
                out_offset) &&
            layout.member_offset(
                static_cast<std::size_t>(
                    record.members.begin) +
                    in.value(),
                in_offset) &&
            layout.object_offset(
                a,
                a_offset) &&
            layout.object_offset(
                b,
                b_offset) &&
            layout.object_offset(
                objects,
                objects_offset) &&
            layout.value(
                named_type,
                record_layout),
            "query FIXED_DIRECT array offsets")) {

        return;
    }

    const auto base =
        reinterpret_cast<std::uintptr_t>(
            runtime.data());

    const auto read_address =
        [&](runtime_offset offset) {
            std::uintptr_t value = 0;

            std::memcpy(
                &value,
                runtime.data() +
                    static_cast<std::size_t>(
                        offset),
                sizeof(value));

            return value;
        };

    const auto a_out =
        base +
        static_cast<std::uintptr_t>(
            a_offset +
            out_offset);

    tests.expect(
        read_address(
            a_offset +
                in_offset) ==
                    a_out &&
        read_address(
            b_offset +
                in_offset) ==
                    a_out,
        "default and static-link references bind whole native arrays");

    bool object_array_bindings = true;

    for (std::uint64_t index = 0;
         index < 2;
         ++index) {

        const auto element_offset =
            objects_offset +
            index *
                record_layout.size;

        const auto expected =
            base +
            static_cast<std::uintptr_t>(
                element_offset +
                out_offset);

        object_array_bindings =
            object_array_bindings &&
            read_address(
                element_offset +
                    in_offset) ==
                expected;
    }

    tests.expect(
        object_array_bindings,
        "record-array Project object materializes per-element native self-bindings");

    constexpr std::size_t element =
        5;

    const int written = 37;

    std::memcpy(
        runtime.data() +
            static_cast<std::size_t>(
                a_offset +
                out_offset) +
            element *
                sizeof(int),
        &written,
        sizeof(written));

    const auto linked =
        read_address(
            b_offset +
                in_offset);

    int observed = 0;

    std::memcpy(
        &observed,
        reinterpret_cast<const void*>(
            linked +
            element *
                sizeof(int)),
        sizeof(observed));

    tests.expect(
        observed == written,
        "reference-to-array exposes the same nested-array storage");
}


void test_fixed_direct_subobject_links(
    test_state& tests) {

    compiled_fixture fixture;

    string_id type_name;
    string_id values_name;
    string_id input_name;
    string_id source_name;
    string_id objects_name;

    const auto intern =
        [&](std::string_view value,
            string_id& output) {

            return succeeded(
                fixture.strings.intern(
                    value,
                    output));
        };

    if (!tests.expect(
            intern("SubobjectRecord", type_name) &&
            intern("values", values_name) &&
            intern("in", input_name) &&
            intern("source", source_name) &&
            intern("objects", objects_name),
            "prepare FIXED_DIRECT subobject-link strings")) {

        return;
    }

    identity_ref type_identity;
    identity_ref source_identity;
    identity_ref objects_identity;

    if (!tests.expect(
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    type_name,
                    identity_kind::type,
                    type_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    source_name,
                    identity_kind::object,
                    source_identity)) &&
            succeeded(
                fixture.identities.resolve(
                    fixture.identities.root(),
                    objects_name,
                    identity_kind::object,
                    objects_identity)),
            "prepare FIXED_DIRECT subobject-link identities")) {

        return;
    }

    type_handle type;

    if (!tests.expect(
            succeeded(
                fixture.G.declare_record(
                    type_identity,
                    graph_record_kind::struct_type,
                    type)),
            "declare FIXED_DIRECT subobject-link record")) {

        return;
    }

    const auto integer =
        fixture.G.intrinsic(
            intrinsic_type::signed_int);

    type_ref values_type;
    type_ref input_type;

    if (!tests.expect(
            integer &&
            succeeded(
                fixture.G.derive(
                    integer,
                    derived_type_kind::bounded_array,
                    4,
                    values_type)) &&
            succeeded(
                fixture.G.derive(
                    integer,
                    derived_type_kind::lvalue_reference,
                    0,
                    input_type)),
            "derive FIXED_DIRECT subobject-link types")) {

        return;
    }

    const std::array<member_record, 2>
        members{{
            {
                values_name,
                values_type,
                graph_member_access::public_access,
            },
            {
                input_name,
                input_type,
                graph_member_access::public_access,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    type,
                    graph_record_kind::struct_type,
                    members)),
            "define FIXED_DIRECT subobject-link record")) {

        return;
    }

    const auto named =
        fixture.G.named(type);

    type_ref object_array;

    if (!tests.expect(
            named &&
            succeeded(
                fixture.G.derive(
                    named,
                    derived_type_kind::bounded_array,
                    2,
                    object_array)),
            "derive FIXED_DIRECT subobject object array")) {

        return;
    }

    object_handle source;
    object_handle objects;

    if (!tests.expect(
            succeeded(
                fixture.G.add_object(
                    source_identity,
                    named,
                    source)) &&
            succeeded(
                fixture.G.add_object(
                    objects_identity,
                    object_array,
                    objects)),
            "add FIXED_DIRECT subobject-link objects")) {

        return;
    }

    const auto values =
        fixture.G.find_member(
            type,
            values_name);

    const auto input =
        fixture.G.find_member(
            type,
            input_name);

    const std::array<endpoint_path_step, 2>
        source_steps{{
            {
                values.value(),
                endpoint_path_step_kind::member,
                {},
            },
            {
                2,
                endpoint_path_step_kind::array_index,
                {},
            },
        }};

    const std::array<endpoint_path_step, 2>
        target_steps{{
            {
                1,
                endpoint_path_step_kind::array_index,
                {},
            },
            {
                input.value(),
                endpoint_path_step_kind::member,
                {},
            },
        }};

    endpoint_path_handle source_path;
    endpoint_path_handle target_path;
    type_ref source_type;
    type_ref target_type;

    if (!tests.expect(
            values &&
            input &&
            succeeded(
                fixture.G.intern_endpoint_path(
                    named,
                    source_steps,
                    source_path,
                    &source_type)) &&
            succeeded(
                fixture.G.intern_endpoint_path(
                    object_array,
                    target_steps,
                    target_path,
                    &target_type)) &&
            source_type ==
                integer &&
            target_type ==
                input_type,
            "intern FIXED_DIRECT source and target subobject paths")) {

        return;
    }

    link_handle link;

    if (!tests.expect(
            succeeded(
                fixture.G.add_link(
                    {
                        fixture.G.identity(
                            source),
                        endpoint_ref::from_path(
                            source_path),
                    },
                    {
                        fixture.G.identity(
                            objects),
                        endpoint_ref::from_path(
                            target_path),
                    },
                    link)),
            "add FIXED_DIRECT indexed subobject link")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.sources.finalize(
                    fixture.files.size(),
                    fixture.identities,
                    fixture.G)),
            "finalize FIXED_DIRECT subobject-link Source Map")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::success,
            "encode FIXED_DIRECT subobject-link image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success &&
            view.verify_contents() ==
                compiled_project_image_result::success,
            "bind FIXED_DIRECT subobject-link image")) {

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
            "prepare FIXED_DIRECT subobject-link Runtime layout")) {

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
            "allocate FIXED_DIRECT subobject-link Runtime");
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
            "materialize FIXED_DIRECT indexed subobject link")) {

        return;
    }

    type_entry record;
    record_offset values_offset = 0;
    record_offset input_offset = 0;
    runtime_offset source_offset = 0;
    runtime_offset objects_offset = 0;
    runtime_value_layout record_layout;

    if (!tests.expect(
            view.type(
                type,
                record) &&
            layout.member_offset(
                static_cast<std::size_t>(
                    record.members.begin) +
                    values.value(),
                values_offset) &&
            layout.member_offset(
                static_cast<std::size_t>(
                    record.members.begin) +
                    input.value(),
                input_offset) &&
            layout.object_offset(
                source,
                source_offset) &&
            layout.object_offset(
                objects,
                objects_offset) &&
            layout.value(
                named,
                record_layout),
            "query FIXED_DIRECT subobject-link offsets")) {

        return;
    }

    const auto base =
        reinterpret_cast<std::uintptr_t>(
            runtime.data());

    const auto expected_source =
        base +
        static_cast<std::uintptr_t>(
            source_offset +
            values_offset +
            2 *
                sizeof(int));

    const auto target_slot_offset =
        objects_offset +
        record_layout.size +
        input_offset;

    std::uintptr_t observed = 0;

    std::memcpy(
        &observed,
        runtime.data() +
            static_cast<std::size_t>(
                target_slot_offset),
        sizeof(observed));

    tests.expect(
        observed ==
            expected_source,
        "indexed target reference points directly at indexed source element");
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

    record_offset value_offset = 0;
    record_offset peer_offset = 0;
    runtime_offset left_offset = 0;
    runtime_offset right_offset = 0;

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
        left_offset == 80 &&
        native.object_offset(
            fixture.right,
            right_offset) &&
        right_offset == 96 &&
        native.size() == 120 &&
        native.alignment() == 8,
        "pack-8 System prefix, canonical sentinel, and dense object layout");

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
        left_offset == 76 &&
        packed.object_offset(
            fixture.right,
            right_offset) &&
        right_offset == 88 &&
        packed.size() == 104 &&
        packed.alignment() == 8,
        "pack-4 System prefix, canonical sentinel, and dense object layout");

    std::uint64_t unconnected = 0;

    tests.expect(
        native.unconnected_offset(
            fixture.integer_type,
            unconnected) &&
        unconnected ==
            sizeof(runtime_system),
        "Runtime layout places System first and canonical unconnected<int> after it");
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
                        G.identity(
                            source_object),
                        a_out,
                    },
                    {
                        G.identity(
                            linked_object),
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

    runtime_offset unconnected_int = 0;
    runtime_offset unconnected_a = 0;
    runtime_offset source_offset = 0;
    runtime_offset linked_offset = 0;
    runtime_offset container_offset = 0;

    type_entry a_record;
    type_entry b_record;

    record_offset a_in_offset = 0;
    record_offset a_out_offset = 0;
    record_offset b_in_offset = 0;
    record_offset b_out_offset = 0;

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
    runtime_offset object_offset = 0;
    record_offset offsets[4]{};

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

    const auto cycle_construction =
        section_offset(
            image.bytes,
            compiled_project_section::
                member_construction) +
        (static_cast<std::size_t>(
             record.members.begin) +
         2) *
            sizeof(construction_value);

    write_u32(
        image.bytes.data() +
            cycle_construction +
            offsetof(
                construction_value,
                operand),
        1);

    rewrite_section_crc(
        image.bytes,
        compiled_project_section::
            member_construction);

    tests.expect(
        materialize_fixed_direct(
            view,
            layout,
            abi,
            runtime) ==
                fixed_direct_materialization_result::
                    invalid_input,
        "specialized unplanned reference chain preserves cycle detection");
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
                            value.G.identity(
                                value.b),
                            value.in,
                        },
                        {
                            value.G.identity(
                                value.a),
                            value.in,
                        },
                        first)) &&
                succeeded(
                    value.G.add_link(
                        {
                            value.G.identity(
                                value.c),
                            value.out,
                        },
                        {
                            value.G.identity(
                                value.b),
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
        record_offset out_offset = 0;
        record_offset in_offset = 0;
        runtime_offset a_offset = 0;
        runtime_offset b_offset = 0;
        runtime_offset c_offset = 0;

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
                            value.G.identity(
                                value.b),
                            value.in,
                        },
                        {
                            value.G.identity(
                                value.a),
                            value.in,
                        },
                        first)) &&
                succeeded(
                    value.G.add_link(
                        {
                            value.G.identity(
                                value.a),
                            value.in,
                        },
                        {
                            value.G.identity(
                                value.b),
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
                            value.G.identity(
                                value.c),
                            value.out,
                        },
                        {
                            value.G.identity(
                                value.a),
                            value.in,
                        },
                        first)) &&
                succeeded(
                    value.G.add_link(
                        {
                            value.G.identity(
                                value.c),
                            value.out,
                        },
                        {
                            value.G.identity(
                                value.b),
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

void test_windows_x86_target_runtime(
    test_state& tests,
    const compiled_fixture& fixture,
    const compiled_test_image& image) {

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success &&
            view.verify_contents() ==
                compiled_project_image_result::success,
            "bind windows-x86 target image")) {

        return;
    }

    const server_abi_configuration abi{
        abi_target::windows_x86,
        8,
    };

    runtime_layout layout;

    if (!tests.expect(
            prepare_runtime_layout(
                view,
                abi,
                layout) ==
                runtime_layout_result::success,
            "prepare windows-x86 target Runtime layout")) {

        return;
    }

    runtime_value_layout record_layout;
    runtime_value_layout reference_layout;

    if (!tests.expect(
            layout.value(
                fixture.named_type,
                record_layout) &&
            layout.value(
                fixture.reference_type,
                reference_layout) &&
            record_layout.size == 8 &&
            record_layout.alignment == 4 &&
            reference_layout.size == 4 &&
            reference_layout.alignment == 4,
            "windows-x86 uses 4-byte native pointer/reference layout")) {

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
            "allocate windows-x86 target Runtime");
        return;
    }

    constexpr std::uint64_t target_base =
        0x20000000ull;

    if (!tests.expect(
            fixed_direct_target_range_compatible(
                abi,
                target_base,
                layout.size()) &&
            materialize_fixed_direct(
                view,
                layout,
                abi,
                target_base,
                runtime) ==
                fixed_direct_materialization_result::
                    success,
            "materialize windows-x86 target image from host byte buffer")) {

        return;
    }

    type_entry record;
    record_offset value_offset = 0;
    record_offset peer_offset = 0;
    runtime_offset left_offset = 0;
    runtime_offset right_offset = 0;

    if (!tests.expect(
            view.type(
                fixture.type,
                record) &&
            layout.member_offset(
                static_cast<std::size_t>(
                    record.members.begin) +
                    fixture.value_member.value(),
                value_offset) &&
            layout.member_offset(
                static_cast<std::size_t>(
                    record.members.begin) +
                    fixture.peer_member.value(),
                peer_offset) &&
            layout.object_offset(
                fixture.left,
                left_offset) &&
            layout.object_offset(
                fixture.right,
                right_offset),
            "query windows-x86 target offsets")) {

        return;
    }

    std::uint32_t observed = 0;

    std::memcpy(
        &observed,
        runtime.data() +
            static_cast<std::size_t>(
                right_offset +
                peer_offset),
        sizeof(observed));

    const auto expected =
        static_cast<std::uint32_t>(
            target_base +
            left_offset +
            value_offset);

    tests.expect(
        observed == expected,
        "windows-x86 reference stores target VA, not host buffer address");

    const server_abi_configuration wrong_target{
        abi_target::windows_x64,
        8,
    };

    tests.expect(
        materialize_fixed_direct(
            view,
            layout,
            wrong_target,
            target_base,
            runtime) ==
                fixed_direct_materialization_result::
                    incompatible_abi,
        "FIXED_DIRECT rejects target codec that does not match Runtime layout");

    tests.expect(
        !fixed_direct_target_range_compatible(
            abi,
            0xc0000000ull,
            layout.size()),
        "windows-x86 rejects Runtime range reserved for construction markers");
}


void test_windows_class_abi_runtime(
    test_state& tests) {

    compiled_fixture fixture;

    string_id a_type_name;
    string_id b_type_name;
    string_id c_type_name;
    string_id d_type_name;
    string_id reference_base_name;
    string_id reference_derived_name;
    string_id own_vfptr_base_name;
    string_id own_vfptr_derived_name;
    string_id a_name;
    string_id b_name;
    string_id c_name;
    string_id d_name;
    string_id out_name;
    string_id in_name;
    string_id tail_name;
    string_id own_base_member_name;
    string_id own_member_name;
    string_id d_object_name;
    string_id reference_object_name;
    string_id own_vfptr_object_name;

    const auto intern =
        [&](std::string_view value,
            string_id& output) {

            return succeeded(
                fixture.strings.intern(
                    value,
                    output));
        };

    if (!tests.expect(
            intern("ClassAbiA", a_type_name) &&
            intern("ClassAbiB", b_type_name) &&
            intern("ClassAbiC", c_type_name) &&
            intern("ClassAbiD", d_type_name) &&
            intern("ClassAbiReferenceBase", reference_base_name) &&
            intern("ClassAbiReferenceDerived", reference_derived_name) &&
            intern("ClassAbiOwnVfptrBase", own_vfptr_base_name) &&
            intern("ClassAbiOwnVfptrDerived", own_vfptr_derived_name) &&
            intern("a", a_name) &&
            intern("b", b_name) &&
            intern("c", c_name) &&
            intern("d", d_name) &&
            intern("out", out_name) &&
            intern("in", in_name) &&
            intern("tail", tail_name) &&
            intern("base_value", own_base_member_name) &&
            intern("own_value", own_member_name) &&
            intern("class_abi_d", d_object_name) &&
            intern("class_abi_reference", reference_object_name) &&
            intern("class_abi_own_vfptr", own_vfptr_object_name),
            "prepare Windows class ABI Runtime strings")) {

        return;
    }

    const auto declare_type =
        [&](string_id name,
            type_handle& output) {

            identity_ref identity;

            return
                succeeded(
                    fixture.identities.resolve(
                        fixture.identities.root(),
                        name,
                        identity_kind::type,
                        identity)) &&
                succeeded(
                    fixture.G.declare_record(
                        identity,
                        graph_record_kind::struct_type,
                        output));
        };

    type_handle a;
    type_handle b;
    type_handle c;
    type_handle d;
    type_handle reference_base;
    type_handle reference_derived;
    type_handle own_vfptr_base;
    type_handle own_vfptr_derived;

    if (!tests.expect(
            declare_type(a_type_name, a) &&
            declare_type(b_type_name, b) &&
            declare_type(c_type_name, c) &&
            declare_type(d_type_name, d) &&
            declare_type(reference_base_name, reference_base) &&
            declare_type(reference_derived_name, reference_derived) &&
            declare_type(own_vfptr_base_name, own_vfptr_base) &&
            declare_type(own_vfptr_derived_name, own_vfptr_derived),
            "declare Windows class ABI Runtime records")) {

        return;
    }

    const auto integer =
        fixture.G.intrinsic(
            intrinsic_type::signed_int);

    const auto real =
        fixture.G.intrinsic(
            intrinsic_type::double_type);

    type_ref integer_reference;

    if (!tests.expect(
            integer &&
            real &&
            succeeded(
                fixture.G.derive(
                    integer,
                    derived_type_kind::lvalue_reference,
                    0,
                    integer_reference)),
            "prepare Windows class ABI Runtime value types")) {

        return;
    }

    const std::array<member_record, 1> a_members{{
        {
            a_name,
            integer,
            graph_member_access::public_access,
        },
    }};

    const std::array<member_record, 1> b_members{{
        {
            b_name,
            integer,
            graph_member_access::public_access,
        },
    }};

    const std::array<member_record, 1> c_members{{
        {
            c_name,
            real,
            graph_member_access::public_access,
        },
    }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    a,
                    graph_record_kind::struct_type,
                    a_members)) &&
            succeeded(
                fixture.G.define_record(
                    b,
                    graph_record_kind::struct_type,
                    b_members,
                    {},
                    {},
                    true)) &&
            succeeded(
                fixture.G.define_record(
                    c,
                    graph_record_kind::struct_type,
                    c_members,
                    {},
                    {},
                    true)),
            "define Windows class ABI direct bases")) {

        return;
    }

    const std::array<base_record, 3> d_bases{{
        {
            fixture.G.identity(a),
            graph_member_access::public_access,
            0,
            0,
        },
        {
            fixture.G.identity(b),
            graph_member_access::public_access,
            0,
            0,
        },
        {
            fixture.G.identity(c),
            graph_member_access::public_access,
            0,
            0,
        },
    }};

    const std::array<member_record, 1> d_members{{
        {
            d_name,
            integer,
            graph_member_access::public_access,
        },
    }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    d,
                    graph_record_kind::struct_type,
                    d_members,
                    {},
                    d_bases)),
            "define Windows class ABI multiple inheritance")) {

        return;
    }

    const std::array<member_record, 2> reference_members{{
        {
            out_name,
            integer,
            graph_member_access::public_access,
        },
        {
            in_name,
            integer_reference,
            graph_member_access::public_access,
        },
    }};

    const std::array<construction_value, 2>
        reference_construction{{
            {},
            construction_value::member_binding(1),
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    reference_base,
                    graph_record_kind::struct_type,
                    reference_members,
                    reference_construction)),
            "define native-reference base record")) {

        return;
    }

    const std::array<base_record, 1> reference_bases{{
        {
            fixture.G.identity(
                reference_base),
            graph_member_access::public_access,
            0,
            0,
        },
    }};

    const std::array<member_record, 1>
        reference_derived_members{{
            {
                tail_name,
                integer,
                graph_member_access::public_access,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    reference_derived,
                    graph_record_kind::struct_type,
                    reference_derived_members,
                    {},
                    reference_bases)),
            "define native-reference derived record")) {

        return;
    }

    const std::array<member_record, 1>
        own_vfptr_base_members{{
            {
                own_base_member_name,
                integer,
                graph_member_access::public_access,
            },
        }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    own_vfptr_base,
                    graph_record_kind::struct_type,
                    own_vfptr_base_members)),
            "define own-vfptr non-polymorphic base")) {

        return;
    }

    const std::array<base_record, 1> own_vfptr_bases{{
        {
            fixture.G.identity(
                own_vfptr_base),
            graph_member_access::public_access,
            0,
            0,
        },
    }};

    const std::array<member_record, 1> own_vfptr_members{{
        {
            own_member_name,
            real,
            graph_member_access::public_access,
        },
    }};

    if (!tests.expect(
            succeeded(
                fixture.G.define_record(
                    own_vfptr_derived,
                    graph_record_kind::struct_type,
                    own_vfptr_members,
                    {},
                    own_vfptr_bases,
                    true)),
            "define own-vfptr aligned derived record")) {

        return;
    }

    const auto add_object =
        [&](string_id name,
            type_handle type,
            object_handle& output) {

            identity_ref identity;

            return
                succeeded(
                    fixture.identities.resolve(
                        fixture.identities.root(),
                        name,
                        identity_kind::object,
                        identity)) &&
                succeeded(
                    fixture.G.add_object(
                        identity,
                        fixture.G.named(type),
                        output));
        };

    object_handle d_object;
    object_handle reference_object;
    object_handle own_vfptr_object;

    if (!tests.expect(
            add_object(d_object_name, d, d_object) &&
            add_object(
                reference_object_name,
                reference_derived,
                reference_object) &&
            add_object(
                own_vfptr_object_name,
                own_vfptr_derived,
                own_vfptr_object),
            "add Windows class ABI Runtime objects")) {

        return;
    }

    if (!tests.expect(
            succeeded(
                fixture.sources.finalize(
                    fixture.files.size(),
                    fixture.identities,
                    fixture.G)),
            "finalize Windows class ABI Runtime Source Map")) {

        return;
    }

    compiled_test_image image;

    if (!tests.expect(
            build_test_compiled_image(
                fixture,
                image) ==
                compiled_project_image_result::success,
            "encode Windows class ABI Runtime image")) {

        return;
    }

    compiled_project_view view;

    if (!tests.expect(
            view.bind(
                image.bytes) ==
                compiled_project_image_result::success &&
            view.verify_contents() ==
                compiled_project_image_result::success,
            "bind and audit Windows class ABI Runtime image")) {

        return;
    }

    type_entry persisted_d;
    type_entry persisted_reference_base;
    type_entry persisted_reference_derived;
    type_entry persisted_own_vfptr_derived;

    if (!tests.expect(
            view.type(d, persisted_d) &&
            view.type(
                reference_base,
                persisted_reference_base) &&
            view.type(
                reference_derived,
                persisted_reference_derived) &&
            view.type(
                own_vfptr_derived,
                persisted_own_vfptr_derived),
            "read Windows class ABI Runtime type records")) {

        return;
    }

    const auto check_layout =
        [&](abi_target target,
            std::uint64_t d_size,
            std::uint32_t d_alignment,
            record_offset a_base,
            record_offset b_base,
            record_offset c_base,
            record_offset d_member,
            std::uint64_t reference_base_size,
            std::uint32_t reference_base_alignment,
            record_offset reference_in,
            record_offset reference_tail,
            record_offset own_base,
            record_offset own_member) {

            const server_abi_configuration abi{
                target,
                8,
            };

            runtime_layout layout;

            if (!tests.expect(
                    prepare_runtime_layout(
                        view,
                        abi,
                        layout) ==
                        runtime_layout_result::success,
                    "derive Windows class ABI Runtime layout")) {

                return;
            }

            runtime_value_layout d_layout;
            runtime_value_layout reference_base_layout;
            runtime_value_layout reference_derived_layout;
            runtime_value_layout own_vfptr_layout;

            record_offset observed_a = 0;
            record_offset observed_b = 0;
            record_offset observed_c = 0;
            record_offset observed_d = 0;
            record_offset observed_reference_base = 0;
            record_offset observed_reference_in = 0;
            record_offset observed_reference_tail = 0;
            record_offset observed_own_base = 0;
            record_offset observed_own_member = 0;

            if (!tests.expect(
                    layout.type(
                        d,
                        d_layout) &&
                    layout.type(
                        reference_base,
                        reference_base_layout) &&
                    layout.type(
                        reference_derived,
                        reference_derived_layout) &&
                    layout.type(
                        own_vfptr_derived,
                        own_vfptr_layout) &&
                    layout.base_offset(
                        static_cast<std::size_t>(
                            persisted_d.bases.begin),
                        observed_a) &&
                    layout.base_offset(
                        static_cast<std::size_t>(
                            persisted_d.bases.begin) +
                            1,
                        observed_b) &&
                    layout.base_offset(
                        static_cast<std::size_t>(
                            persisted_d.bases.begin) +
                            2,
                        observed_c) &&
                    layout.member_offset(
                        static_cast<std::size_t>(
                            persisted_d.members.begin),
                        observed_d) &&
                    layout.base_offset(
                        static_cast<std::size_t>(
                            persisted_reference_derived.bases.begin),
                        observed_reference_base) &&
                    layout.member_offset(
                        static_cast<std::size_t>(
                            persisted_reference_base.members.begin) +
                            1,
                        observed_reference_in) &&
                    layout.member_offset(
                        static_cast<std::size_t>(
                            persisted_reference_derived.members.begin),
                        observed_reference_tail) &&
                    layout.base_offset(
                        static_cast<std::size_t>(
                            persisted_own_vfptr_derived.bases.begin),
                        observed_own_base) &&
                    layout.member_offset(
                        static_cast<std::size_t>(
                            persisted_own_vfptr_derived.members.begin),
                        observed_own_member),
                    "query Windows class ABI base/member offsets")) {

                return;
            }

            tests.expect(
                d_layout.size ==
                    d_size &&
                d_layout.alignment ==
                    d_alignment &&
                observed_a ==
                    a_base &&
                observed_b ==
                    b_base &&
                observed_c ==
                    c_base &&
                observed_d ==
                    d_member,
                "multiple inheritance matches MSVC compiler oracle");

            tests.expect(
                reference_base_layout.size ==
                    reference_base_size &&
                reference_base_layout.alignment ==
                    reference_base_alignment &&
                observed_reference_base == 0 &&
                observed_reference_in ==
                    reference_in &&
                observed_reference_tail ==
                    reference_tail,
                "native-reference base layout matches target ABI");

            tests.expect(
                own_vfptr_layout.size == 24 &&
                own_vfptr_layout.alignment == 8 &&
                observed_own_base ==
                    own_base &&
                observed_own_member ==
                    own_member,
                "own vfptr aligns base region by complete record alignment");

#if defined(_WIN32)
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
                    "allocate class ABI Runtime image");
                return;
            }

            constexpr std::uint64_t target_base =
                0x20000000ull;

            if (!tests.expect(
                    materialize_fixed_direct(
                        view,
                        layout,
                        abi,
                        target_base,
                        runtime) ==
                        fixed_direct_materialization_result::
                            success,
                    "materialize recursive class ABI base subobjects")) {

                return;
            }

            runtime_offset object_offset = 0;

            if (!tests.expect(
                    layout.object_offset(
                        reference_object,
                        object_offset),
                    "query native-reference derived object offset")) {

                return;
            }

            const auto slot_offset =
                object_offset +
                observed_reference_base +
                observed_reference_in;

            const auto referent_offset =
                object_offset +
                observed_reference_base;

            if (target ==
                abi_target::windows_x86) {

                std::uint32_t observed = 0;

                std::memcpy(
                    &observed,
                    runtime.data() +
                        static_cast<std::size_t>(
                            slot_offset),
                    sizeof(observed));

                tests.expect(
                    observed ==
                        static_cast<std::uint32_t>(
                            target_base +
                            referent_offset),
                    "Win32 inherited reference binds inside base subobject");
            }
            else {
                std::uint64_t observed = 0;

                std::memcpy(
                    &observed,
                    runtime.data() +
                        static_cast<std::size_t>(
                            slot_offset),
                    sizeof(observed));

                tests.expect(
                    observed ==
                        target_base +
                        referent_offset,
                    "x64 inherited reference binds inside base subobject");
            }
#endif
        };

    check_layout(
        abi_target::windows_x64,
        40,
        8,
        32,
        0,
        16,
        36,
        16,
        8,
        8,
        16,
        8,
        16);

    check_layout(
        abi_target::windows_x86,
        32,
        8,
        24,
        0,
        8,
        28,
        8,
        4,
        4,
        8,
        8,
        16);

    const server_abi_configuration posix_abi{
        abi_target::posix_x64,
        8,
    };

    runtime_layout posix_layout;

    tests.expect(
        prepare_runtime_layout(
            view,
            posix_abi,
            posix_layout) ==
            runtime_layout_result::
                unsupported_type,
        "class inheritance remains fail-closed for unverified POSIX ABI");

    const server_abi_configuration windows_abi{
        abi_target::windows_x64,
        8,
    };

    const auto d_base_slot =
        static_cast<std::size_t>(
            persisted_d.bases.begin);

    {
        auto duplicate =
            image.bytes;

        write_u32(
            duplicate.data() +
                section_offset(
                    duplicate,
                    compiled_project_section::bases) +
                (d_base_slot + 1) *
                    sizeof(base_record) +
                offsetof(
                    base_record,
                    type),
            fixture.G.identity(
                a).value());

        rewrite_section_crc(
            duplicate,
            compiled_project_section::bases);

        compiled_project_view duplicate_view;
        runtime_layout duplicate_layout;

        tests.expect(
            duplicate_view.bind(
                duplicate) ==
                    compiled_project_image_result::
                        success &&
            prepare_runtime_layout(
                duplicate_view,
                windows_abi,
                duplicate_layout) ==
                    runtime_layout_result::
                        invalid_input,
            "Runtime hot path rejects duplicate direct bases without cold audit");
    }

    {
        auto cyclic =
            image.bytes;

        write_u32(
            cyclic.data() +
                section_offset(
                    cyclic,
                    compiled_project_section::bases) +
                d_base_slot *
                    sizeof(base_record) +
                offsetof(
                    base_record,
                    type),
            fixture.G.identity(
                d).value());

        rewrite_section_crc(
            cyclic,
            compiled_project_section::bases);

        compiled_project_view cyclic_view;
        runtime_layout cyclic_layout;

        tests.expect(
            cyclic_view.bind(
                cyclic) ==
                    compiled_project_image_result::
                        success &&
            prepare_runtime_layout(
                cyclic_view,
                windows_abi,
                cyclic_layout) ==
                    runtime_layout_result::
                        invalid_input,
            "Runtime hot path rejects inheritance cycles without cold audit");
    }

    {
        auto virtual_base =
            image.bytes;

        auto* flags =
            virtual_base.data() +
            section_offset(
                virtual_base,
                compiled_project_section::bases) +
            d_base_slot *
                sizeof(base_record) +
            offsetof(
                base_record,
                flags);

        *flags =
            static_cast<std::byte>(
                graph_base_virtual);

        rewrite_section_crc(
            virtual_base,
            compiled_project_section::bases);

        compiled_project_view virtual_view;
        runtime_layout virtual_layout;

        tests.expect(
            virtual_view.bind(
                virtual_base) ==
                    compiled_project_image_result::
                        success &&
            prepare_runtime_layout(
                virtual_view,
                windows_abi,
                virtual_layout) ==
                    runtime_layout_result::
                        unsupported_type,
            "Runtime hot path keeps virtual inheritance fail-closed");
    }

    compiled_fixture empty_fixture;

    string_id empty_name;
    string_id derived_name;
    string_id value_name;
    string_id object_name;

    if (!tests.expect(
            succeeded(
                empty_fixture.strings.intern(
                    "EmptyBase",
                    empty_name)) &&
            succeeded(
                empty_fixture.strings.intern(
                    "EmptyDerived",
                    derived_name)) &&
            succeeded(
                empty_fixture.strings.intern(
                    "value",
                    value_name)) &&
            succeeded(
                empty_fixture.strings.intern(
                    "empty_object",
                    object_name)),
            "prepare EBO fail-closed strings")) {

        return;
    }

    identity_ref empty_identity;
    identity_ref derived_identity;
    identity_ref object_identity;

    if (!tests.expect(
            succeeded(
                empty_fixture.identities.resolve(
                    empty_fixture.identities.root(),
                    empty_name,
                    identity_kind::type,
                    empty_identity)) &&
            succeeded(
                empty_fixture.identities.resolve(
                    empty_fixture.identities.root(),
                    derived_name,
                    identity_kind::type,
                    derived_identity)) &&
            succeeded(
                empty_fixture.identities.resolve(
                    empty_fixture.identities.root(),
                    object_name,
                    identity_kind::object,
                    object_identity)),
            "prepare EBO fail-closed identities")) {

        return;
    }

    type_handle empty_base;
    type_handle empty_derived;

    if (!tests.expect(
            succeeded(
                empty_fixture.G.declare_record(
                    empty_identity,
                    graph_record_kind::struct_type,
                    empty_base)) &&
            succeeded(
                empty_fixture.G.declare_record(
                    derived_identity,
                    graph_record_kind::struct_type,
                    empty_derived)) &&
            succeeded(
                empty_fixture.G.define_record(
                    empty_base,
                    graph_record_kind::struct_type,
                    {})),
            "define empty base")) {

        return;
    }

    const std::array<base_record, 1> empty_bases{{
        {
            empty_identity,
            graph_member_access::public_access,
            0,
            0,
        },
    }};

    const std::array<member_record, 1> empty_derived_members{{
        {
            value_name,
            empty_fixture.G.intrinsic(
                intrinsic_type::signed_int),
            graph_member_access::public_access,
        },
    }};

    object_handle empty_object;

    if (!tests.expect(
            succeeded(
                empty_fixture.G.define_record(
                    empty_derived,
                    graph_record_kind::struct_type,
                    empty_derived_members,
                    {},
                    empty_bases)) &&
            succeeded(
                empty_fixture.G.add_object(
                    object_identity,
                    empty_fixture.G.named(
                        empty_derived),
                    empty_object)) &&
            succeeded(
                empty_fixture.sources.finalize(
                    empty_fixture.files.size(),
                    empty_fixture.identities,
                    empty_fixture.G)),
            "prepare EBO fail-closed Runtime graph")) {

        return;
    }

    compiled_test_image empty_image;

    if (!tests.expect(
            build_test_compiled_image(
                empty_fixture,
                empty_image) ==
                compiled_project_image_result::success,
            "encode EBO fail-closed image")) {

        return;
    }

    compiled_project_view empty_view;

    runtime_layout empty_layout;

    tests.expect(
        empty_view.bind(
            empty_image.bytes) ==
                compiled_project_image_result::success &&
        empty_view.verify_contents() ==
                compiled_project_image_result::success &&
        prepare_runtime_layout(
            empty_view,
            windows_abi,
            empty_layout) ==
                runtime_layout_result::
                    unsupported_type,
        "empty-base optimization remains fail-closed in V1B");
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
        wide_offset ==
            sizeof(runtime_system) &&
        tail_offset ==
            sizeof(runtime_system) + 8 &&
        layout.alignment() == 8 &&
        layout.size() ==
            sizeof(runtime_system) + 16,
        "Runtime total size is aligned after the System prefix");
}

void test_sparse_fixed_graph_writes(
    test_state& tests,
    const compiled_fixture& fixture,
    const compiled_test_image& baseline_image) {

    {
        auto image =
            baseline_image.bytes;

        compiled_project_view baseline;

        if (!tests.expect(
                baseline.bind(
                    image) ==
                    compiled_project_image_result::success,
                "bind sparse fixed type retirement baseline")) {

            return;
        }

        graph_delta delta;

        if (!tests.expect(
                succeeded(
                    delta.bind_baseline(
                        baseline)) &&
                succeeded(
                    delta.retire(
                        fixture.type)) &&
                apply_compiled_project_graph_fixed_writes(
                    delta,
                    image) ==
                    compiled_project_image_result::success,
                "persist sparse fixed type retirement")) {

            return;
        }

        compiled_project_view retired;
        type_entry raw;

        tests.expect(
            retired.bind(
                image) ==
                compiled_project_image_result::success &&
            retired.type_slot_count() ==
                baseline.type_slot_count() &&
            retired.live_type_count() + 1 ==
                baseline.live_type_count() &&
            !retired.type_slot_live(
                fixture.type) &&
            !retired.find_type(
                fixture.type_identity) &&
            retired.find_type_lineage(
                fixture.type_identity) ==
                fixture.type &&
            retired.type_raw(
                fixture.type,
                raw),
            "retired type keeps assigned WHERE without live semantic lookup");
    }

    {
        auto image =
            baseline_image.bytes;

        compiled_project_view baseline;

        if (!tests.expect(
                baseline.bind(
                    image) ==
                    compiled_project_image_result::success,
                "bind sparse fixed object retirement baseline")) {

            return;
        }

        graph_delta delta;

        if (!tests.expect(
                succeeded(
                    delta.bind_baseline(
                        baseline)) &&
                succeeded(
                    delta.retire(
                        fixture.left)) &&
                apply_compiled_project_graph_fixed_writes(
                    delta,
                    image) ==
                    compiled_project_image_result::success,
                "persist sparse fixed object retirement")) {

            return;
        }

        compiled_project_view retired;
        object_entry raw;

        if (!tests.expect(
                retired.bind(
                    image) ==
                    compiled_project_image_result::success &&
                retired.object_slot_count() ==
                    baseline.object_slot_count() &&
                retired.live_object_count() + 1 ==
                    baseline.live_object_count() &&
                !retired.object_slot_live(
                    fixture.left) &&
                !retired.find_object(
                    fixture.left_identity) &&
                retired.find_object_lineage(
                    fixture.left_identity) ==
                    fixture.left &&
                retired.object_raw(
                    fixture.left,
                    raw),
                "retired object keeps assigned WHERE without live semantic lookup")) {

            return;
        }

        graph_delta reactivate;

        object_handle restored;

        if (!tests.expect(
                succeeded(
                    reactivate.bind_baseline(
                        retired)) &&
                succeeded(
                    reactivate.add_object(
                        fixture.left_identity,
                        fixture.named_type,
                        restored)) &&
                restored ==
                    fixture.left &&
                apply_compiled_project_graph_fixed_writes(
                    reactivate,
                    image) ==
                    compiled_project_image_result::success,
                "reactivate object at persisted assigned WHERE")) {

            return;
        }

        compiled_project_view current;

        tests.expect(
            current.bind(
                image) ==
                    compiled_project_image_result::success &&
            current.object_slot_live(
                fixture.left) &&
            current.find_object(
                fixture.left_identity) ==
                fixture.left &&
            current.find_object_lineage(
                fixture.left_identity) ==
                fixture.left &&
            current.live_object_count() ==
                baseline.live_object_count(),
            "object reactivation restores same current WHERE");
    }

    {
        auto image =
            baseline_image.bytes;

        compiled_project_view baseline;

        if (!tests.expect(
                baseline.bind(
                    image) ==
                    compiled_project_image_result::success,
                "bind sparse fixed link retirement baseline")) {

            return;
        }

        link_record original;

        if (!tests.expect(
                baseline.link(
                    fixture.link,
                    original),
                "read sparse fixed link retirement baseline")) {

            return;
        }

        graph_delta delta;

        if (!tests.expect(
                succeeded(
                    delta.bind_baseline(
                        baseline)) &&
                succeeded(
                    delta.retire(
                        fixture.link)) &&
                apply_compiled_project_graph_fixed_writes(
                    delta,
                    image) ==
                    compiled_project_image_result::success,
                "persist sparse fixed link retirement")) {

            return;
        }

        compiled_project_view retired;
        link_record raw;

        if (!tests.expect(
                retired.bind(
                    image) ==
                    compiled_project_image_result::success &&
                retired.link_slot_count() ==
                    baseline.link_slot_count() &&
                retired.live_link_count() + 1 ==
                    baseline.live_link_count() &&
                !retired.link_slot_live(
                    fixture.link) &&
                !retired.find_link_target(
                    original.target) &&
                retired.find_link_target_lineage(
                    original.target) ==
                    fixture.link &&
                retired.link_raw(
                    fixture.link,
                    raw) &&
                raw.source ==
                    original.source &&
                raw.target ==
                    original.target,
                "retired link keeps target lineage and same WHERE")) {

            return;
        }

        graph_delta reactivate;

        link_handle restored;

        if (!tests.expect(
                succeeded(
                    reactivate.bind_baseline(
                        retired)) &&
                succeeded(
                    reactivate.add_link(
                        original.source,
                        original.target,
                        restored)) &&
                restored ==
                    fixture.link &&
                apply_compiled_project_graph_fixed_writes(
                    reactivate,
                    image) ==
                    compiled_project_image_result::success,
                "reactivate link at persisted assigned WHERE")) {

            return;
        }

        compiled_project_view current;

        tests.expect(
            current.bind(
                image) ==
                    compiled_project_image_result::success &&
            current.link_slot_live(
                fixture.link) &&
            current.find_link_target(
                original.target) ==
                fixture.link &&
            current.find_link_target_lineage(
                original.target) ==
                fixture.link &&
            current.live_link_count() ==
                baseline.live_link_count(),
            "link reactivation restores same current WHERE");
    }
}



void test_sparse_in_place_payload_reuse(
    test_state& tests,
    const compiled_fixture& fixture,
    const compiled_test_image& baseline_image) {

    {
        auto image =
            baseline_image.bytes;

        compiled_project_view baseline;
        type_entry baseline_type;

        if (!tests.expect(
                baseline.bind(
                    image) ==
                    compiled_project_image_result::success &&
                baseline.type(
                    fixture.type,
                    baseline_type),
                "bind sparse same-shape type update baseline")) {

            return;
        }

        const std::array<member_record, 2>
            members{{
                {
                    fixture.value_name,
                    fixture.integer_type,
                    graph_member_access::
                        protected_access,
                },
                {
                    fixture.peer_name,
                    fixture.reference_type,
                    graph_member_access::
                        private_access,
                },
            }};

        const std::array<construction_value, 2>
            construction{{
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    43),
                construction_value{},
            }};

        graph_delta delta;

        if (!tests.expect(
                succeeded(
                    delta.bind_baseline(
                        baseline)) &&
                succeeded(
                    delta.clear_definition(
                        fixture.type)) &&
                succeeded(
                    delta.define_record(
                        fixture.type,
                        graph_record_kind::
                            struct_type,
                        members,
                        construction)) &&
                delta.member_entries().size() ==
                    members.size() &&
                delta.base_entries().empty(),
                "prepare same-shape type replacement")) {

            return;
        }

        const auto old_size =
            image.size();

        if (!tests.expect(
                apply_compiled_project_graph_fixed_writes(
                    delta,
                    image) ==
                    compiled_project_image_result::
                        success &&
                image.size() ==
                    old_size,
                "patch same-shape type payload in place")) {

            return;
        }

        compiled_project_view current;
        type_entry current_type;
        member_record current_member;
        construction_value current_construction;

        tests.expect(
            current.bind(
                image) ==
                    compiled_project_image_result::success &&
            current.type(
                fixture.type,
                current_type) &&
            current_type.members.begin ==
                baseline_type.members.begin &&
            current_type.members.count ==
                baseline_type.members.count &&
            current.member(
                fixture.type,
                fixture.value_member,
                current_member) &&
            current_member.name ==
                fixture.value_name &&
            current_member.access ==
                graph_member_access::
                    protected_access &&
            current.construction(
                fixture.type,
                fixture.value_member,
                current_construction) &&
            current_construction ==
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    43) &&
            current.member_count() ==
                baseline.member_count() &&
            current.graph_append_byte_size() ==
                baseline.graph_append_byte_size(),
            "same-shape type update keeps member WHERE and file size");
    }

    {
        auto image =
            baseline_image.bytes;

        compiled_project_view baseline;
        object_entry baseline_scalar;

        if (!tests.expect(
                baseline.bind(
                    image) ==
                    compiled_project_image_result::success &&
                baseline.object(
                    fixture.scalar,
                    baseline_scalar) &&
                baseline_scalar.
                    non_default_initializer(),
                "bind sparse construction update baseline")) {

            return;
        }

        const auto old_construction_slot =
            baseline_scalar.
                construction_slot();

        graph_delta delta;
        object_handle restored;

        if (!tests.expect(
                succeeded(
                    delta.bind_baseline(
                        baseline)) &&
                succeeded(
                    delta.retire(
                        fixture.scalar)) &&
                succeeded(
                    delta.add_object(
                        fixture.scalar_identity,
                        fixture.integer_type,
                        restored,
                        graph_object_non_default_initializer,
                        construction_value::constant(
                            construction_kind::
                                unsigned_integer,
                            9))) &&
                restored ==
                    fixture.scalar &&
                delta.object_construction_entries().
                    size() == 1,
                "prepare existing object construction replacement")) {

            return;
        }

        const auto old_size =
            image.size();
        const auto old_construction_count =
            baseline.object_construction_count();

        if (!tests.expect(
                apply_compiled_project_graph_fixed_writes(
                    delta,
                    image) ==
                    compiled_project_image_result::
                        success &&
                image.size() ==
                    old_size,
                "patch existing object construction in place")) {

            return;
        }

        compiled_project_view current;
        object_entry current_scalar;
        construction_value current_initial;

        tests.expect(
            current.bind(
                image) ==
                    compiled_project_image_result::success &&
            current.object(
                fixture.scalar,
                current_scalar) &&
            current_scalar.construction_slot() ==
                old_construction_slot &&
            current.object_construction_count() ==
                old_construction_count &&
            current.construction(
                fixture.scalar,
                current_initial) &&
            current_initial ==
                construction_value::constant(
                    construction_kind::
                        unsigned_integer,
                    9) &&
            current.graph_append_byte_size() ==
                baseline.graph_append_byte_size(),
            "existing construction UPDATE reuses same WHERE without append");
    }
}


void test_graph_append_tail_boundary(
    test_state& tests,
    const compiled_test_image& image) {

    const auto tail =
        compiled_project_section::
            graph_append_bytes;

    bool section_crc_zero = true;

    for (std::size_t index = 0;
         index <
            compiled_project_directory_count;
         ++index) {

        const auto* entry =
            image.bytes.data() +
            directory_offset +
            index *
                compiled_project_directory_entry_size;

        if (read_u64(
                entry + 24) != 0) {

            section_crc_zero = false;
            break;
        }
    }

    compiled_project_view view;

    tests.expect(
        section_count(
            image.bytes,
            tail) == 0 &&
        section_offset(
            image.bytes,
            tail) ==
            image.bytes.size() &&
        section_crc_zero &&
        view.bind(
            image.bytes) ==
                compiled_project_image_result::success &&
        view.graph_append_byte_size() == 0,
        "compact compiled G ends at one empty grow-only Graph tail");
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
            "bind remains structural after payload byte change")) {

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
            baseline.type_slot_count() &&
        G.live_type_count() ==
            baseline.live_type_count() &&
        G.member_count() ==
            baseline.member_count() &&
        G.base_count() ==
            baseline.base_count() &&
        G.object_count() ==
            baseline.object_slot_count() &&
        G.live_object_count() ==
            baseline.live_object_count() &&
        G.link_count() ==
            baseline.link_slot_count() &&
        G.live_link_count() ==
            baseline.live_link_count() &&
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

    const auto initial_type_slots =
        G.type_count();

    const auto initial_stale_types =
        G.stale_type_count();

    if (!tests.expect(
            succeeded(
                G.clear_definition(
                    fixture.type)) &&
            G.find_type(
                fixture.type_identity) ==
                fixture.type &&
            G.contains(
                fixture.type) &&
            G.type_count() ==
                initial_type_slots &&
            G.stale_type_count() ==
                initial_stale_types,
            "BUILD type update preserves physical WHERE")) {
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
        "graph_delta type replacement preserves top-level WHERE");

    std::vector<graph_delta_type_change>
        type_changes;

    const auto collect_type_change =
        [](void* context,
           const graph_delta_type_change& change) noexcept
        -> server_status {

            try {
                static_cast<
                    std::vector<
                        graph_delta_type_change>*>(
                            context)->push_back(
                                change);

                return server_status::success;
            }
            catch (...) {
                return server_status::io_error;
            }
        };

    tests.expect(
        succeeded(
            G.visit_type_changes(
                &type_changes,
                collect_type_change)) &&
        type_changes.size() == 1 &&
        type_changes[0].kind ==
            graph_delta_change_kind::patch &&
        type_changes[0].handle ==
            fixture.type &&
        type_changes[0].identity ==
            fixture.type_identity &&
        type_changes[0].live &&
        type_changes[0].value.members.count ==
            replacement_members.size() &&
        G.appended_type_count() == 0 &&
        G.member_entries().size() ==
            replacement_members.size(),
        "sparse type write set excludes unchanged baseline types");

    string_id build_base_name;
    identity_ref build_base_identity;
    type_handle build_base;

    if (!tests.expect(
            succeeded(
                strings.intern(
                    "BuildBase",
                    build_base_name)) &&
            succeeded(
                identities.resolve(
                    identities.root(),
                    build_base_name,
                    identity_kind::type,
                    build_base_identity)) &&
            succeeded(
                G.declare_record(
                    build_base_identity,
                    graph_record_kind::struct_type,
                    build_base)) &&
            succeeded(
                G.define_record(
                    build_base,
                    graph_record_kind::struct_type,
                    std::span<const member_record>{},
                    std::span<const construction_value>{},
                    std::span<const base_record>{},
                    true)),
            "BUILD graph_delta creates polymorphic appended base type")) {

        return;
    }

    const std::array<base_record, 1>
        replacement_bases{{
            {
                build_base_identity,
                graph_member_access::public_access,
                0,
                0,
            },
        }};

    if (!tests.expect(
            succeeded(
                G.clear_definition(
                    fixture.type)) &&
            succeeded(
                G.define_record(
                    fixture.type,
                    graph_record_kind::struct_type,
                    replacement_members,
                    replacement_construction,
                    replacement_bases,
                    false)),
            "BUILD graph_delta replaces type with base topology")) {

        return;
    }

    type_entry inherited_type;
    base_record inherited_base;

    tests.expect(
        G.type(
            fixture.type,
            inherited_type) &&
        inherited_type.bases.count == 1 &&
        inherited_type.polymorphic() &&
        G.polymorphic(
            fixture.type) &&
        G.base(
            fixture.type,
            0,
            inherited_base) &&
        inherited_base.type ==
            build_base_identity &&
        inherited_base.access ==
            graph_member_access::public_access,
        "BUILD graph_delta preserves sparse base and inherited polymorphism");

    const auto old_live_objects =
        G.live_object_count();

    const auto old_object_slots =
        G.object_count();

    const auto old_stale_objects =
        G.stale_object_count();

    if (!tests.expect(
            succeeded(
                G.retire(
                    fixture.scalar)) &&
            !G.contains(
                fixture.scalar) &&
            !G.find_object(
                fixture.scalar_identity) &&
            G.live_object_count() + 1 ==
                old_live_objects &&
            G.object_count() ==
                old_object_slots &&
            G.stale_object_count() ==
                old_stale_objects + 1,
            "BUILD graph_delta retires object without moving its WHERE")) {
        return;
    }

    std::vector<graph_delta_object_change>
        object_changes;

    const auto collect_object_change =
        [](void* context,
           const graph_delta_object_change& change) noexcept
        -> server_status {

            try {
                static_cast<
                    std::vector<
                        graph_delta_object_change>*>(
                            context)->push_back(
                                change);

                return server_status::success;
            }
            catch (...) {
                return server_status::io_error;
            }
        };

    tests.expect(
        succeeded(
            G.visit_object_changes(
                &object_changes,
                collect_object_change)) &&
        object_changes.size() == 1 &&
        object_changes[0].kind ==
            graph_delta_change_kind::patch &&
        object_changes[0].handle ==
            fixture.scalar &&
        object_changes[0].identity ==
            fixture.scalar_identity &&
        !object_changes[0].live &&
        G.appended_object_count() == 0,
        "sparse object write set carries retirement at the same WHERE");

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
                fixture.scalar &&
            G.object_count() ==
                old_object_slots &&
            G.stale_object_count() ==
                old_stale_objects,
            "BUILD graph_delta reactivates scalar in the same WHERE")) {
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

    object_changes.clear();

    tests.expect(
        succeeded(
            G.visit_object_changes(
                &object_changes,
                collect_object_change)) &&
        object_changes.size() == 1 &&
        object_changes[0].handle ==
            fixture.scalar &&
        object_changes[0].live &&
        object_changes[0].construction ==
            construction_value::constant(
                construction_kind::
                    unsigned_integer,
                9) &&
        G.object_construction_entries().size() == 1,
        "sparse object write set reactivates the same WHERE with appended construction");

    const auto old_live_links =
        G.live_link_count();

    const auto old_link_slots =
        G.link_count();

    const auto old_stale_links =
        G.stale_link_count();

    if (!tests.expect(
            succeeded(
                G.retire(
                    fixture.link)) &&
            !G.contains(
                fixture.link) &&
            G.live_link_count() + 1 ==
                old_live_links &&
            G.link_count() ==
                old_link_slots &&
            G.stale_link_count() ==
                old_stale_links + 1,
            "BUILD graph_delta retires link without moving its WHERE")) {
        return;
    }

    std::vector<graph_delta_link_change>
        link_changes;

    const auto collect_link_change =
        [](void* context,
           const graph_delta_link_change& change) noexcept
        -> server_status {

            try {
                static_cast<
                    std::vector<
                        graph_delta_link_change>*>(
                            context)->push_back(
                                change);

                return server_status::success;
            }
            catch (...) {
                return server_status::io_error;
            }
        };

    tests.expect(
        succeeded(
            G.visit_link_changes(
                &link_changes,
                collect_link_change)) &&
        link_changes.size() == 1 &&
        link_changes[0].kind ==
            graph_delta_change_kind::patch &&
        link_changes[0].handle ==
            fixture.link &&
        !link_changes[0].live &&
        G.appended_link_count() == 0,
        "sparse link write set carries retirement at the same WHERE");

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
            old_live_links &&
        G.link_count() ==
            old_link_slots &&
        G.stale_link_count() ==
            old_stale_links,
        "BUILD graph_delta reactivates link in the same WHERE");

    link_changes.clear();

    tests.expect(
        succeeded(
            G.visit_link_changes(
                &link_changes,
                collect_link_change)) &&
        link_changes.size() == 1 &&
        link_changes[0].handle ==
            fixture.link &&
        link_changes[0].live &&
        link_changes[0].value.source ==
            old_link.source &&
        link_changes[0].value.target ==
            old_link.target,
        "sparse link write set reactivates the same WHERE without append");

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
                    fixture.left_identity,
                    fixture.value_member,
                },
                {
                    appended_identity,
                    fixture.peer_member,
                },
                appended_link)) &&
        appended_link.value() ==
            baseline.link_count() + 1,
        "BUILD graph_delta appends link after baseline slots");

    compiled_project_graph_write_plan
        graph_write_plan;

    if (!tests.expect(
            prepare_compiled_project_graph_write_plan(
                G,
                graph_write_plan) ==
                compiled_project_image_result::success,
            "prepare sparse compiled Graph write plan")) {

        return;
    }

    const auto expected_graph_payload_bytes =
        G.type_patch_count() *
            sizeof(type_entry) +
        G.object_patch_count() *
            sizeof(object_entry) +
        G.link_patch_count() *
            sizeof(link_record) +
        G.initialization_change_count() *
            sizeof(object_initialization_record) +
        G.type_entries().size() *
            (sizeof(type_entry) +
             sizeof(identity_ref)) +
        G.member_entries().size() *
            (sizeof(member_record) +
             sizeof(construction_value)) +
        G.base_entries().size() *
            sizeof(base_record) +
        G.object_entries().size() *
            (sizeof(object_entry) +
             sizeof(identity_ref)) +
        G.object_construction_entries().size() *
            sizeof(construction_value) +
        G.link_entries().size() *
            sizeof(link_record) +
        G.derived_type_entries().size() *
            sizeof(derived_type_record) +
        G.endpoint_path_entries().size() *
            sizeof(endpoint_path_record) +
        G.endpoint_path_step_entries().size() *
            sizeof(endpoint_path_step);

    tests.expect(
        graph_write_plan.type_patch_count ==
            G.type_patch_count() &&
        graph_write_plan.object_patch_count ==
            G.object_patch_count() &&
        graph_write_plan.link_patch_count ==
            G.link_patch_count() &&
        graph_write_plan.initialization_change_count ==
            G.initialization_change_count() &&
        graph_write_plan.appended_types.first_slot ==
            baseline.type_slot_count() + 1 &&
        graph_write_plan.appended_types.count ==
            G.type_entries().size() &&
        graph_write_plan.appended_members.begin ==
            baseline.member_count() &&
        graph_write_plan.appended_members.count ==
            G.member_entries().size() &&
        graph_write_plan.appended_bases.begin ==
            baseline.base_count() &&
        graph_write_plan.appended_bases.count ==
            G.base_entries().size() &&
        graph_write_plan.appended_objects.first_slot ==
            baseline.object_slot_count() + 1 &&
        graph_write_plan.appended_objects.count ==
            G.object_entries().size() &&
        graph_write_plan.appended_object_construction.first_slot ==
            baseline.object_construction_count() + 1 &&
        graph_write_plan.appended_object_construction.count ==
            G.object_construction_entries().size() &&
        graph_write_plan.appended_links.first_slot ==
            baseline.link_slot_count() + 1 &&
        graph_write_plan.appended_links.count ==
            G.link_entries().size() &&
        graph_write_plan.appended_derived_types.first_slot ==
            baseline.derived_type_count() + 1 &&
        graph_write_plan.appended_derived_types.count ==
            G.derived_type_entries().size() &&
        graph_write_plan.graph_payload_bytes ==
            expected_graph_payload_bytes,
        "sparse compiled Graph plan preserves physical baseline+append numbering");

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
        view.source_file_count() == 5 &&
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

        test_class_abi_persistence(
            tests);

        test_class_abi_multiple_base_persistence(
            tests);

        test_endpoint_path_persistence(
            tests);

        test_graph_derived_type_invariants(
            tests);

        test_graph_reference_invariants(
            tests);

        compiled_fixture fixture;

        if (!build_fixture(
                tests,
                fixture)) {

            return 1;
        }

        test_source_map_provenance(tests, fixture);

        test_object_initialization_persistence_runtime(
            tests);

        test_assign_overlay(
            tests);

        test_graph_dense_projection(
            tests);

        test_graph_delta_initialization_overlay(
            tests);

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

        test_graph_append_tail_boundary(
            tests,
            first);

        test_sparse_fixed_graph_writes(
            tests,
            fixture,
            first);

        test_sparse_in_place_payload_reuse(
            tests,
            fixture,
            first);

        test_round_trip(
            tests,
            fixture,
            first);

        test_runtime_layout(
            tests,
            fixture,
            first);

        test_runtime_query(
            tests,
            first);

        test_runtime_object_query(
            tests,
            first);

        test_runtime_type_query(
            tests,
            first);

        test_runtime_link_query(
            tests,
            first);

        test_runtime_ic_cross_generation(
            tests,
            fixture,
            first);

        test_runtime_ic_codec(
            tests);

        test_runtime_ic_snapshot(
            tests,
            first);

        test_runtime_ic_reset(
            tests,
            first);

        test_windows_x86_target_runtime(
            tests,
            fixture,
            first);

        test_windows_class_abi_runtime(
            tests);

        test_runtime_layout_tail_alignment(
            tests);

        test_fixed_direct_arrays(
            tests);

        test_fixed_direct_subobject_links(
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

        test_writable_mapping_grow_existing(
            tests);

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
