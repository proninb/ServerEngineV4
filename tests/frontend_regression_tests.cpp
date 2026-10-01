#include "project/frontend/directive_executor.hpp"
#include "project/frontend/lexer.hpp"
#include "project/frontend/lexical_generation.hpp"
#include "project/frontend/semantic_input.hpp"
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

void test_conversion_operators(test_state& tests) {
    const temporary_source header{"conversion_operators",
        "struct Target {};\n"
        "struct Value { int data; Value() : data(7) {}\n"
        "operator char*(); operator const char*() const;\n"
        "explicit operator bool() const noexcept;\n"
        "operator int&() &; operator double&&() &&;\n"
        "operator int* const*() const; operator Target() const;\n"
        "operator unsigned long(void) const; operator float() = delete; };\n"
        "struct Base { virtual operator bool() const = 0; };\n"
        "struct Derived : Base { operator bool() const override; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse conversion operator declarations");
    (void)tests.expect(G.type_count() == 4 && G.member_count() == 1,
        "conversion operators do not introduce data members");

    for (const auto text : {
        "struct Bad { operator int(int argument); };",
        "struct Bad { operator Unknown(); };",
        "struct Bad { operator int[2](); };",
        "struct Bad { operator void&(); };",
        "struct Bad { operator int&*(); };",
        "struct Bad { operator int&&&(); };",
        "struct Bad { operator int() = default; };",
        "struct Bad { operator int() { return 1; } };",
        "struct Bad { operator int() override; };"}) {
        const temporary_source invalid{"invalid_conversion", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject invalid or unsupported conversion declarations");
    }
}

void test_assignment_operators(test_state& tests) {
    const temporary_source header{"assignment_operators",
        "struct Value { int data; Value() : data(7) {}\n"
        "Value& operator=(const char* value);\n"
        "Value& operator=(const Value&) & = default;\n"
        "Value& operator=(Value&&) noexcept = delete;\n"
        "void operator=(int); };\n"
        "struct Base { virtual Base& operator=(int) = 0; };\n"
        "struct Derived : Base { Base& operator=(int) override; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse assignment operator declarations");
    (void)tests.expect(G.type_count() == 3 && G.member_count() == 1,
        "assignment declarations do not add data members");
    for (const auto text : {
        "struct Bad { Bad& operator=(); };",
        "struct Bad { Bad& operator=(void); };",
        "struct Bad { Bad& operator=(int, int); };",
        "struct Bad { Bad& operator=(int x = 1); };",
        "struct Bad { Bad& operator=(...); };",
        "struct Bad { Bad& operator=; };",
        "struct Bad { static Bad& operator=(int); };",
        "struct Bad { Bad& operator=(int) { return *this; } };",
        "struct Bad {}; Bad& operator=(int);",
        "struct Bad { Bad& operator+(int); };"}) {
        const temporary_source invalid{"invalid_assignment", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject invalid or unsupported assignment declarations");
    }
}

void test_subscript_operators(test_state& tests) {
    const temporary_source header{"subscript_operators",
        "struct Value { short data[32];\n"
        "short& operator[](int index);\n"
        "const short& operator[](int) const & noexcept;\n"
        "short operator[](unsigned int) && = delete; };\n"
        "struct Base { virtual short& operator[](int) = 0; };\n"
        "struct Derived : Base { short& operator[](int) override; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse subscript operator declarations");
    (void)tests.expect(G.type_count() == 3 && G.member_count() == 1,
        "subscript declarations do not add instance data members");
    for (const auto text : {
        "struct Bad { int operator[(); };",
        "struct Bad { int operator[1](int); };",
        "struct Bad { int operator[]; };",
        "struct Bad { int operator[](); };",
        "struct Bad { int operator[](void); };",
        "struct Bad { int operator[](int, int); };",
        "struct Bad { int operator[](int x = 0); };",
        "struct Bad { int operator[](...) ; };",
        "struct Bad { int operator[](int) = default; };",
        "struct Bad { int operator[](int) { return 0; } };",
        "struct Bad { static int operator[](int); };",
        "int operator[](int);"}) {
        const temporary_source invalid{"invalid_subscript", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject malformed or unsupported subscript declarations");
    }
}

void test_microsoft_int64(test_state& tests) {
    const temporary_source header{"microsoft_int64",
        "struct Wide { __int64 a; signed __int64 b; unsigned __int64 c;\n"
        "operator __int64() const; operator unsigned __int64() const; };\n"};
    file_context files;
    lexical_generation lexical;
    file_id root;
    if (!prepare_root(tests, header.path(), files, lexical, root)) {
        return;
    }
    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;
    parser_failure failure;
    const auto status = parse_semantic_project(
        files, lexical, 1, configuration, strings, identities, G, sources, &failure);
    (void)tests.expect(succeeded(status), "parse Microsoft int64 declarations and conversions");
    (void)tests.expect(G.type_count() == 1 && G.member_count() == 3,
        "Microsoft int64 members retained");
    const auto type = G.find_type(identities.find(identities.root(), strings.find("Wide"), identity_kind::type));
    if (type) {
        for (const auto name : {"a", "b", "c"}) {
            const auto member = G.find_member(type, strings.find(name));
            intrinsic_type intrinsic = intrinsic_type::none;
            const auto* record = G.member(type, member);
            (void)tests.expect(record && G.intrinsic(record->type, intrinsic) &&
                intrinsic == (std::string_view{name} == "c"
                    ? intrinsic_type::unsigned_long_long : intrinsic_type::signed_long_long),
                "Microsoft int64 maps to the correct signedness and width");
        }
    }
    for (const auto text : {
        "struct Bad { short __int64 value; };",
        "struct Bad { long __int64 value; };",
        "struct Bad { unsigned signed __int64 value; };",
        "struct Bad { __int64 int value; };"}) {
        const temporary_source invalid{"invalid_int64", text};
        (void)tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "reject invalid Microsoft int64 type combinations");
    }
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

void test_pragma_once_and_angled_includes(test_state& tests) {
    const temporary_source child{"pragma_once_child",
        "#pragma once // physical file guard\nVALUE\n"};
    const auto filename = child.path().filename().string();
    const temporary_source root{"pragma_once_root",
        "#include <" + filename + ">\n#include \"./" + filename + "\"\n"};

    file_context files;
    lexical_generation lexical;
    file_id root_id;
    if (!prepare_root(tests, root.path(), files, lexical, root_id)) {
        return;
    }
    preprocessor_configuration configuration;
    configuration.predefines.push_back({"VALUE", "ConfiguredValue"});
    string_table strings;
    string_id expected;
    tests.expect(succeeded(strings.intern("ConfiguredValue", expected)), "intern configured alias");
    semantic_input input{files, lexical, configuration, strings};
    for (int replay = 0; replay < 2; ++replay) {
        tests.expect(succeeded(input.start(root_id, semantic_input_mode::header)), "start once replay");
        std::size_t identifiers = 0;
        server_status status = server_status::success;
        while (!input.finished() && succeeded(status)) {
            semantic_token token;
            status = input.next(token);
            if (succeeded(status) && token.kind == token_kind::identifier) {
                ++identifiers;
                tests.expect(token.identifier == expected, "predefine applies inside angled include");
            }
        }
        tests.expect(succeeded(status) && identifiers == 1,
            "once deduplicates mixed include spellings and resets for next root replay");
    }

    const temporary_source inactive{"inactive_once",
        "#ifdef NEVER_DEFINED\n#pragma once\n#endif\nVALUE\n"};
    const auto inactive_name = inactive.path().filename().string();
    const temporary_source inactive_root{"inactive_once_root",
        "#include <" + inactive_name + ">\n#include <" + inactive_name + ">\n"};
    file_context inactive_files;
    lexical_generation inactive_lexical;
    file_id inactive_id;
    if (prepare_root(tests, inactive_root.path(), inactive_files, inactive_lexical, inactive_id)) {
        semantic_input inactive_input{inactive_files, inactive_lexical, configuration, strings};
        tests.expect(succeeded(inactive_input.start(inactive_id, semantic_input_mode::header)),
            "start inactive once replay");
        std::size_t identifiers = 0;
        server_status status = server_status::success;
        while (!inactive_input.finished() && succeeded(status)) {
            semantic_token token;
            status = inactive_input.next(token);
            identifiers += succeeded(status) && token.kind == token_kind::identifier ? 1 : 0;
        }
        tests.expect(succeeded(status) && identifiers == 2, "inactive pragma once does not suppress inclusion");
    }

    const temporary_source self{"self_once", ""};
    {
        std::ofstream stream{self.path(), std::ios::binary | std::ios::trunc};
        stream << "#pragma once\n#include <" << self.path().filename().string()
               << ">\nstruct SelfOnce {};\n";
    }
    parser_failure failure;
    tests.expect(succeeded(parse_file(tests, self.path(), failure)), "pragma once stops self inclusion");
    for (const auto text : {"#pragma once extra\n", "#pragma unknown\n", "#include <>\n"}) {
        const temporary_source invalid{"invalid_once_include", text};
        tests.expect(!succeeded(parse_file(tests, invalid.path(), failure)),
            "unsupported pragma and empty include fail closed");
    }
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
                type_identity),
        "Source semantic root records dependency on global Header type");
}

void test_sparse_semantic_replay_order(
    test_state& tests) {

    const temporary_source source{
        "sparse_replay_source_first",
        "A instance;\n"
        "instance.field = 7;\n"};

    const temporary_source header{
        "sparse_replay_header_second",
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
            source_id.value() <
                header_id.value(),
            "prepare sparse replay roots with Source file_id before Header")) {

        return;
    }

    const std::array<file_id, 2>
        roots{
            source_id,
            header_id,
        };

    if (!prepare_all_roots(
            tests,
            files,
            lexical,
            roots)) {

        return;
    }

    preprocessor_configuration configuration;
    string_table strings;
    identity_space identities{
        strings};
    graph_delta G;
    source_map_delta sources;
    parser_failure failure;
    std::vector<parser_warning> warnings;

    if (!tests.expect(
            succeeded(
                parse_semantic_roots(
                    files,
                    lexical,
                    roots,
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure,
                    &warnings)),
            "sparse replay executes Header domain before Source domain")) {

        return;
    }

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("A"),
                identity_kind::type));

    const auto object =
        G.find_object(
            identities.find(
                identities.root(),
                strings.find("instance"),
                identity_kind::object));

    const auto member =
        G.find_member(
            type,
            strings.find("field"));

    object_initialization_record
        initialization;

    const auto replay_roots =
        sources.root_entries();

    tests.expect(
        type &&
        object &&
        member &&
        G.initialization(
            {
                object,
                endpoint_ref{member},
            },
            initialization) &&
        initialization.value.kind ==
            construction_kind::
                unsigned_integer &&
        initialization.value.bits() == 7,
        "sparse replay produces Graph delta with Source initialization");

    tests.expect(
        replay_roots.size() == 2 &&
        replay_roots[0].root ==
            header_id &&
        replay_roots[1].root ==
            source_id &&
        replay_roots[0].
            initializations.count == 0 &&
        replay_roots[1].
            initializations.count == 1 &&
        sources.
            initialization_target_entries().
                size() == 1 &&
        warnings.empty(),
        "BUILD provenance retains canonical Header then Source replay order");
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

void test_class_abi_semantics(
    test_state& tests) {

    const temporary_source source{
        "class_abi_semantics",
        "struct A {\n"
        "    virtual void f();\n"
        "    int a;\n"
        "};\n"
        "struct B : public A {\n"
        "    void f() override;\n"
        "    int b;\n"
        "};\n"
        "struct C : A {\n"
        "    virtual ~C();\n"
        "    int c;\n"
        "};\n"
        "struct Plain {\n"
        "    int value;\n"
        "};\n"};

    file_context files;
    lexical_generation lexical;
    file_id root;

    if (!prepare_root(
            tests,
            source.path(),
            files,
            lexical,
            root)) {

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
                    1,
                    configuration,
                    strings,
                    identities,
                    G,
                    sources,
                    &failure)),
            "parse class ABI semantic declarations")) {

        return;
    }

    const auto type =
        [&](std::string_view name) {
            return G.find_type(
                identities.find(
                    identities.root(),
                    strings.find(name),
                    identity_kind::type));
        };

    const auto a = type("A");
    const auto b = type("B");
    const auto c = type("C");
    const auto plain = type("Plain");

    const auto* a_entry = G.find(a);
    const auto* b_entry = G.find(b);
    const auto* c_entry = G.find(c);
    const auto* plain_entry = G.find(plain);

    const auto b_bases =
        G.bases(b);

    const auto c_bases =
        G.bases(c);

    tests.expect(
        a_entry != nullptr &&
        b_entry != nullptr &&
        c_entry != nullptr &&
        plain_entry != nullptr &&
        a_entry->polymorphic() &&
        b_entry->polymorphic() &&
        c_entry->polymorphic() &&
        !plain_entry->polymorphic() &&
        G.polymorphic(a) &&
        G.polymorphic(b) &&
        G.polymorphic(c) &&
        !G.polymorphic(plain),
        "virtual declarations and inheritance normalize to type polymorphic flag");

    tests.expect(
        b_bases.size() == 1 &&
        b_bases[0].type == a &&
        b_bases[0].access ==
            graph_member_access::
                public_access &&
        !b_bases[0].virtual_base() &&
        c_bases.size() == 1 &&
        c_bases[0].type == a,
        "single non-virtual base relation retained directly by type_entry");

    const temporary_source virtual_base{
        "class_abi_virtual_base",
        "struct A {};\n"
        "struct B : virtual A {};\n"};

    parser_failure virtual_failure;

    tests.expect(
        parse_file(
            tests,
            virtual_base.path(),
            virtual_failure) ==
                server_status::
                    project_configuration_invalid &&
        virtual_failure.kind ==
            parser_failure_kind::unsupported &&
        virtual_failure.detail ==
            "Virtual base classes are not implemented in CXX-CLASS-ABI-V1",
        "virtual inheritance fails closed before Runtime ABI layout");

    const temporary_source multiple_base{
        "class_abi_multiple_base",
        "struct A {};\n"
        "struct B {};\n"
        "struct C : A, B {};\n"};

    parser_failure multiple_failure;

    tests.expect(
        parse_file(
            tests,
            multiple_base.path(),
            multiple_failure) ==
                server_status::
                    project_configuration_invalid &&
        multiple_failure.kind ==
            parser_failure_kind::unsupported &&
        multiple_failure.detail ==
            "Multiple inheritance is not implemented in CXX-CLASS-ABI-V1",
        "multiple inheritance fails closed in V1 semantic slice");
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


void test_source_value_initialization(
    test_state& tests) {

    const temporary_source header{
        "source_value_initialization_header",
        "struct T { int param; int& in; int out; };\n"};

    const temporary_source source{
        "source_value_initialization_source",
        "T a;\n"
        "T b;\n"
        "a.param = 5;\n"
        "a.in = b.out;\n"};

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
            "resolve Source value initialization roots")) {

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
            "parse Source scalar initialization and reference link")) {

        return;
    }

    const auto a =
        G.find_object(
            identities.find(
                identities.root(),
                strings.find("a"),
                identity_kind::object));

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("T"),
                identity_kind::type));

    const auto param =
        G.find_member(
            type,
            strings.find("param"));

    const auto initializations =
        G.initialization_entries();

    tests.expect(
        a &&
        param &&
        initializations.size() == 1 &&
        initializations[0].target.object == a &&
        initializations[0].target.member ==
            endpoint_ref{param} &&
        initializations[0].value.kind ==
            construction_kind::unsigned_integer &&
        initializations[0].value.bits() == 5 &&
        G.link_count() == 1,
        "Source keeps scalar initialization separate from reference link");

    const temporary_source invalid_source{
        "source_value_copy_rejected",
        "T a;\n"
        "T b;\n"
        "a.param = b.out;\n"};

    file_context invalid_files;
    lexical_generation invalid_lexical;

    file_id invalid_header_id;
    file_id invalid_source_id;

    if (!tests.expect(
            succeeded(
                invalid_files.resolve(
                    header.path(),
                    file_kind::header,
                    invalid_header_id)) &&
            succeeded(
                invalid_files.resolve(
                    invalid_source.path(),
                    file_kind::source,
                    invalid_source_id)),
            "resolve invalid Source value-copy roots")) {

        return;
    }

    const std::array<file_id, 2> invalid_roots{
        invalid_header_id,
        invalid_source_id};

    if (!prepare_all_roots(
            tests,
            invalid_files,
            invalid_lexical,
            invalid_roots)) {

        return;
    }

    string_table invalid_strings;
    identity_space invalid_identities{
        invalid_strings};
    graph invalid_G;
    source_map invalid_sources;
    preprocessor_configuration
        invalid_configuration;
    parser_failure invalid_failure;

    const auto invalid_status =
        parse_semantic_project(
            invalid_files,
            invalid_lexical,
            invalid_roots.size(),
            invalid_configuration,
            invalid_strings,
            invalid_identities,
            invalid_G,
            invalid_sources,
            &invalid_failure);

    tests.expect(
        invalid_status ==
            server_status::
                project_configuration_invalid &&
        invalid_failure.kind ==
            parser_failure_kind::unsupported &&
        invalid_G.initialization_count() == 0,
        "Source rejects value-to-value assignment");
}


void test_source_value_initialization_last_wins(
    test_state& tests) {

    const temporary_source header{
        "source_value_last_wins_header",
        "struct T { int param = 1; };\n"};

    const std::string first_text{
        "T a;\n"
        "a.param = 5;\n"};

    const std::string second_text{
        "a.param = 7;\n"};

    const temporary_source first{
        "source_value_last_wins_first",
        first_text};

    const temporary_source second{
        "source_value_last_wins_second",
        second_text};

    file_context files;
    lexical_generation lexical;

    file_id header_id;
    file_id first_id;
    file_id second_id;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    header.path(),
                    file_kind::header,
                    header_id)) &&
            succeeded(
                files.resolve(
                    first.path(),
                    file_kind::source,
                    first_id)) &&
            succeeded(
                files.resolve(
                    second.path(),
                    file_kind::source,
                    second_id)),
            "resolve Source last-wins initialization roots")) {

        return;
    }

    const std::array<file_id, 3> roots{
        header_id,
        first_id,
        second_id};

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
    std::vector<parser_warning> warnings;

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
                    &failure,
                    &warnings)),
            "parse Source last-wins initialization")) {

        return;
    }

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("T"),
                identity_kind::type));

    const auto object_identity =
        identities.find(
            identities.root(),
            strings.find("a"),
            identity_kind::object);

    const auto object =
        G.find_object(
            object_identity);

    const auto member =
        G.find_member(
            type,
            strings.find("param"));

    const auto* default_value =
        G.construction(
            type,
            member);

    object_initialization_record init;

    const bool has_init =
        G.initialization(
            {
                object,
                endpoint_ref{member},
            },
            init);

    tests.expect(
        default_value != nullptr &&
        default_value->kind ==
            construction_kind::unsigned_integer &&
        default_value->bits() == 1 &&
        has_init &&
        init.value.kind ==
            construction_kind::unsigned_integer &&
        init.value.bits() == 7 &&
        G.initialization_count() == 1,
        "type default and object init remain distinct; last init wins");

    tests.expect(
        warnings.size() == 1 &&
        warnings[0].kind ==
            parser_warning_kind::
                duplicate_initialization &&
        warnings[0].file ==
            second_id &&
        warnings[0].source.offset ==
            second_text.find("param") &&
        warnings[0].source.length == 5,
        "duplicate object init emits warning at replacing target");

    const auto first_initializations =
        sources.root_initializations(
            first_id);

    const auto second_initializations =
        sources.root_initializations(
            second_id);

    tests.expect(
        first_initializations.size() == 1 &&
        second_initializations.size() == 1 &&
        first_initializations[0] ==
            object_endpoint{
                object,
                endpoint_ref{member}} &&
        second_initializations[0] ==
            object_endpoint{
                object,
                endpoint_ref{member}},
        "BUILD provenance keeps every root that produces the canonical init target");

    const auto second_contributions =
        sources.root(
            second_id);

    tests.expect(
        second_contributions.size() == 1 &&
        second_contributions[0].file ==
            second_id &&
        second_contributions[0].data.kind() ==
            source_data_kind::object &&
        second_contributions[0].data.slot() ==
            object_identity.slot(),
        "object init is provenance of its owning object");
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
                type_identity));

    const auto x_dependents =
        sources.dependents(
            source_dependency_ref::object(
                x_identity));

    const auto y_dependents =
        sources.dependents(
            source_dependency_ref::object(
                y_identity));

    tests.expect(
        type &&
        x &&
        y &&
        dependencies.size() == 3 &&
        dependencies[0] ==
            source_dependency_ref::type(
                type_identity) &&
        dependencies[1] ==
            source_dependency_ref::object(
                x_identity) &&
        dependencies[2] ==
            source_dependency_ref::object(
                y_identity) &&
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


void test_declarator_arrays(
    test_state& tests) {

    const std::string header_text{
        "struct T {\n"
        "    int values[2][3];\n"
        "    int* pointers[4];\n"
        "    int (*row)[3];\n"
        "    int (&view)[2][3] = values;\n"
        "    int out[4];\n"
        "    int (&in)[4] = out;\n"
        "};\n"};

    const std::string source_text{
        "T a;\n"
        "T b;\n"
        "T objects[2];\n"
        "b.in = a.out;\n"};

    const temporary_source header{
        "declarator_array_header",
        header_text};

    const temporary_source source{
        "declarator_array_source",
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
            "resolve declarator-array roots")) {

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
            "parse bounded arrays and parenthesized declarators")) {

        return;
    }

    const auto type =
        G.find_type(
            identities.find(
                identities.root(),
                strings.find("T"),
                identity_kind::type));

    if (!tests.expect(
            static_cast<bool>(
                type),
            "resolve declarator-array record")) {

        return;
    }

    const auto member_type =
        [&](std::string_view name,
            type_ref& output,
            member_index& member) {

            member =
                G.find_member(
                    type,
                    strings.find(name));

            const auto* value =
                G.member(
                    type,
                    member);

            if (value == nullptr) {
                return false;
            }

            output = value->type;
            return true;
        };

    const auto derived_is =
        [&](type_ref value,
            derived_type_kind kind,
            std::uint64_t payload,
            type_ref& child) {

            derived_type_record derived;

            if (!G.derived(
                    value,
                    derived) ||
                derived.kind != kind ||
                derived.payload != payload) {

                return false;
            }

            child = derived.child;
            return true;
        };

    const auto is_int =
        [&](type_ref value) {

            intrinsic_type intrinsic;

            return G.intrinsic(
                       value,
                       intrinsic) &&
                intrinsic ==
                    intrinsic_type::signed_int;
        };

    type_ref values_type;
    type_ref pointers_type;
    type_ref row_type;
    type_ref view_type;
    type_ref out_type;
    type_ref in_type;

    member_index values_member;
    member_index pointers_member;
    member_index row_member;
    member_index view_member;
    member_index out_member;
    member_index in_member;

    if (!tests.expect(
            member_type(
                "values",
                values_type,
                values_member) &&
            member_type(
                "pointers",
                pointers_type,
                pointers_member) &&
            member_type(
                "row",
                row_type,
                row_member) &&
            member_type(
                "view",
                view_type,
                view_member) &&
            member_type(
                "out",
                out_type,
                out_member) &&
            member_type(
                "in",
                in_type,
                in_member),
            "resolve declarator-array members")) {

        return;
    }

    type_ref child;
    type_ref leaf;

    tests.expect(
        derived_is(
            values_type,
            derived_type_kind::bounded_array,
            2,
            child) &&
        derived_is(
            child,
            derived_type_kind::bounded_array,
            3,
            leaf) &&
        is_int(leaf),
        "multidimensional array preserves C++ bound order");

    tests.expect(
        derived_is(
            pointers_type,
            derived_type_kind::bounded_array,
            4,
            child) &&
        derived_is(
            child,
            derived_type_kind::pointer,
            0,
            leaf) &&
        is_int(leaf),
        "array of pointers binds suffix outside pointer prefix");

    tests.expect(
        derived_is(
            row_type,
            derived_type_kind::pointer,
            0,
            child) &&
        derived_is(
            child,
            derived_type_kind::bounded_array,
            3,
            leaf) &&
        is_int(leaf),
        "parenthesized declarator distinguishes pointer to array");

    type_ref view_array;
    type_ref view_inner;

    tests.expect(
        derived_is(
            view_type,
            derived_type_kind::lvalue_reference,
            0,
            view_array) &&
        derived_is(
            view_array,
            derived_type_kind::bounded_array,
            2,
            view_inner) &&
        derived_is(
            view_inner,
            derived_type_kind::bounded_array,
            3,
            leaf) &&
        is_int(leaf),
        "reference to multidimensional array preserves native type shape");

    type_ref in_array;

    tests.expect(
        derived_is(
            out_type,
            derived_type_kind::bounded_array,
            4,
            child) &&
        is_int(child) &&
        derived_is(
            in_type,
            derived_type_kind::lvalue_reference,
            0,
            in_array) &&
        in_array == out_type,
        "whole-array reference uses exact array TypeRef");

    const auto* view_binding =
        G.construction(
            type,
            view_member);

    const auto* in_binding =
        G.construction(
            type,
            in_member);

    tests.expect(
        view_binding != nullptr &&
        view_binding->kind ==
            construction_kind::member_binding &&
        view_binding->operand ==
            values_member.value() + 1 &&
        in_binding != nullptr &&
        in_binding->kind ==
            construction_kind::member_binding &&
        in_binding->operand ==
            out_member.value() + 1,
        "array-reference defaults normalize to member bindings");

    const auto objects =
        G.find_object(
            identities.find(
                identities.root(),
                strings.find("objects"),
                identity_kind::object));

    const auto* object =
        G.find(objects);

    type_ref object_element;
    type_handle object_record;

    tests.expect(
        object != nullptr &&
        derived_is(
            object->type,
            derived_type_kind::bounded_array,
            2,
            object_element) &&
        G.named(
            object_element,
            object_record) &&
        object_record == type,
        "Project object may be a bounded array of records");

    tests.expect(
        G.link_count() == 1,
        "whole-array static link binds reference-to-array target");
}

void test_invalid_array_declarators(
    test_state& tests) {

    const auto reject =
        [&](std::string_view label,
            std::string text,
            parser_failure_kind expected_kind,
            std::string_view expected_detail,
            std::string_view marker) {

            const temporary_source source{
                label,
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
                        project_configuration_invalid &&
                failure.kind ==
                    expected_kind &&
                failure.file &&
                failure.source.offset ==
                    text.find(marker) &&
                failure.detail ==
                    expected_detail,
                label);
        };

    reject(
        "array_of_references",
        "struct Bad { int& values[4]; };\n",
        parser_failure_kind::semantic,
        "Arrays of references are not valid C++",
        "[");

    reject(
        "unbounded_array",
        "struct Bad { int values[]; };\n",
        parser_failure_kind::unsupported,
        "Unbounded arrays are not implemented",
        "[");

    reject(
        "zero_array_bound",
        "struct Bad { int values[0]; };\n",
        parser_failure_kind::semantic,
        "Array bound must be greater than zero",
        "0");

    reject(
        "array_bound_expression",
        "struct Bad { int values[2 + 2]; };\n",
        parser_failure_kind::unsupported,
        "Array bound expressions are not implemented",
        "+");
}


void test_subobject_link_endpoints(
    test_state& tests) {

    const std::string header_text{
        "struct T {\n"
        "    int values[2][3];\n"
        "    int& in;\n"
        "};\n"};

    const std::string source_text{
        "T a;\n"
        "T objects[2];\n"
        "objects[1].in = a.values[1][2];\n"
        "a.in = objects[0].values[0][1];\n"};

    const temporary_source header{
        "subobject_link_header",
        header_text};

    const temporary_source source{
        "subobject_link_source",
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
            "resolve subobject-link roots")) {

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
            "parse indexed subobject static links")) {

        return;
    }

    tests.expect(
        G.link_count() == 2 &&
        G.endpoint_path_count() == 3,
        "indexed endpoints intern only complex canonical paths");

    const auto links =
        G.link_entries();

    if (!tests.expect(
            links.size() == 2,
            "resolve indexed subobject links from dense G storage")) {

        return;
    }

    const auto& first_link =
        links[0];

    const auto& second_link =
        links[1];

    if (!tests.expect(
            first_link.target.member.is_path() &&
            first_link.source.member.is_path() &&
            second_link.target.member.is_member() &&
            second_link.source.member.is_path(),
            "direct and path endpoints preserve compact link representation")) {

        return;
    }

    endpoint_path_record target_path;
    endpoint_path_record source_path;

    const auto target_steps =
        G.endpoint_path_steps(
            first_link.target.member.path());

    const auto source_steps =
        G.endpoint_path_steps(
            first_link.source.member.path());

    tests.expect(
        G.endpoint_path(
            first_link.target.member.path(),
            target_path) &&
        G.endpoint_path(
            first_link.source.member.path(),
            source_path) &&
        target_steps.size() == 2 &&
        target_steps[0].kind ==
            endpoint_path_step_kind::array_index &&
        target_steps[0].value == 1 &&
        target_steps[1].kind ==
            endpoint_path_step_kind::member &&
        source_steps.size() == 3 &&
        source_steps[0].kind ==
            endpoint_path_step_kind::member &&
        source_steps[1].kind ==
            endpoint_path_step_kind::array_index &&
        source_steps[1].value == 1 &&
        source_steps[2].kind ==
            endpoint_path_step_kind::array_index &&
        source_steps[2].value == 2,
        "subobject path order matches source spelling");
}

void test_subobject_link_diagnostics(
    test_state& tests) {

    const std::string header_text{
        "struct T { int a[2]; int& in; };\n"};

    const auto reject =
        [&](std::string_view label,
            std::string source_text,
            parser_failure_kind kind,
            std::string_view detail,
            std::string_view marker) {

            const temporary_source header{
                std::string{label} +
                    "_header",
                header_text};

            const temporary_source source{
                std::string{label} +
                    "_source",
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
                    std::string{label} +
                        " resolve roots")) {

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
                    server_status::
                        project_configuration_invalid &&
                failure.kind ==
                    kind &&
                failure.file ==
                    source_id &&
                failure.source.offset ==
                    source_text.find(marker) &&
                failure.detail ==
                    detail,
                label);
        };

    reject(
        "subobject_link_out_of_bounds",
        "T x;\nx.in = x.a[2];\n",
        parser_failure_kind::semantic,
        "Array link index is outside the declared bound",
        "2]");

    reject(
        "subobject_link_index_expression",
        "T x;\nx.in = x.a[0 + 1];\n",
        parser_failure_kind::unsupported,
        "Array link index expressions are not implemented",
        "+");
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
                parser_failure_kind::unsupported &&
            failure.file == source_id &&
            failure.source.offset ==
                source_text.find("y.value") &&
            failure.source.length == 1 &&
            failure.detail ==
                "Source value initialization requires a scalar constant",
            "Source value initialization rejects value-to-value copy");
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

        test_sparse_semantic_replay_order(
            tests);

        test_source_preprocessor_rejected(
            tests);

        test_header_static_constructor_binding(
            tests);

        test_record_scratch_isolation(tests);

        test_class_abi_semantics(
            tests);

        test_source_value_initialization(
            tests);

        test_source_value_initialization_last_wins(
            tests);

        test_source_link_semantic_dependencies(
            tests);

        test_source_link_failure_provenance(
            tests);

        test_declarator_arrays(
            tests);

        test_invalid_array_declarators(
            tests);

        test_subobject_link_endpoints(
            tests);

        test_subobject_link_diagnostics(
            tests);

        test_semantic_type_diagnostics(
            tests);

        test_parser_provenance(tests);

        test_header_name_diagnostics(
            tests);

        test_conversion_operators(tests);
        test_assignment_operators(tests);
        test_subscript_operators(tests);
        test_microsoft_int64(tests);

        test_conditional_entry_floor(
            tests);

        test_pragma_once_and_angled_includes(tests);
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
