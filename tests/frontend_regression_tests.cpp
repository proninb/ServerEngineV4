#include "project/frontend/directive_executor.hpp"
#include "project/frontend/lexer.hpp"
#include "project/frontend/lexical_generation.hpp"
#include "project/graph/graph.hpp"
#include "project/parser/parser.hpp"
#include "project/preprocessor/preprocessor.hpp"
#include "project/preprocessor_configuration.hpp"
#include "project/semantic/identity.hpp"
#include "project/source/source_map.hpp"
#include "project/string/string_table.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cw::server {
namespace {

class test_state final {
public:
    [[nodiscard]] bool expect(
        bool condition,
        std::string_view name) {

        if (condition) {
            return true;
        }

        ++failures;
        std::cerr << "FAIL: " << name << '\n';
        return false;
    }

    std::size_t failures = 0;
};

class temporary_source final {
public:
    temporary_source(
        std::string_view label,
        std::string_view content) {

        const auto nonce =
            std::chrono::steady_clock::now().
                time_since_epoch().count();

        path_value =
            std::filesystem::temp_directory_path() /
            ("server_engine_v4_" +
             std::string{label} +
             "_" +
             std::to_string(nonce) +
             ".hpp");

        std::ofstream stream{
            path_value,
            std::ios::binary |
                std::ios::trunc};

        if (!stream) {
            throw std::runtime_error{
                "Cannot create temporary frontend source"};
        }

        stream.write(
            content.data(),
            static_cast<std::streamsize>(
                content.size()));

        if (!stream) {
            throw std::runtime_error{
                "Cannot write temporary frontend source"};
        }
    }

    temporary_source(
        const temporary_source&) = delete;

    temporary_source& operator=(
        const temporary_source&) = delete;

    ~temporary_source() {
        std::error_code error;
        std::filesystem::remove(
            path_value,
            error);
    }

    [[nodiscard]] const std::filesystem::path&
    path() const noexcept {
        return path_value;
    }

private:
    std::filesystem::path path_value;
};

[[nodiscard]] bool prepare_root(
    test_state& tests,
    const std::filesystem::path& path,
    file_context& files,
    lexical_generation& lexical,
    file_id& root,
    file_kind kind = file_kind::header) {

    root = {};

    if (!tests.expect(
            succeeded(
                files.resolve(
                    path,
                    kind,
                    root)) &&
                root,
            "resolve frontend root")) {

        return false;
    }

    file_acquire_job job;

    if (!tests.expect(
            succeeded(
                files.prepare_acquire(
                    root,
                    job)),
            "prepare frontend root acquisition")) {

        return false;
    }

    file_acquire_result result;
    file_context::execute_acquire(
        job,
        result);

    bool changed = false;

    if (!tests.expect(
            result.kind ==
                file_acquire_result_kind::present &&
            succeeded(
                files.apply_acquire(
                    result,
                    changed)) &&
            files.content_available(root),
            "materialize frontend root")) {

        return false;
    }

    if (!tests.expect(
            succeeded(
                lexical.reset(
                    files.size(),
                    1)),
            "reset lexical generation")) {

        return false;
    }

    lexical_stream stream;
    lexical_error error;

    if (!tests.expect(
            succeeded(
                lexer::tokenize(
                    root,
                    files.content(root),
                    stream,
                    &error)),
            "tokenize frontend root")) {

        return false;
    }

    return tests.expect(
        succeeded(
            lexical.publish(
                root,
                0,
                stream)),
        "publish frontend root lexical state");
}

[[nodiscard]] bool prepare_all_roots(
    test_state& tests,
    file_context& files,
    lexical_generation& lexical,
    std::span<const file_id> roots) {

    if (!tests.expect(
            succeeded(
                lexical.reset(
                    files.size(),
                    1)),
            "reset multi-root lexical generation")) {

        return false;
    }

    for (const auto root : roots) {
        file_acquire_job job;

        if (!tests.expect(
                succeeded(
                    files.prepare_acquire(
                        root,
                        job)),
                "prepare multi-root acquisition")) {

            return false;
        }

        file_acquire_result result;
        file_context::execute_acquire(
            job,
            result);

        bool changed = false;

        if (!tests.expect(
                result.kind ==
                    file_acquire_result_kind::present &&
                succeeded(
                    files.apply_acquire(
                        result,
                        changed)) &&
                files.content_available(root),
                "materialize multi-root input")) {

            return false;
        }

        lexical_stream stream;
        lexical_error error;

        if (!tests.expect(
                succeeded(
                    lexer::tokenize(
                        root,
                        files.content(root),
                        stream,
                        &error)) &&
                succeeded(
                    lexical.publish(
                        root,
                        0,
                        stream)),
                "publish multi-root lexical state")) {

            return false;
        }
    }

    return true;
}

[[nodiscard]] server_status parse_file(
    test_state& tests,
    const std::filesystem::path& path,
    parser_failure& failure) {

    file_context files;
    lexical_generation lexical;
    file_id root;

    if (!prepare_root(
            tests,
            path,
            files,
            lexical,
            root)) {

        return server_status::
            project_configuration_invalid;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;

    return parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
}

void test_header_name_diagnostics(
    test_state& tests) {

    const auto check =
        [&](std::string_view source,
            std::uint32_t expected_offset,
            std::uint32_t expected_length,
            std::string_view name) {

            lexical_stream stream;
            lexical_error error;

            const auto status =
                lexer::tokenize(
                    file_id{1},
                    source,
                    stream,
                    &error);

            tests.expect(
                status ==
                    server_status::
                        project_configuration_invalid,
                std::string{name} + " status");

            tests.expect(
                error.reason ==
                    lexical_error_reason::
                        unterminated_header_name,
                std::string{name} + " reason");

            tests.expect(
                error.offset ==
                    expected_offset &&
                error.length ==
                    expected_length,
                std::string{name} + " range");
        };

    check(
        "#include \"bad\n",
        9,
        4,
        "quoted unterminated include");

    check(
        "#include <bad\r\n",
        9,
        4,
        "angled unterminated include");
}

void test_conditional_entry_floor(
    test_state& tests) {

    string_table strings;
    preprocessor state{strings};
    preprocessor_configuration configuration;

    tests.expect(
        succeeded(
            initialize_preprocessor(
                configuration,
                strings,
                state)),
        "initialize directive executor");

    directive_executor executor{
        strings,
        state};

    const file_id file{1};

    tests.expect(
        succeeded(
            executor.enter_file(file)),
        "enter outer directive file");

    preprocessing_directive opening;
    opening.kind =
        directive_kind::ifndef;
    opening.range.file = file;
    opening.range.source = {
        0,
        5,
    };
    opening.identifier.name = {
        0,
        5,
    };

    directive_execution_result result;
    directive_execution_error error;

    tests.expect(
        succeeded(
            executor.execute(
                opening,
                "A_HPP",
                result,
                &error)),
        "open outer conditional");

    tests.expect(
        succeeded(
            executor.enter_file(file)),
        "enter recursive same-file directive entry");

    preprocessing_directive closing;
    closing.kind =
        directive_kind::endif;
    closing.range.file = file;
    closing.range.source = {
        0,
        5,
    };

    tests.expect(
        executor.execute(
            closing,
            "A_HPP",
            result,
            &error) ==
            server_status::
                project_configuration_invalid &&
        error.kind ==
            directive_execution_error_kind::
                unmatched_endif,
        "inner endif cannot close parent entry conditional");
}

[[nodiscard]] std::string nested_namespaces(
    std::size_t depth) {

    std::string source;

    for (std::size_t index = 0;
         index < depth;
         ++index) {

        source +=
            "namespace n" +
            std::to_string(index) +
            " {\n";
    }

    for (std::size_t index = 0;
         index < depth;
         ++index) {

        source += "}\n";
    }

    return source;
}

void test_self_include_guard(
    test_state& tests) {

    const auto nonce =
        std::chrono::steady_clock::now().
            time_since_epoch().count();

    const auto path =
        std::filesystem::temp_directory_path() /
        ("server_engine_v4_self_include_" +
         std::to_string(nonce) +
         ".hpp");

    const auto filename =
        path.filename().string();

    const std::string text =
        "#ifndef A_HPP\n"
        "#define A_HPP\n"
        "#include \"" +
        filename +
        "\"\n"
        "#endif\n";

    {
        std::ofstream stream{
            path,
            std::ios::binary |
                std::ios::trunc};

        if (!stream) {
            throw std::runtime_error{
                "Cannot create self-include regression source"};
        }

        stream.write(
            text.data(),
            static_cast<std::streamsize>(
                text.size()));

        if (!stream) {
            throw std::runtime_error{
                "Cannot write self-include regression source"};
        }
    }

    parser_failure failure;

    tests.expect(
        succeeded(
            parse_file(
                tests,
                path,
                failure)),
        "guarded self-include completes without unterminated conditional");

    std::error_code error;
    std::filesystem::remove(
        path,
        error);
}

void test_parser_scope_limit(
    test_state& tests) {

    {
        const auto text =
            nested_namespaces(
                parser_scope_depth_limit);

        const temporary_source source{
            "namespace_limit_ok",
            text};

        parser_failure failure;

        tests.expect(
            succeeded(
                parse_file(
                    tests,
                    source.path(),
                    failure)),
            "parser accepts configured maximum namespace depth");
    }

    {
        const auto text =
            nested_namespaces(
                parser_scope_depth_limit + 1);

        const temporary_source source{
            "namespace_limit_fail",
            text};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::
                    project_configuration_invalid,
            "parser rejects namespace depth above limit");

        tests.expect(
            failure.kind ==
                parser_failure_kind::unsupported &&
            failure.detail ==
                "Namespace nesting exceeds the supported parser scope depth",
            "parser reports namespace depth reason");
    }
}

void test_header_source_semantic_split(
    test_state& tests) {

    const temporary_source source{
        "source_before_header",
        "A instance;\n"
        "int value = 7;\n"};

    const temporary_source header{
        "header_after_source",
        "struct A { int field; };\n"};

    file_context files;
    lexical_generation lexical;

    file_id source_id;
    file_id header_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)) &&
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            source_id.value() == 1 &&
            header_id.value() == 2,
            "Source may precede Header in Project order")) {

        return;
    }

    const std::array<file_id, 2> roots{
        source_id,
        header_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "Header semantic pass precedes Source regardless of file_id order")) {

        return;
    }

    const auto type_name =
        strings.find("A");

    const auto instance_name =
        strings.find("instance");

    const auto value_name =
        strings.find("value");

    const auto type_identity =
        identities.find(
            identities.root(),
            type_name,
            identity_kind::type);

    const auto instance_identity =
        identities.find(
            identities.root(),
            instance_name,
            identity_kind::object);

    const auto value_identity =
        identities.find(
            identities.root(),
            value_name,
            identity_kind::object);

    const auto instance =
        G.find_object(
            instance_identity);

    const auto value =
        G.find_object(
            value_identity);

    construction_value value_initial;

    const auto type_handle_value =
        G.find_type(
            type_identity);

    const auto source_dependencies =
        sources.root_dependencies(
            source_id);

    tests.expect(
        type_identity &&
        type_handle_value &&
        instance &&
        value &&
        G.construction(
            value,
            value_initial) &&
        value_initial.kind ==
            construction_kind::unsigned_integer &&
        value_initial.bits() == 7,
        "Source consumes completed Header types and retains object initialization");

    tests.expect(
        source_dependencies.size() == 1 &&
        source_dependencies[0] ==
            source_dependency_ref::type(
                type_handle_value),
        "Source semantic root records dependency on global Header type");
}

void test_source_preprocessor_rejected(
    test_state& tests) {

    const temporary_source source{
        "source_preprocessor_rejected",
        "#define X int\n"
        "X value;\n"};

    file_context files;
    lexical_generation lexical;
    file_id root;

    if (!prepare_root(
            tests,
            source.path(),
            files,
            lexical,
            root,
            file_kind::source)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    const auto status =
        parse_semantic_project(
            files,
            lexical,
            1,
            configuration,
            strings,
            identities,
            G,
            sources,
            &failure);

    tests.expect(
        status ==
            server_status::project_configuration_invalid &&
        failure.kind ==
            parser_failure_kind::preprocessing &&
        failure.detail ==
            "C++ preprocessing directives are not supported in Source inputs",
        "Source rejects C++ preprocessing");
}

void test_record_scratch_isolation(test_state& tests) {
    const temporary_source source{
        "record_scratch_isolation",
        "struct A { int x = 7; int y = 9; A() : x(11) {} };\n"
        "struct B { int x; };\n"
        "struct Empty {};\n"
        "struct Forward;\n"
        "struct C { int x = 3; int y; int z = 5; };\n"
        "struct D { int x; D() : x(13) {} };\n"};

    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, source.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    if (!tests.expect(succeeded(parse_semantic_project(
            files, lexical, 1, configuration, strings, identities, G, sources,
            &failure)), "parse records with growing/shrinking scratch storage")) {
        return;
    }
    const auto check = [&](std::string_view name, std::size_t count,
                           std::string_view member, std::uint64_t value) {
        const auto type = G.find_type(identities.find(
            identities.root(), strings.find(name), identity_kind::type));
        const auto* initial = G.construction(type, G.find_member(type, strings.find(member)));
        (void)tests.expect(type && G.members(type).size() == count && initial != nullptr &&
                     initial->kind == (value == 0 ? construction_kind::zero :
                                                  construction_kind::unsigned_integer) &&
                     initial->bits() == value,
                     "record members and initialization do not leak between definitions");
    };
    check("A", 2, "x", 11);
    check("A", 2, "y", 9);
    check("B", 1, "x", 0);
    check("C", 3, "x", 3);
    check("C", 3, "y", 0);
    check("C", 3, "z", 5);
    check("D", 1, "x", 13);
}

void test_header_static_constructor_binding(
    test_state& tests) {

    const temporary_source first{
        "header_static_a",
        "static int a = 5;\n"
        "struct A {\n"
        "    int& b;\n"
        "    A() : b(a) {}\n"
        "};\n"};

    const temporary_source second{
        "header_static_b",
        "static int a = 7;\n"
        "struct B {\n"
        "    int& b;\n"
        "    B() : b(a) {}\n"
        "};\n"};

    file_context files;
    lexical_generation lexical;

    file_id first_id;
    file_id second_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    first.path(),
                    file_kind::header,
                    first_id)) &&
            succeeded(
                files.resolve(
                    second.path(),
                    file_kind::header,
                    second_id)),
            "resolve Header static roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        first_id,
        second_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse Header static constructor bindings")) {

        return;
    }

    tests.expect(
        G.object_count() == 2,
        "internal static object is distinct per semantic root");

    const auto first_static =
        G.object_at(0);

    const auto second_static =
        G.object_at(1);

    const auto* first_object =
        G.find(first_static);

    const auto* second_object =
        G.find(second_static);

    construction_value first_initial;
    construction_value second_initial;

    tests.expect(
        first_object != nullptr &&
        second_object != nullptr &&
        first_object->internal_static() &&
        second_object->internal_static() &&
        G.construction(
            first_static,
            first_initial) &&
        G.construction(
            second_static,
            second_initial) &&
        first_initial.kind ==
            construction_kind::unsigned_integer &&
        second_initial.kind ==
            construction_kind::unsigned_integer &&
        first_initial.bits() == 5 &&
        second_initial.bits() == 7,
        "Header static storage and initialization retained in G");

    const auto a_name =
        strings.find("A");

    const auto b_name =
        strings.find("B");

    const auto member_name =
        strings.find("b");

    const auto a_type =
        G.find_type(
            identities.find(
                identities.root(),
                a_name,
                identity_kind::type));

    const auto b_type =
        G.find_type(
            identities.find(
                identities.root(),
                b_name,
                identity_kind::type));

    const auto a_member =
        G.find_member(
            a_type,
            member_name);

    const auto b_member =
        G.find_member(
            b_type,
            member_name);

    const auto* a_initial =
        G.construction(
            a_type,
            a_member);

    const auto* b_initial =
        G.construction(
            b_type,
            b_member);

    tests.expect(
        a_initial != nullptr &&
        b_initial != nullptr &&
        a_initial->kind ==
            construction_kind::object_binding &&
        b_initial->kind ==
            construction_kind::object_binding &&
        a_initial->operand ==
            first_static.value() &&
        b_initial->operand ==
            second_static.value(),
        "constructor reference binds to root-local Header static object");

    const auto static_name =
        strings.find("a");

    tests.expect(
        !identities.find(
            identities.root(),
            static_name,
            identity_kind::object),
        "Header internal static is not visible as a Source Project object");
}


void test_source_link_semantic_dependencies(
    test_state& tests) {

    const temporary_source header{
        "source_link_dependency_header",
        "struct T { int& in; int out; };\n"};

    const temporary_source source{
        "source_link_dependency_source",
        "T x;\n"
        "T y;\n"
        "x.in = y.out;\n"};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve Source link dependency roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    preprocessor_configuration configuration;
    parser_failure failure;

    if (!tests.expect(
            succeeded(
                parse_semantic_project(
                    files,
                    lexical,
                    roots.size(),
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse Source link dependency fixture")) {

        return;
    }

    const auto type_identity =
        identities.find(
            identities.root(),
            strings.find("T"),
            identity_kind::type);

    const auto x_identity =
        identities.find(
            identities.root(),
            strings.find("x"),
            identity_kind::object);

    const auto y_identity =
        identities.find(
            identities.root(),
            strings.find("y"),
            identity_kind::object);

    const auto type =
        G.find_type(
            type_identity);

    const auto x =
        G.find_object(
            x_identity);

    const auto y =
        G.find_object(
            y_identity);

    const auto dependencies =
        sources.root_dependencies(
            source_id);

    const auto type_dependents =
        sources.dependents(
            source_dependency_ref::type(
                type));

    const auto x_dependents =
        sources.dependents(
            source_dependency_ref::object(
                x));

    const auto y_dependents =
        sources.dependents(
            source_dependency_ref::object(
                y));

    tests.expect(
        type &&
        x &&
        y &&
        dependencies.size() == 3 &&
        dependencies[0] ==
            source_dependency_ref::type(
                type) &&
        dependencies[1] ==
            source_dependency_ref::object(
                x) &&
        dependencies[2] ==
            source_dependency_ref::object(
                y) &&
        type_dependents.size() == 1 &&
        type_dependents[0] == source_id &&
        x_dependents.size() == 1 &&
        x_dependents[0] == source_id &&
        y_dependents.size() == 1 &&
        y_dependents[0] == source_id,
        "Source link records record-layout and object dependencies");
}

void test_source_link_failure_provenance(
    test_state& tests) {

    const temporary_source header{
        "source_link_failure_header",
        "struct T { int& in; int a; int b; };\n"};

    const temporary_source source{
        "source_link_failure_source",
        "T x;\n"
        "T y;\n"
        "x.in = y.a;\n"
        "x.in = y.b;\n"};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve Source link provenance roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map provenance;
    preprocessor_configuration configuration;
    parser_failure failure;

    tests.expect(
        !succeeded(
            parse_semantic_project(
                files,
                lexical,
                roots.size(),
                configuration,
                strings,
                identities,
                G,
                provenance,
                &failure)),
        "reject conflicting Source link");

    const auto contributions =
        provenance.contribution_entries();

    tests.expect(
        !provenance.finalized() &&
        G.link_count() == 1 &&
        contributions.size() == 4 &&
        contributions[0].file == header_id &&
        contributions[1].file == source_id &&
        contributions[2].file == source_id &&
        contributions[3].file == source_id,
        "failed Source link operation adds no provenance");
}

void test_semantic_type_diagnostics(
    test_state& tests) {

    {
        const std::string text{
            "static Missing value;\n"};

        const temporary_source source{
            "undeclared_object_type",
            text};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file &&
            failure.source.offset ==
                text.find("Missing") &&
            failure.source.length == 7 &&
            failure.detail ==
                "Named type is not declared in the visible semantic scope",
            "undeclared object type keeps precise source range");
    }

    {
        const std::string text{
            "struct A {\n"
            "    int out;\n"
            "    double& in = out;\n"
            "};\n"};

        const temporary_source source{
            "reference_member_type_mismatch",
            text};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file &&
            failure.source.offset ==
                text.rfind("out") &&
            failure.source.length == 3 &&
            failure.detail ==
                "Reference binding type does not match bound member type",
            "reference mismatch reports bound member token");
    }

    {
        const std::string text{
            "static int source;\n"
            "struct A {\n"
            "    double& in;\n"
            "    A() : in(source) {}\n"
            "};\n"};

        const temporary_source source{
            "constructor_reference_type_mismatch",
            text};

        parser_failure failure;

        const auto status =
            parse_file(
                tests,
                source.path(),
                failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file &&
            failure.source.offset ==
                text.rfind("source") &&
            failure.source.length == 6 &&
            failure.detail ==
                "Reference binding type does not match bound object type",
            "constructor mismatch reports source token");
    }

{
    const std::string text{
        "struct T { int value; };\n"
        "static T x = 7;\n"};

    const temporary_source source{
        "record_object_scalar_initializer",
        text};

    parser_failure failure;

    const auto status =
        parse_file(
            tests,
            source.path(),
            failure);

    tests.expect(
        status ==
            server_status::project_configuration_invalid &&
        failure.kind ==
            parser_failure_kind::semantic &&
        failure.file &&
        failure.source.offset ==
            text.rfind("7") &&
        failure.source.length == 1 &&
        failure.detail ==
            "Object initializer is not compatible with target type",
        "record object rejects scalar initializer at literal");
}

{
    const std::string text{
        "struct T { int value = 1.5; };\n"};

    const temporary_source source{
        "integral_member_real_initializer",
        text};

    parser_failure failure;

    const auto status =
        parse_file(
            tests,
            source.path(),
            failure);

    tests.expect(
        status ==
            server_status::project_configuration_invalid &&
        failure.kind ==
            parser_failure_kind::semantic &&
        failure.file &&
        failure.source.offset ==
            text.find("1.5") &&
        failure.source.length == 3 &&
        failure.detail ==
            "Initializer is not compatible with target type",
        "integral member rejects real initializer at literal");
}

    {
        const std::string header_text{
            "struct T { double& in; int out; };\n"};

        const std::string source_text{
            "T x;\n"
            "T y;\n"
            "x.in = y.out;\n"};

        const temporary_source header{
            "link_type_mismatch_header",
            header_text};

        const temporary_source source{
            "link_type_mismatch_source",
            source_text};

        file_context files;
        lexical_generation lexical;
        file_id header_id;
        file_id source_id;

        if (!tests.expect(
                succeeded(
                    files.resolve(
                        header.path(),
                        file_kind::header,
                        header_id)) &&
                succeeded(
                    files.resolve(
                        source.path(),
                        file_kind::source,
                        source_id)),
                "resolve link mismatch roots")) {

            return;
        }

        const std::array<file_id, 2> roots{
            header_id,
            source_id};

        if (!prepare_all_roots(
                tests,
                files,
                lexical,
                roots)) {

            return;
        }

        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;

        const auto status =
            parse_semantic_project(
                files,
                lexical,
                roots.size(),
                configuration,
                strings,
                identities,
                G,
                sources,
                &failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file == source_id &&
            failure.source.offset ==
                source_text.rfind("out") &&
            failure.source.length == 3 &&
            failure.detail ==
                "Link source type does not match target reference type",
            "link mismatch reports source member token");
    }

{
    const std::string header_text{
        "struct T { int out; int& in = out; };\n"};

    const std::string source_text{
        "T x;\n"
        "T y;\n"
        "x.in = y.out;\n"};

    const temporary_source header{
        "link_default_override_header",
        header_text};

    const temporary_source source{
        "link_default_override_source",
        source_text};

    file_context files;
    lexical_generation lexical;
    file_id header_id;
    file_id source_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    source.path(),
                    file_kind::source,
                    source_id)),
            "resolve default-override link roots")) {

        return;
    }

    const std::array<file_id, 2> roots{
        header_id,
        source_id};

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;

    const auto status =
        parse_semantic_project(
            files,
            lexical,
            roots.size(),
            configuration,
            strings,
            identities,
            G,
            sources,
            &failure);

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("T"),
                identity_kind::type));

    const auto input =
        G.find_member(
            type,
            strings.find("in"));

    const auto* default_binding =
        G.construction(
            type,
            input);

    tests.expect(
        succeeded(status) &&
        G.link_count() == 1 &&
        default_binding != nullptr &&
        default_binding->kind ==
            construction_kind::member_binding,
        "per-object link preserves and overrides the type-level default binding");
}

    {
        const std::string header_text{
            "struct T { int value; };\n"};

        const std::string source_text{
            "T x;\n"
            "T y;\n"
            "x.value = y.value;\n"};

        const temporary_source header{
            "link_target_value_header",
            header_text};

        const temporary_source source{
            "link_target_value_source",
            source_text};

        file_context files;
        lexical_generation lexical;
        file_id header_id;
        file_id source_id;

        if (!tests.expect(
                succeeded(
                    files.resolve(
                        header.path(),
                        file_kind::header,
                        header_id)) &&
                succeeded(
                    files.resolve(
                        source.path(),
                        file_kind::source,
                        source_id)),
                "resolve non-reference link roots")) {

            return;
        }

        const std::array<file_id, 2> roots{
            header_id,
            source_id};

        if (!prepare_all_roots(
                tests,
                files,
                lexical,
                roots)) {

            return;
        }

        preprocessor_configuration configuration;
        string_table strings;
        identity_space identities{strings};
        graph G;
        source_map sources;
        parser_failure failure;

        const auto status =
            parse_semantic_project(
                files,
                lexical,
                roots.size(),
                configuration,
                strings,
                identities,
                G,
                sources,
                &failure);

        tests.expect(
            status ==
                server_status::project_configuration_invalid &&
            failure.kind ==
                parser_failure_kind::semantic &&
            failure.file == source_id &&
            failure.source.offset ==
                source_text.find("value") &&
            failure.source.length == 5 &&
            failure.detail ==
                "Link target member must be a reference",
            "link target must be a native reference");
    }
}

void test_parser_provenance(test_state &tests) {
    const temporary_source common{"common_provenance", "struct Shared { int field; };"};
    const auto include = "#include \"" + common.path().filename().string() + "\"\n";
    const temporary_source first{"first_provenance", include};
    const temporary_source second{"second_provenance", include};
    file_context files;
    lexical_generation lexical;
    file_id a, b;
    if (!prepare_root(tests, first.path(), files, lexical, a) ||
        !prepare_root(tests, second.path(), files, lexical, b))
        return;
    lexical_stream stream;
    if (!tests.expect(succeeded(lexer::tokenize(a, files.content(a), stream)) &&
                          succeeded(lexical.publish(a, 0, stream)),
                      "restore first root tokens"))
        return;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    preprocessor_configuration configuration;
    parser_failure failure;
    tests.expect(succeeded(parse_semantic_project(
                     files, lexical, 2, configuration, strings, identities, G, sources, &failure)),
                 "parse shared discovered include");
    tests.expect(files.size() == 3 && sources.finalized() && sources.file_entries().size() == 3 &&
                     sources.contribution_entries().size() == 2 &&
                     sources.file_index_entries().size() == 2 &&
                     sources.root(file_id{1}).size() == 1 &&
                     sources.root(file_id{2}).size() == 1 &&
                     sources.contribution_entries()[0].file == file_id{3} &&
                     sources.contribution_entries()[1].file == file_id{3} &&
                     sources.type_presence_entries()[0] == source_type_presence{2, 2},
                 "parser preserves root-owned provenance for a shared physical include");
    const auto rejected = [&](std::string_view initial, std::string_view conflict,
                              std::size_t expected_contributions) {
        const temporary_source child{"conflict_provenance", conflict};
        const auto text =
            std::string{initial} + "\n#include \"" + child.path().filename().string() + "\"\n";
        const temporary_source root{"conflicting_root", text};
        file_context inputs;
        lexical_generation tokens;
        file_id id;
        if (!prepare_root(tests, root.path(), inputs, tokens, id))
            return;
        string_table atoms;
        identity_space names{atoms};
        graph candidate;
        source_map provenance;
        parser_failure error;
        tests.expect(
            !succeeded(parse_semantic_project(
                inputs, tokens, 1, configuration, atoms, names, candidate, provenance, &error)),
            "reject semantic conflict");
        bool only_root = true;
        for (const auto &entry : provenance.contribution_entries())
            only_root = only_root && entry.file == id;
        tests.expect(!provenance.finalized() &&
                         provenance.contribution_entries().size() == expected_contributions && only_root,
                     "failed G operation adds no child provenance");
    };
    rejected("struct T { int a; };", "struct T { double a; };", 1);
    rejected("static int value;", "static double value;", 1);
}
}
}

int main() {
    using namespace cw::server;

    try {
        test_state tests;

        test_header_source_semantic_split(
            tests);

        test_source_preprocessor_rejected(
            tests);

        test_header_static_constructor_binding(
            tests);

        test_record_scratch_isolation(tests);

        test_source_link_semantic_dependencies(
            tests);

        test_source_link_failure_provenance(
            tests);

        test_semantic_type_diagnostics(
            tests);

        test_parser_provenance(tests);

        test_header_name_diagnostics(
            tests);

        test_conditional_entry_floor(
            tests);

        test_self_include_guard(
            tests);

        test_parser_scope_limit(
            tests);

        if (tests.failures != 0) {
            std::cerr
                << tests.failures
                << " frontend regression test(s) failed\n";
            return 1;
        }

        std::cout
            << "frontend regression tests passed\n";

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
