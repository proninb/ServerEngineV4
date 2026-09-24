#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"
#include "project/file/file_context.hpp"
#include "project/frontend/lexer.hpp"
#include "project/frontend/lexical_generation.hpp"
#include "project/frontend/source_discovery.hpp"
#include "project/frontend/lexical_stream.hpp"
#include "project/persistence/database.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
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

constexpr std::string_view database_fixture_source{
    "#include \"other.hpp\"\n"
    "struct A { int x; };\n"};

bool write_text(
    const std::filesystem::path& path,
    std::string_view value) {

    std::ofstream output(
        path,
        std::ios::binary |
            std::ios::trunc);

    if (!output) {
        return false;
    }

    output.write(
        value.data(),
        static_cast<std::streamsize>(
            value.size()));

    return static_cast<bool>(output);
}

bool prepare_fixture(
    test_state& tests,
    const std::filesystem::path& root,
    file_context& files,
    lexical_generation& lexical,
    file_id& header,
    file_id& project) {

    if (!tests.expect(
            write_text(
                root / "types.hpp",
                database_fixture_source),
            "write database fixture source")) {
        return false;
    }

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

    file_acquire_job job;
    file_acquire_result acquired;
    bool content_changed = false;

    if (!tests.expect(
            succeeded(
                files.prepare_acquire(
                    header,
                    job)),
            "prepare database fixture acquisition")) {
        return false;
    }

    file_context::execute_acquire(
        job,
        acquired);

    if (!tests.expect(
            succeeded(
                files.apply_acquire(
                    acquired,
                    content_changed)) &&
            content_changed &&
            files.content_available(header) &&
            files.content(header) ==
                database_fixture_source,
            "materialize exact database fixture source")) {
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

    lexical_stream stream;
    lexical_error error;

    return tests.expect(
            succeeded(
                lexer::tokenize(
                    header,
                    files.content(header),
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
    std::string_view header_content;
    std::string_view project_content;

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
            "non-C++ file has no lexical state") ||
        !tests.expect(
            mapped.content(
                header,
                header_content) &&
            header_content ==
                database_fixture_source,
            "read exact persisted Header source bytes") ||
        !tests.expect(
            !mapped.content(
                project,
                project_content),
            "non-C++ file has no persisted source bytes")) {
        return;
    }

    const auto content_baseline =
        mapped.content_baseline();

    std::string_view baseline_content;

    tests.expect(
        content_baseline.valid() &&
        content_baseline.file_count() ==
            files.size() &&
        content_baseline.read(
            header,
            baseline_content) &&
        baseline_content ==
            database_fixture_source,
        "database exports borrowed mmap source-content baseline");

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

    constexpr std::string_view replacement_source{
        "struct B { long value; };\n"};

    file_context replacement_files;
    file_id replacement_header;
    file_id replacement_project;

    if (!tests.expect(write_text(root / "types.hpp", replacement_source), "write replacement source") ||
        !tests.expect(
            succeeded(replacement_files.resolve(root / "types.hpp", file_kind::header, replacement_header)) &&
            replacement_header == header &&
            succeeded(replacement_files.resolve(root / "project.json", file_kind::project, replacement_project)) &&
            replacement_project == project,
            "resolve sparse replacement fixture")) {
        return;
    }

    file_acquire_job replacement_job;
    file_acquire_result replacement_acquired;
    bool replacement_changed = false;
    if (!tests.expect(succeeded(replacement_files.prepare_acquire(replacement_header, replacement_job)),
                      "prepare sparse replacement acquisition")) {
        return;
    }
    file_context::execute_acquire(replacement_job, replacement_acquired);
    if (!tests.expect(
            succeeded(replacement_files.apply_acquire(replacement_acquired, replacement_changed)) &&
            replacement_changed && replacement_files.content_available(replacement_header),
            "materialize sparse replacement bytes")) {
        return;
    }

    source_preparation_failure replacement_failure;
    source_replacement_metrics replacement_metrics;
    const std::array<file_id, 1> semantic_changed{header};

    tests.expect(
        succeeded(replace_source_lexical_state(
            replacement_files, build_lexical, semantic_changed,
            &replacement_failure, &replacement_metrics)) &&
        replacement_failure.kind == source_preparation_failure_kind::none &&
        replacement_metrics.masked_files == 1 &&
        replacement_metrics.retokenized_files == 1 &&
        replacement_metrics.missing_files == 0 &&
        replacement_metrics.active_lanes == 1 &&
        build_lexical.contains(header),
        "BUILD sparse lexical replacement masks and retokenizes exact changed source");

    lexical_stream replacement_stream;
    lexical_error replacement_error;
    tests.expect(
        succeeded(lexer::tokenize(header, replacement_source, replacement_stream, &replacement_error)) &&
        build_lexical.token_count(header) == replacement_stream.token_count(),
        "BUILD sparse lexical replacement matches direct lexer result");

    lexical_generation missing_lexical;

    if (!tests.expect(
            succeeded(
                missing_lexical.bind_baseline(
                    persisted_view.lexical_baseline(),
                    1)),
            "bind lexical baseline for missing-source replacement")) {
        return;
    }

    file_context missing_files;
    file_id missing_header;
    file_id missing_project;

    if (!tests.expect(
            succeeded(
                missing_files.resolve(
                    root / "types.hpp",
                    file_kind::header,
                    missing_header)) &&
            missing_header == header &&
            succeeded(
                missing_files.resolve(
                    root / "project.json",
                    file_kind::project,
                    missing_project)) &&
            missing_project == project,
            "resolve missing-source replacement fixture")) {
        return;
    }

    source_preparation_failure missing_failure;
    source_replacement_metrics missing_metrics;

    tests.expect(
        succeeded(
            replace_source_lexical_state(
                missing_files,
                missing_lexical,
                semantic_changed,
                &missing_failure,
                &missing_metrics)) &&
        missing_failure.kind ==
            source_preparation_failure_kind::none &&
        missing_metrics.masked_files == 1 &&
        missing_metrics.retokenized_files == 0 &&
        missing_metrics.missing_files == 1 &&
        missing_metrics.active_lanes == 0 &&
        !missing_lexical.contains(header),
        "BUILD missing changed Header masks stale lexical baseline without tokenization");

    const auto stable_replacement_words =
        build_lexical.words(header);

    const auto stable_first_word =
        stable_replacement_words.empty()
            ? 0u
            : stable_replacement_words[0];

    database_layout replacement_layout;

    tests.expect(
        prepare_database_layout(
            files,
            build_lexical,
            replacement_layout) ==
                database_image_result::success &&
        replacement_layout.size() != 0,
        "database layout reads merged baseline and sparse lexical overlay");

    const file_id appended{
        static_cast<std::uint32_t>(
            persisted_view.file_count() + 1)};

    lexical_stream growth_stream;

    bool growth_ready =
        succeeded(
            growth_stream.reset(
                appended,
                4096));

    for (std::uint32_t offset = 0;
         growth_ready &&
         offset < 2048;
         ++offset) {

        growth_ready =
            succeeded(
                growth_stream.append(
                    token_kind::identifier,
                    offset,
                    1));
    }

    tests.expect(
        !stable_replacement_words.empty() &&
        growth_ready &&
        succeeded(
            build_lexical.extend(
                persisted_view.file_count() + 1)) &&
        build_lexical.size() ==
            persisted_view.file_count() + 1 &&
        succeeded(
            build_lexical.publish(
                appended,
                0,
                growth_stream)) &&
        stable_replacement_words[0] ==
            stable_first_word,
        "native lexical descriptor survives arena growth without stale pointer");

    constexpr std::string_view appended_source{
        "struct C { int value; };\n"};

    lexical_generation appended_lexical;

    if (!tests.expect(
            succeeded(
                appended_lexical.bind_baseline(
                    persisted_view.lexical_baseline(),
                    1)),
            "bind lexical baseline for appended BUILD root")) {

        return;
    }

    file_context appended_files;

    file_id appended_existing_header;
    file_id appended_existing_project;
    file_id appended_header;

    if (!tests.expect(
            write_text(
                root / "appended.hpp",
                appended_source) &&
            succeeded(
                appended_files.resolve(
                    root / "types.hpp",
                    file_kind::header,
                    appended_existing_header)) &&
            appended_existing_header ==
                header &&
            succeeded(
                appended_files.resolve(
                    root / "project.json",
                    file_kind::project,
                    appended_existing_project)) &&
            appended_existing_project ==
                project &&
            succeeded(
                appended_files.resolve(
                    root / "appended.hpp",
                    file_kind::header,
                    appended_header)) &&
            appended_header ==
                appended,
            "resolve appended BUILD semantic root")) {

        return;
    }

    source_preparation_failure
        appended_failure;

    source_replacement_metrics
        appended_metrics;

    const std::array<file_id, 1>
        appended_replacement{
            appended_header};

    tests.expect(
        succeeded(
            replace_source_lexical_state(
                appended_files,
                appended_lexical,
                appended_replacement,
                &appended_failure,
                &appended_metrics)) &&
        appended_failure.kind ==
            source_preparation_failure_kind::none &&
        appended_metrics.masked_files == 0 &&
        appended_metrics.retokenized_files == 1 &&
        appended_metrics.missing_files == 0 &&
        appended_metrics.active_lanes == 1 &&
        appended_files.content_available(
            appended_header) &&
        appended_lexical.size() ==
            persisted_view.file_count() + 1 &&
        appended_lexical.contains(
            appended_header),
        "BUILD lexical replacement materializes and tokenizes appended semantic root");
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
