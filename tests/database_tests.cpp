#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"
#include "project/file/file_context.hpp"
#include "project/frontend/lexer.hpp"
#include "project/frontend/lexical_generation.hpp"
#include "project/frontend/lexical_stream.hpp"
#include "project/persistence/database.hpp"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

using namespace cw::server;

struct test_state final {
    int failures = 0;

    bool expect(
        bool condition,
        const char* name) {

        if (!condition) {
            ++failures;
            std::cerr
                << "FAILED: "
                << name
                << '\n';
        }

        return condition;
    }
};

class temporary_tree final {
public:
    temporary_tree() {
        root =
            std::filesystem::temp_directory_path() /
            "server_engine_v4_database_test";

        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(
            root,
            error);
        valid_value = !error;
    }

    ~temporary_tree() {
        std::error_code error;
        std::filesystem::remove_all(
            root,
            error);
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_value;
    }

    std::filesystem::path root;

private:
    bool valid_value = false;
};

bool prepare_fixture(
    test_state& tests,
    const std::filesystem::path& root,
    file_context& files,
    lexical_generation& lexical,
    file_id& header,
    file_id& project) {

    if (!tests.expect(
            succeeded(
                files.resolve(
                    root / "types.hpp",
                    file_kind::header,
                    header)) &&
            succeeded(
                files.resolve(
                    root / "project.json",
                    file_kind::project,
                    project)),
            "resolve database fixture files")) {
        return false;
    }

    if (!tests.expect(
            succeeded(
                lexical.reset(
                    files.size(),
                    1)),
            "reset database lexical generation")) {
        return false;
    }

    constexpr std::string_view source{
        "#include \"other.hpp\"\n"
        "struct A { int x; };\n"};

    lexical_stream stream;
    lexical_error error;

    return tests.expect(
            succeeded(
                lexer::tokenize(
                    header,
                    source,
                    stream,
                    &error)),
            "tokenize database lexical fixture") &&
        tests.expect(
            stream.word_count() != 0 &&
            stream.directive_count() != 0,
            "database fixture has words and directives") &&
        tests.expect(
            succeeded(
                lexical.publish(
                    header,
                    0,
                    stream)),
            "publish database lexical fixture");
}

void test_direct_database(
    test_state& tests,
    const std::filesystem::path& root) {

    file_context files;
    lexical_generation lexical;
    file_id header;
    file_id project;

    if (!prepare_fixture(
            tests,
            root,
            files,
            lexical,
            header,
            project)) {
        return;
    }

    database_layout layout;

    if (!tests.expect(
            prepare_database_layout(
                files,
                lexical,
                layout) ==
                    database_image_result::success &&
            layout.size() != 0,
            "prepare exact database layout")) {
        return;
    }

    const auto path =
        root / "database.bin";

    writable_file_mapping writable;

    if (!tests.expect(
            writable.create(
                path,
                layout.size()) ==
                writable_file_mapping_result::success,
            "create writable database mmap") ||
        !tests.expect(
            encode_database_image(
                files,
                lexical,
                layout,
                writable.bytes()) ==
                database_image_result::success,
            "encode database directly into mmap")) {
        return;
    }

    database_view mapped;

    if (!tests.expect(
            mapped.bind(
                writable.bytes()) ==
                database_image_result::success &&
            mapped.file_count() ==
                files.size(),
            "bind database mmap view")) {
        return;
    }

    database_lexical_file_view header_state;
    database_lexical_file_view project_state;

    if (!tests.expect(
            mapped.file(
                header,
                header_state) &&
            header_state.available,
            "read persisted header lexical state") ||
        !tests.expect(
            mapped.file(
                project,
                project_state) &&
            !project_state.available,
            "non-C++ file has no lexical state")) {
        return;
    }

    const auto words =
        lexical.words(header);
    const auto directives =
        lexical.directives(header);

    tests.expect(
        header_state.words.size() ==
            words.size() &&
        header_state.directives.size() ==
            directives.size() &&
        header_state.token_count ==
            lexical.token_count(header),
        "persisted lexical counts match");

    if (!words.empty()) {
        tests.expect(
            header_state.words[0] ==
                words[0],
            "persisted lexical word matches");
    }

    if (!directives.empty()) {
        const auto persisted =
            header_state.directives[0];

        tests.expect(
            persisted.word_offset ==
                directives[0].word_offset &&
            persisted.source_base ==
                directives[0].source_base,
            "persisted directive anchor matches");
    }

    if (!tests.expect(
            validate_database_image(
                writable.bytes()) ==
                database_image_result::success,
            "validate writable database mmap") ||
        !tests.expect(
            verify_database_image(
                writable.bytes(),
                files,
                lexical) ==
                database_image_result::success,
            "verify database against construction")) {
        return;
    }

    const auto original =
        writable.bytes()[0];

    writable.bytes()[0] =
        static_cast<std::byte>(
            std::to_integer<unsigned char>(
                original) ^
            0x01u);

    tests.expect(
        validate_database_image(
            writable.bytes()) ==
            database_image_result::invalid_image,
        "database corruption rejection");

    writable.bytes()[0] =
        original;

    if (!tests.expect(
            writable.flush() ==
                writable_file_mapping_result::success,
            "flush writable database mmap")) {
        return;
    }

    writable.reset();

    read_only_file_mapping persisted;

    if (!tests.expect(
            persisted.open(
                path) ==
                read_only_file_mapping_result::success,
            "reopen database read-only")) {
        return;
    }

    database_view persisted_view;

    tests.expect(
        persisted_view.bind(
            persisted.bytes()) ==
                database_image_result::success &&
        validate_database_image(
            persisted.bytes()) ==
                database_image_result::success,
        "bind and validate persisted database mmap");

    lexical_generation build_lexical;

    tests.expect(
        succeeded(
            build_lexical.bind_baseline(
                persisted_view.lexical_baseline(),
                1)) &&
        build_lexical.baseline_bound() &&
        build_lexical.size() == persisted_view.file_count() &&
        build_lexical.contains(header) &&
        !build_lexical.contains(project),
        "BUILD lexical generation binds database mmap without dense records");

    const auto baseline_words = build_lexical.words(header);
    const auto baseline_directives = build_lexical.directives(header);

    tests.expect(
        baseline_words.size() == lexical.words(header).size() &&
        baseline_directives.size() == lexical.directives(header).size() &&
        build_lexical.token_count(header) == lexical.token_count(header) &&
        !baseline_words.empty() &&
        baseline_words[0] == lexical.words(header)[0],
        "BUILD lexical baseline reads persisted words and directives directly");

    tests.expect(
        succeeded(build_lexical.begin_replacement(header)) &&
        !build_lexical.contains(header),
        "BUILD lexical replacement masks stale baseline before publish");

    constexpr std::string_view replacement_source{
        "struct B { long value; };\n"};

    lexical_stream replacement_stream;
    lexical_error replacement_error;

    tests.expect(
        succeeded(
            lexer::tokenize(
                header,
                replacement_source,
                replacement_stream,
                &replacement_error)) &&
        succeeded(
            build_lexical.publish(
                header,
                0,
                replacement_stream)) &&
        build_lexical.contains(header) &&
        build_lexical.token_count(header) == replacement_stream.token_count(),
        "BUILD lexical replacement publishes sparse native overlay");

    database_layout replacement_layout;

    tests.expect(
        prepare_database_layout(
            files,
            build_lexical,
            replacement_layout) ==
                database_image_result::success &&
        replacement_layout.size() != 0,
        "database layout reads merged baseline and sparse lexical overlay");

    tests.expect(
        succeeded(
            build_lexical.extend(
                persisted_view.file_count() + 1)) &&
        build_lexical.size() == persisted_view.file_count() + 1,
        "BUILD lexical generation appends one local record without baseline copy");
}

}

int main() {
    try {
        test_state tests;
        temporary_tree tree;

        if (!tests.expect(
                tree.valid(),
                "create database temporary tree")) {
            return 1;
        }

        test_direct_database(
            tests,
            tree.root);

        if (tests.failures != 0) {
            std::cerr
                << tests.failures
                << " database test(s) failed\n";
            return 1;
        }

        std::cout
            << "database tests passed\n";
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
