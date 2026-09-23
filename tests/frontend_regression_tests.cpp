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
#include <iostream>
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
    file_id& root) {

    root = {};

    if (!tests.expect(
            succeeded(
                files.resolve(
                    path,
                    file_kind::header,
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
    rejected("int value;", "double value;", 1);
    rejected("struct T { int a; int b; }; T x; T y; x.a = y.a;", "x.a = y.b;", 4);
}
}
}

int main() {
    using namespace cw::server;

    try {
        test_state tests;

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
