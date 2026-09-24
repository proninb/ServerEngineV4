#include "filesystem_path.hpp"
#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"
#include "project/file/file_context.hpp"
#include "project/graph/graph.hpp"
#include "project/persistence/source_save.hpp"
#include "project/source/source_map.hpp"
#include "project/persistence/compiled_project.hpp"

#include <array>
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

    graph G;
    source_map sources;
    string_table strings;
    identity_space identities{strings};
    string_id name;
    identity_ref identity;
    type_handle type;
    tests.expect(
        succeeded(strings.intern("Shared", name)) &&
            succeeded(identities.resolve(identities.root(), name, identity_kind::type, identity)) &&
            succeeded(G.declare_record(identity, graph_record_kind::struct_type, type)),
        "presence graph fixture");
    string_id field_name, object_name; identity_ref object_identity;
    object_handle object; link_handle link;
    tests.expect(succeeded(strings.intern("value", field_name)) && succeeded(strings.intern("instance", object_name)) &&
        succeeded(identities.resolve(identities.root(),object_name,identity_kind::object,object_identity)),"presence member/object identities");
    const member_record field{field_name,G.intrinsic(intrinsic_type::signed_int),graph_member_access::public_access};
    tests.expect(succeeded(G.define_record(type,graph_record_kind::struct_type,{&field,1})) &&
        succeeded(G.add_object(object_identity,G.named(type),object)),"presence object fixture");
    const object_endpoint endpoint{object,G.find_member(type,field_name)};
    tests.expect(succeeded(G.add_link(endpoint,endpoint,link)),"presence link fixture");
    for (auto owner : {first, second}) {
        tests.expect(
            succeeded(sources.begin_root(owner)) &&
                succeeded(sources.add(second, source_data_ref::type_definition(identity))) &&
                succeeded(sources.add(second, source_data_ref::object(object_identity))) &&
                succeeded(sources.add(second, source_data_ref::link(link))) &&
                succeeded(sources.end_root()),
            "source-save root ownership");
    }

    if (!tests.expect(succeeded(sources.finalize(files.size(), identities, G)),
                      "finalize source-save semantic presence")) {
        return;
    }

    source_save_layout layout;

    if (!tests.expect(prepare_source_save_layout(files, sources, layout) ==
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

    if (!tests.expect(encode_source_save_image(files, sources, layout, writable.bytes()) ==
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

    tests.expect(writable_view.type_presence_count() == 1 &&
                     writable_view.type_presence(0) == source_type_presence{2, 2},
                 "presence persisted by root ownership");
    tests.expect(writable_view.object_presence_count()==1 && writable_view.link_presence_count()==1 &&
        writable_view.object_presence(0)==2 && writable_view.link_presence(0)==2,"object and link presence persisted");
    assign_table assigns;
    compiled_project_layout compiled_layout;
    tests.expect(prepare_compiled_project_layout(
                     strings, identities, G, assigns, files, sources, compiled_layout) ==
                     compiled_project_image_result::success,
                 "prepare cross-artifact fixture");
    std::vector<std::byte> compiled_bytes(compiled_layout.size());
    tests.expect(
        encode_compiled_project_image(
            strings, identities, G, assigns, files, sources, compiled_layout, compiled_bytes) ==
            compiled_project_image_result::success,
        "encode cross-artifact fixture");
    compiled_project_view compiled;
    tests.expect(compiled.bind(compiled_bytes) == compiled_project_image_result::success &&
                     verify_source_save_presence(writable_view, compiled) ==
                         source_save_result::success,
                 "presence matches compiled root ownership");
    auto altered = std::vector<std::byte>(writable.bytes().begin(), writable.bytes().end());
    // Presence has one type pair, one object and one link counter before SHA-256.
    altered[altered.size() - 32 - 16] = std::byte{1};
    source_save_view stale;
    tests.expect(stale.bind(altered) == source_save_result::success &&
                     verify_source_save_presence(stale, compiled) ==
                         source_save_result::invalid_image,
                 "cross-artifact validation rejects stale presence");

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

    file_id found_path;

    tests.expect(
        succeeded(
            writable_view.find_path(
                first_path,
                found_path)) &&
            found_path == first,
        "mmap path index resolves existing file_id");

    found_path = {};

    tests.expect(
        succeeded(
            writable_view.find_path(
                root / "missing.hpp",
                found_path)) &&
            !found_path,
        "mmap path index reports missing path without scan");

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

    file_context build_files;

    tests.expect(
        succeeded(
            build_files.bind_baseline(
                persisted_view)) &&
            build_files.baseline_bound() &&
            build_files.size() == 2 &&
            build_files.contains(first) &&
            build_files.contains(second),
        "BUILD File Context binds SourceSave without dense reconstruction");

    file_id baseline_found;

    tests.expect(
        succeeded(
            build_files.find(
                first_path,
                baseline_found)) &&
            baseline_found == first &&
        succeeded(
            build_files.resolve(
                second_path,
                file_kind::header,
                baseline_found)) &&
            baseline_found == second,
        "BUILD File Context preserves persisted file_id path lookup");

    const auto baseline_dependencies =
        build_files.dependencies(
            first);

    tests.expect(
        baseline_dependencies.size() == 1 &&
            baseline_dependencies[0] == second,
        "BUILD File Context reads committed topology directly from mmap");


    std::vector<file_id>
        fallback_candidates;

    source_save_change_scan
        candidate_scan;

    if (tests.expect(
            succeeded(
                scan_source_save_change_candidates(
                    persisted_view,
                    fallback_candidates,
                    &candidate_scan)) &&
            candidate_scan.metrics.fallback &&
            candidate_scan.metrics.current_files == 2 &&
            candidate_scan.metrics.candidate_files == 2 &&
            fallback_candidates.size() == 2 &&
            fallback_candidates[0] == first &&
            fallback_candidates[1] == second,
            "portable SourceSave fallback selects candidates without pre-reading files")) {

        std::vector<file_id>
            semantic_changed;

        source_save_change_classification_metrics
            classification;

        tests.expect(
            succeeded(
                classify_source_save_changes(
                    persisted_view,
                    fallback_candidates,
                    build_files,
                    semantic_changed,
                    &classification)) &&
            semantic_changed.empty() &&
            classification.files_read == 2 &&
            classification.semantic_changed_files == 0 &&
            !build_files.content_available(first) &&
            !build_files.content_available(second),
            "exact candidate classifier reads once and keeps unchanged baseline mmap-only");
    }

    file_acquire_job baseline_job;
    file_acquire_result baseline_result;
    bool baseline_changed = true;

    if (tests.expect(
            succeeded(
                build_files.prepare_acquire(
                    first,
                    baseline_job)),
            "prepare sparse baseline acquisition")) {

        file_context::execute_acquire(
            baseline_job,
            baseline_result);

        tests.expect(
            baseline_result.kind ==
                file_acquire_result_kind::present &&
            succeeded(
                build_files.apply_acquire(
                    baseline_result,
                    baseline_changed)) &&
            !baseline_changed &&
            build_files.content_available(
                first),
            "unchanged baseline file materializes sparse content overlay");
    }

    file_id appended;

    tests.expect(
        succeeded(
            build_files.resolve(
                root / "new.hpp",
                file_kind::header,
                appended)) &&
            appended ==
                file_id{3} &&
            build_files.size() == 3 &&
            build_files.kind(appended) ==
                file_kind::header,
        "BUILD File Context appends new file_id after committed lineage");

    tests.expect(
        succeeded(
            build_files.begin_dependency_replacement(
                first)) &&
        succeeded(
            build_files.add_dependency(
                first,
                appended)) &&
        succeeded(
            build_files.add_dependency(
                first,
                appended)) &&
        succeeded(
            build_files.finalize_dependency_topology()),
        "finalize sparse BUILD dependency replacement");

    const auto replaced_dependencies =
        build_files.dependencies(
            first);

    const auto old_target_dependents =
        build_files.dependents(
            second);

    const auto new_target_dependents =
        build_files.dependents(
            appended);

    tests.expect(
        replaced_dependencies.size() == 1 &&
            replaced_dependencies[0] ==
                appended &&
        old_target_dependents.empty() &&
        new_target_dependents.size() == 1 &&
            new_target_dependents[0] ==
                first,
        "BUILD topology replaces forward adjacency and applies sparse reverse delta");

    found_path = {};

    tests.expect(
        succeeded(
            persisted_view.find_path(
                second_path,
                found_path)) &&
            found_path == second,
        "persisted path index survives read-only reopen");
}


void test_affected_semantic_roots(
    test_state& tests,
    const std::filesystem::path& root) {

    const auto project_path =
        root / "roots_project.json";

    const auto root_path =
        root / "roots_root.hpp";

    const auto child_path =
        root / "roots_child.hpp";

    if (!tests.expect(
            write_text(
                project_path,
                "{}\n") &&
            write_text(
                root_path,
                "#include \"roots_child.hpp\"\n") &&
            write_text(
                child_path,
                "\n"),
            "write affected-root fixture files")) {

        return;
    }

    file_context files;

    file_id project;
    file_id semantic_root;
    file_id child;

    if (!tests.expect(
            succeeded(
                files.resolve(
                    project_path,
                    file_kind::project,
                    project)) &&
            succeeded(
                files.resolve(
                    root_path,
                    file_kind::header,
                    semantic_root)) &&
            succeeded(
                files.resolve(
                    child_path,
                    file_kind::header,
                    child)),
            "resolve affected-root fixture") ||
        !tests.expect(
            acquire(
                files,
                project) &&
            acquire(
                files,
                semantic_root) &&
            acquire(
                files,
                child),
            "acquire affected-root fixture")) {

        return;
    }

    constexpr std::size_t unrelated_count = 64;

    for (std::size_t index = 0;
         index < unrelated_count;
         ++index) {

        const auto path =
            root /
            ("roots_unrelated_" +
             std::to_string(index) +
             ".hpp");

        if (!tests.expect(
                write_text(
                    path,
                    "\n"),
                "write unrelated affected-root fixture")) {

            return;
        }

        file_id unrelated;

        if (!tests.expect(
                succeeded(
                    files.resolve(
                        path,
                        file_kind::header,
                        unrelated)) &&
            acquire(
                files,
                unrelated),
            "materialize unrelated affected-root fixture")) {

            return;
        }
    }

    if (!tests.expect(
            succeeded(
                files.add_dependency(
                    project,
                    semantic_root)) &&
            succeeded(
                files.add_dependency(
                    semantic_root,
                    child)) &&
            succeeded(
                files.finalize_dependency_topology()),
            "finalize affected-root physical topology")) {

        return;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;
    source_map sources;

    if (!tests.expect(
            succeeded(
                sources.reset(
                    files.size())) &&
            succeeded(
                sources.begin_root(
                    semantic_root)) &&
            succeeded(
                sources.end_root()) &&
            succeeded(
                sources.finalize(
                    files.size(),
                    identities,
                    G)),
            "finalize zero-contribution semantic root")) {

        return;
    }

    source_save_layout layout;

    if (!tests.expect(
            prepare_source_save_layout(
                files,
                sources,
                layout) ==
                    source_save_result::success,
            "prepare affected-root SourceSave")) {

        return;
    }

    std::vector<std::byte>
        image(layout.size());

    if (!tests.expect(
            encode_source_save_image(
                files,
                sources,
                layout,
                image) ==
                    source_save_result::success,
            "encode affected-root SourceSave")) {

        return;
    }

    source_save_view persisted;

    if (!tests.expect(
            persisted.bind(image) ==
                source_save_result::success,
            "bind affected-root SourceSave")) {

        return;
    }

    const std::array<file_id, 1>
        semantic_changed{
            child};

    std::vector<file_id> affected;

    source_save_affected_metrics
        affected_metrics;

    if (!tests.expect(
            succeeded(
                collect_source_save_affected(
                    persisted,
                    semantic_changed,
                    affected,
                    &affected_metrics)) &&
            affected_metrics.visited_files ==
                affected.size() &&
            affected_metrics.visited_files == 3 &&
            affected_metrics.dependency_edges == 2 &&
            affected_metrics.visited_slots <
                persisted.file_count(),
            "collect sparse affected physical closure without file_count marker")) {

        return;
    }

    std::vector<file_id> roots;

    tests.expect(
        succeeded(
            collect_source_save_semantic_roots(
                persisted,
                affected,
                roots)) &&
        roots.size() == 1 &&
        roots[0] == semantic_root,
        "OLD physical closure selects zero-contribution semantic root");
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

        test_affected_semantic_roots(
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
