#include "filesystem_path.hpp"
#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"
#include "project/file/file_context.hpp"
#include "project/persistence/source_save.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace cw::server;

struct test_state final {
    int failures = 0;

    bool expect(
        bool condition,
        const char* name) {

        if (!condition) {
            ++failures;
            std::cerr << "FAILED: " << name << '\n';
        }

        return condition;
    }
};

class temporary_tree final {
public:
    temporary_tree() {
        root =
            std::filesystem::temp_directory_path() /
            "server_engine_v4_source_save_test";

        std::error_code error;

        std::filesystem::remove_all(
            root,
            error);

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

bool write_text(
    const std::filesystem::path& path,
    const char* text) {

    std::ofstream stream{
        path,
        std::ios::binary |
            std::ios::trunc};

    stream << text;

    return stream.good();
}

bool acquire(
    file_context& files,
    file_id file) {

    file_acquire_job job;

    if (!succeeded(
            files.prepare_acquire(
                file,
                job))) {

        return false;
    }

    file_acquire_result result;

    file_context::execute_acquire(
        job,
        result);

    bool changed = false;

    return succeeded(
        files.apply_acquire(
            result,
            changed));
}

void test_native_path_codec(
    test_state& tests,
    const std::filesystem::path& path) {

    const auto& native =
        path.native();

    const filesystem_native_path_view view{
        native.data(),
        native.size()};

    std::size_t required = 0;

    if (!tests.expect(
            filesystem_path_utf8_size(
                view,
                required) ==
                filesystem_path_result::success,
            "native path UTF-8 size")) {

        return;
    }

    std::vector<char> direct(
        required);

    std::size_t written = 0;

    if (!tests.expect(
            filesystem_path_to_utf8(
                view,
                direct,
                written) ==
                filesystem_path_result::success &&
            written == required,
            "native path direct UTF-8 encode")) {

        return;
    }

    std::string wrapped;

    tests.expect(
        filesystem_path_to_utf8(
            path,
            wrapped) ==
            filesystem_path_result::success &&
        wrapped ==
            std::string(
                direct.data(),
                written),
        "native direct codec matches path wrapper");

#if defined(_WIN32)
    tests.expect(
        wrapped.find('\\') ==
            std::string::npos,
        "persisted Windows path uses generic separators");
#endif
}

void test_direct_source_save(
    test_state& tests,
    const std::filesystem::path& root) {

    const auto first_path =
        root / "first.hpp";

    const auto second_path =
        root / "second.hpp";

    if (!tests.expect(
            write_text(
                first_path,
                "#pragma once\n") &&
            write_text(
                second_path,
                "struct value {};\n"),
            "write source-save fixture files")) {

        return;
    }

    test_native_path_codec(
        tests,
        first_path);

    file_context files;

    file_id first;
    file_id second;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    first_path,
                    file_kind::header,
                    first)) &&
            succeeded(
                files.resolve(
                    second_path,
                    file_kind::header,
                    second)),
            "resolve source-save files") ||
        !tests.expect(
            acquire(
                files,
                first) &&
            acquire(
                files,
                second),
            "acquire source-save files") ||
        !tests.expect(
            succeeded(
                files.add_dependency(
                    first,
                    second)),
            "add source-save dependency") ||
        !tests.expect(
            succeeded(
                files.finalize_dependency_topology()),
            "finalize source-save topology")) {

        return;
    }

    source_save_layout layout;

    if (!tests.expect(
            prepare_source_save_layout(
                files,
                layout) ==
                source_save_result::success &&
            layout.size() != 0,
            "prepare direct source-save layout")) {

        return;
    }

    const auto image_path =
        root / "source.bin";

    writable_file_mapping writable;

    if (!tests.expect(
            writable.create(
                image_path,
                layout.size()) ==
                writable_file_mapping_result::success,
            "create writable source.bin mmap")) {

        return;
    }

    if (!tests.expect(
            encode_source_save_image(
                files,
                layout,
                writable.bytes()) ==
                source_save_result::success,
            "encode source.bin directly into mmap")) {

        return;
    }

    if (!tests.expect(
            validate_source_save_image(
                writable.bytes()) ==
                source_save_result::success,
            "validate writable source.bin mmap")) {

        return;
    }

    source_save_view writable_view;

    if (!tests.expect(
            writable_view.bind(
                writable.bytes()) ==
                source_save_result::success,
            "bind writable source.bin mmap")) {

        return;
    }

    source_save_file_view first_state;
    source_save_file_view second_state;

    tests.expect(
        writable_view.file(
            first,
            first_state) &&
        writable_view.file(
            second,
            second_state),
        "read source-save records");

    tests.expect(
        first_state.dependencies.size() == 1 &&
        first_state.dependencies[0] == second &&
        second_state.dependents.size() == 1 &&
        second_state.dependents[0] == first,
        "source-save topology preserved");

    std::string expected_first;

    tests.expect(
        filesystem_path_to_utf8(
            first_path.lexically_normal(),
            expected_first) ==
            filesystem_path_result::success &&
        first_state.path_utf8 ==
            expected_first,
        "source-save UTF-8 path preserved");

    tests.expect(
        writable.flush() ==
            writable_file_mapping_result::success,
        "flush writable source.bin mmap");

    writable_view.reset();
    writable.reset();

    read_only_file_mapping persisted;

    if (!tests.expect(
            persisted.open(
                image_path) ==
                read_only_file_mapping_result::success,
            "reopen source.bin read-only")) {

        return;
    }

    source_save_view persisted_view;

    tests.expect(
        persisted_view.bind(
            persisted.bytes()) ==
            source_save_result::success &&
        validate_source_save_image(
            persisted.bytes()) ==
            source_save_result::success,
        "validate persisted source.bin mmap");
}

}

int main() {
    try {
        test_state tests;
        temporary_tree tree;

        if (!tests.expect(
                tree.valid(),
                "create source-save temporary tree")) {

            return 1;
        }

        test_direct_source_save(
            tests,
            tree.root);

        if (tests.failures != 0) {
            std::cerr
                << tests.failures
                << " source_save test(s) failed\n";

            return 1;
        }

        std::cout
            << "source_save tests passed\n";

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
