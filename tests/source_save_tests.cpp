#include "filesystem_path.hpp"
#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"
#include "project/file/file_context.hpp"
#include "project/graph/graph.hpp"
#include "project/graph/graph_delta.hpp"
#include "project/persistence/source_save.hpp"
#include "project/persistence/compiled_project_build.hpp"
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
    string_id value_name;
    string_id input_name;
    string_id object_name;

    identity_ref object_identity;

    object_handle object;
    link_handle link;

    tests.expect(
        succeeded(
            strings.intern(
                "value",
                value_name)) &&
        succeeded(
            strings.intern(
                "in",
                input_name)) &&
        succeeded(
            strings.intern(
                "instance",
                object_name)) &&
        succeeded(
            identities.resolve(
                identities.root(),
                object_name,
                identity_kind::object,
                object_identity)),
        "presence member/object identities");

    const auto integer_type =
        G.intrinsic(
            intrinsic_type::signed_int);

    type_ref reference_type;

    tests.expect(
        succeeded(
            G.derive(
                integer_type,
                derived_type_kind::lvalue_reference,
                0,
                reference_type)),
        "presence reference type");

    const std::array<member_record, 2> members{{
        {
            value_name,
            integer_type,
            graph_member_access::public_access,
        },
        {
            input_name,
            reference_type,
            graph_member_access::public_access,
        },
    }};

    tests.expect(
        succeeded(
            G.define_record(
                type,
                graph_record_kind::struct_type,
                members)) &&
        succeeded(
            G.add_object(
                object_identity,
                G.named(type),
                object,
                graph_object_internal_static)),
        "presence header-static object fixture");

    const object_endpoint source{
        object_identity,
        G.find_member(
            type,
            value_name)};

    const object_endpoint target{
        object_identity,
        G.find_member(
            type,
            input_name)};

    tests.expect(
        succeeded(
            G.add_link(
                source,
                target,
                link)),
        "presence link fixture");

    bool initialization_replaced = false;

    tests.expect(
        succeeded(
            G.add_initialization(
                source,
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    5),
                initialization_replaced)) &&
        !initialization_replaced &&
        succeeded(
            G.add_initialization(
                source,
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    7),
                initialization_replaced)) &&
        initialization_replaced,
        "presence initialization fixture keeps canonical last-wins value");

    for (auto owner : {first, second}) {
        tests.expect(
            succeeded(sources.begin_root(owner)) &&
                succeeded(
                    sources.add(
                        second,
                        source_data_ref::type(
                            identity,
                            owner == first))) &&
                succeeded(
                    sources.add(
                        owner,
                        source_data_ref::object(
                            object_identity))) &&
                succeeded(
                    sources.add_initialization(
                        source)) &&
                succeeded(
                    sources.add_initialization(
                        source)) &&
                succeeded(
                    sources.add_dependency(
                        identity)) &&
                succeeded(
                    sources.add_dependency(
                        object_identity)) &&
                succeeded(sources.end_root()),
            "source-save Header root ownership and endpoint object dependency");
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
                     writable_view.type_presence(0) == source_type_presence{2, 1},
                 "presence persists declarations separately from definitions");

    source_dependency_ref first_dependency;
    source_dependency_ref second_dependency;
    source_dependency_ref first_object_dependency;
    source_dependency_ref second_object_dependency;
    file_id first_dependent;
    file_id second_dependent;

    const auto type_dependency =
        source_dependency_ref::type(
            identity);

    const auto object_dependency =
        source_dependency_ref::object(
            object_identity);

    tests.expect(
        writable_view.semantic_dependency_count(first) == 2 &&
        writable_view.semantic_dependency_count(second) == 2 &&
        writable_view.semantic_dependency(
            first,
            0,
            first_dependency) &&
        writable_view.semantic_dependency(
            second,
            0,
            second_dependency) &&
        writable_view.semantic_dependency(
            first,
            1,
            first_object_dependency) &&
        writable_view.semantic_dependency(
            second,
            1,
            second_object_dependency) &&
        first_dependency == type_dependency &&
        second_dependency == type_dependency &&
        first_object_dependency == object_dependency &&
        second_object_dependency == object_dependency &&
        writable_view.semantic_dependent_count(
            type_dependency) == 2 &&
        writable_view.semantic_dependent_count(
            object_dependency) == 2 &&
        writable_view.semantic_dependent(
            object_dependency,
            0,
            first_dependent) &&
        writable_view.semantic_dependent(
            object_dependency,
            1,
            second_dependent) &&
        first_dependent == first &&
        second_dependent == second,
        "semantic dependency sidecars preserve initialization co-producer object closure");

    object_endpoint first_initialization;
    object_endpoint second_initialization;
    std::size_t first_initialization_count = 0;
    std::size_t second_initialization_count = 0;

    tests.expect(
        writable_view.initialization_targets(
            first,
            first_initialization_count) &&
        writable_view.initialization_targets(
            second,
            second_initialization_count) &&
        first_initialization_count == 1 &&
        second_initialization_count == 1 &&
        writable_view.initialization_target_count(
            first) == 1 &&
        writable_view.initialization_target_count(
            second) == 1 &&
        writable_view.initialization_target(
            first,
            0,
            first_initialization) &&
        writable_view.initialization_target(
            second,
            0,
            second_initialization) &&
        first_initialization == source &&
        second_initialization == source,
        "source.bin persists exact initialization producers per semantic root");

    tests.expect(
        writable_view.object_presence_count() == 1 &&
        writable_view.link_presence_count() == 1 &&
        writable_view.object_presence(0) == 2 &&
        writable_view.link_presence(0) == 0,
        "object initialization producers participate in object presence");
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

    {
        graph_delta changes;
        graph_dense_projection projection;
        source_map_delta replay;
        source_map_overlay_view source_candidate;
        assign_table assign_changes;
        assign_overlay_view assign_candidate;
        compiled_project_layout
            sparse_layout;

        tests.expect(
            succeeded(
                changes.bind_baseline(
                    compiled)) &&
            succeeded(
                projection.prepare(
                    changes)) &&
            succeeded(
                replay.reset()) &&
            succeeded(
                source_candidate.bind(
                    writable_view,
                    compiled,
                    replay,
                    {})) &&
            succeeded(
                assign_candidate.bind(
                    compiled,
                    assign_changes,
                    {})) &&
            prepare_build_compiled_project_layout(
                strings,
                identities,
                changes,
                projection,
                assign_candidate,
                files,
                source_candidate,
                sparse_layout) ==
                    compiled_project_image_result::
                        success &&
            sparse_layout.size() ==
                compiled_layout.size() &&
            projection.type_count() ==
                G.type_count() &&
            projection.member_count() ==
                G.member_count() &&
            projection.base_count() ==
                G.base_count() &&
            projection.object_count() ==
                G.object_count() &&
            projection.link_count() ==
                G.link_count() &&
            projection.derived_type_count() ==
                G.derived_type_count() &&
            projection.endpoint_path_count() ==
                G.endpoint_path_count() &&
            projection.endpoint_path_step_count() ==
                G.endpoint_path_step_count(),
            "sparse BUILD candidate prepares byte-identical final compiled.bin layout counts");

        std::vector<std::byte>
            sparse_bytes(
                sparse_layout.size(),
                std::byte{0xa5});

        compiled_project_view
            sparse_compiled;

        tests.expect(
            encode_build_compiled_project_image(
                strings,
                identities,
                changes,
                projection,
                assign_candidate,
                files,
                source_candidate,
                sparse_layout,
                sparse_bytes) ==
                    compiled_project_image_result::
                        success &&
            sparse_bytes ==
                compiled_bytes &&
            sparse_compiled.bind(
                sparse_bytes) ==
                    compiled_project_image_result::
                        success &&
            sparse_compiled.verify_contents() ==
                    compiled_project_image_result::
                        success,
            "unchanged sparse BUILD encodes byte-identical audited compiled.bin directly");

        source_save_layout
            sparse_source_layout;

        std::vector<std::byte>
            sparse_source_bytes;

        source_save_view
            sparse_source;

        const auto source_prepared =
            prepare_build_source_save_layout(
                files,
                identities,
                changes,
                projection,
                source_candidate,
                source_save_build_options{},
                sparse_source_layout);

        if (source_prepared ==
            source_save_result::success) {

            sparse_source_bytes.assign(
                sparse_source_layout.size(),
                std::byte{0xa5});
        }

        tests.expect(
            source_prepared ==
                    source_save_result::success &&
            sparse_source_layout.size() ==
                layout.size() &&
            encode_build_source_save_image(
                files,
                identities,
                changes,
                projection,
                source_candidate,
                sparse_source_layout,
                sparse_source_bytes) ==
                    source_save_result::success &&
            sparse_source_bytes.size() ==
                writable.bytes().size() &&
            std::equal(
                sparse_source_bytes.begin(),
                sparse_source_bytes.end(),
                writable.bytes().begin(),
                writable.bytes().end()) &&
            validate_source_save_image(
                sparse_source_bytes) ==
                    source_save_result::success &&
            sparse_source.bind(
                sparse_source_bytes) ==
                    source_save_result::success &&
            verify_source_save_presence(
                sparse_source,
                sparse_compiled) ==
                    source_save_result::success,
            "unchanged sparse BUILD encodes byte-identical audited source.bin v6 directly");
    }

    {
        source_map_delta replay;

        tests.expect(
            succeeded(
                replay.reset()) &&
            succeeded(
                replay.begin_root(
                    first)) &&
            succeeded(
                replay.add(
                    first,
                    source_data_ref::
                        type_declaration(
                            identity))) &&
            succeeded(
                replay.end_root()),
            "prepare sparse replay provenance replacement");

        const std::array<file_id, 1>
            invalidated{first};

        source_map_overlay_view overlay;

        std::size_t first_contributions = 0;
        std::size_t first_dependencies = 0;
        std::size_t first_initializations = 0;
        std::size_t second_contributions = 0;
        std::size_t second_dependencies = 0;
        std::size_t second_initializations = 0;

        source_contribution_record
            first_contribution;

        source_contribution_record
            second_contribution;

        source_dependency_ref
            second_dependency_value;

        object_endpoint
            second_initialization_value;

        tests.expect(
            succeeded(
                overlay.bind(
                    writable_view,
                    compiled,
                    replay,
                    invalidated)) &&
            overlay.invalidated_root_count() == 1 &&
            overlay.replayed_root_count() == 1 &&
            overlay.replaced_root_count() == 1 &&
            overlay.removed_root_count() == 0 &&
            overlay.added_root_count() == 0 &&
            overlay.contributions(
                first,
                first_contributions) &&
            first_contributions == 1 &&
            overlay.contribution(
                first,
                0,
                first_contribution) &&
            first_contribution.file == first &&
            first_contribution.data ==
                source_data_ref::
                    type_declaration(
                        identity) &&
            overlay.dependencies(
                first,
                first_dependencies) &&
            first_dependencies == 0 &&
            overlay.initialization_targets(
                first,
                first_initializations) &&
            first_initializations == 0,
            "replayed root fully replaces OLD provenance instead of unioning with it");

        tests.expect(
            overlay.contributions(
                second,
                second_contributions) &&
            second_contributions == 2 &&
            overlay.contribution(
                second,
                0,
                second_contribution) &&
            second_contribution.file == second &&
            overlay.dependencies(
                second,
                second_dependencies) &&
            second_dependencies == 2 &&
            overlay.dependency(
                second,
                0,
                second_dependency_value) &&
            second_dependency_value ==
                type_dependency &&
            overlay.initialization_targets(
                second,
                second_initializations) &&
            second_initializations == 1 &&
            overlay.initialization_target(
                second,
                0,
                second_initialization_value) &&
            second_initialization_value ==
                source,
            "unaffected root remains mmap-backed through merged source overlay");
    }

    {
        source_map_delta empty_replay;

        tests.expect(
            succeeded(
                empty_replay.reset()),
            "prepare empty sparse replay provenance");

        const std::array<file_id, 1>
            invalidated{first};

        source_map_overlay_view overlay;

        std::size_t first_contributions = 99;
        std::size_t first_dependencies = 99;
        std::size_t first_initializations = 99;
        std::size_t second_contributions = 0;

        tests.expect(
            succeeded(
                overlay.bind(
                    writable_view,
                    compiled,
                    empty_replay,
                    invalidated)) &&
            overlay.replayed_root_count() == 0 &&
            overlay.replaced_root_count() == 0 &&
            overlay.removed_root_count() == 1 &&
            overlay.contributions(
                first,
                first_contributions) &&
            first_contributions == 0 &&
            overlay.dependencies(
                first,
                first_dependencies) &&
            first_dependencies == 0 &&
            overlay.initialization_targets(
                first,
                first_initializations) &&
            first_initializations == 0 &&
            overlay.contributions(
                second,
                second_contributions) &&
            second_contributions == 2,
            "invalidated root without replay disappears while unrelated OLD provenance survives");
    }

    {
        source_map_delta added_replay;

        const file_id added_root{
            static_cast<std::uint32_t>(
                writable_view.file_count() + 1)};

        tests.expect(
            succeeded(
                added_replay.reset()) &&
            succeeded(
                added_replay.begin_root(
                    added_root)) &&
            succeeded(
                added_replay.add(
                    added_root,
                    source_data_ref::
                        type_declaration(
                            identity))) &&
            succeeded(
                added_replay.end_root()),
            "prepare newly added semantic root provenance");

        source_map_overlay_view overlay;

        std::size_t added_contributions = 0;
        source_contribution_record
            added_contribution;

        tests.expect(
            succeeded(
                overlay.bind(
                    writable_view,
                    compiled,
                    added_replay,
                    {})) &&
            overlay.invalidated_root_count() == 0 &&
            overlay.replayed_root_count() == 1 &&
            overlay.replaced_root_count() == 0 &&
            overlay.removed_root_count() == 0 &&
            overlay.added_root_count() == 1 &&
            overlay.contributions(
                added_root,
                added_contributions) &&
            added_contributions == 1 &&
            overlay.contribution(
                added_root,
                0,
                added_contribution) &&
            added_contribution.file ==
                added_root,
            "new semantic root exists only in sparse replay overlay");
    }

    {
        source_save_semantic_invalidation_plan
            plan;

        source_save_semantic_invalidation_metrics
            metrics;

        const std::array<file_id, 1>
            roots{first};

        tests.expect(
            succeeded(
                collect_source_save_semantic_invalidation(
                    writable_view,
                    compiled,
                    roots,
                    plan,
                    &metrics)) &&
            plan.retire_types.empty() &&
            plan.clear_type_definitions.size() == 1 &&
            plan.clear_type_definitions[0] == type &&
            plan.retire_objects.empty() &&
            plan.retire_links.empty() &&
            metrics.visited_roots == 1 &&
            metrics.producer_contributions == 2 &&
            metrics.touched_entities == 2,
            "sparse invalidation clears the last OLD definition while surviving declarations remain");
    }

    {
        source_save_semantic_invalidation_plan
            plan;

        source_save_semantic_invalidation_metrics
            metrics;

        const std::array<file_id, 2>
            roots{first, second};

        tests.expect(
            succeeded(
                collect_source_save_semantic_invalidation(
                    writable_view,
                    compiled,
                    roots,
                    plan,
                    &metrics)) &&
            plan.retire_types.size() == 1 &&
            plan.retire_types[0] == type &&
            plan.clear_type_definitions.empty() &&
            plan.retire_objects.size() == 1 &&
            plan.retire_objects[0] == object &&
            plan.retire_links.empty() &&
            metrics.visited_roots == 2 &&
            metrics.producer_contributions == 4 &&
            metrics.touched_entities == 2,
            "sparse invalidation retires semantic entities only when all OLD producers disappear");
    }

    const std::array<file_id, 1>
        semantic_seed{first};

    std::vector<file_id>
        semantic_closure;

    source_save_semantic_dependency_metrics
        semantic_metrics;

    tests.expect(
        succeeded(
            collect_source_save_semantic_dependency_closure(
                writable_view,
                compiled,
                semantic_seed,
                semantic_closure,
                &semantic_metrics)) &&
        semantic_closure.size() == 2 &&
        semantic_closure[0] == first &&
        semantic_closure[1] == second &&
        semantic_metrics.visited_roots == 2 &&
        semantic_metrics.semantic_entities == 4 &&
        semantic_metrics.dependency_edges == 8 &&
        semantic_metrics.visited_slots >=
            semantic_metrics.visited_roots,
        "semantic dependency closure expands invalidated roots sparsely");

    {
        const std::array<file_id, 1>
            latest_producer{second};

        std::vector<file_id>
            producer_closure;

        tests.expect(
            succeeded(
                collect_source_save_semantic_dependency_closure(
                    writable_view,
                    compiled,
                    latest_producer,
                    producer_closure)) &&
            producer_closure.size() == 2 &&
            producer_closure[0] == first &&
            producer_closure[1] == second,
            "invalidating latest initialization producer replays all object co-producers");

        graph_delta recovery;

        bool recovered_replaced = false;
        bool invalidated_all = true;

        tests.expect(
            succeeded(
                recovery.bind_baseline(
                    compiled)),
            "bind co-producer recovery Graph baseline");

        for (const auto root_value :
             producer_closure) {

            std::size_t target_count = 0;

            if (!writable_view.initialization_targets(
                    root_value,
                    target_count)) {

                invalidated_all = false;
                break;
            }

            for (std::size_t index = 0;
                 index < target_count;
                 ++index) {

                object_endpoint persisted_target;

                if (!writable_view.initialization_target(
                        root_value,
                        index,
                        persisted_target) ||
                    !succeeded(
                        recovery.invalidate_initialization(
                            persisted_target))) {

                    invalidated_all = false;
                    break;
                }
            }

            if (!invalidated_all) {
                break;
            }
        }

        object_initialization_record
            recovered_value;

        tests.expect(
            invalidated_all &&
            succeeded(
                recovery.add_initialization(
                    source,
                    construction_value::constant(
                        construction_kind::
                            signed_integer,
                        5),
                    recovered_replaced)) &&
            !recovered_replaced &&
            recovery.initialization(
                source,
                recovered_value) &&
            recovered_value.value ==
                construction_value::constant(
                    construction_kind::
                        signed_integer,
                    5),
            "replay of surviving co-producer restores previous initialization value");
    }

    auto altered = std::vector<std::byte>(writable.bytes().begin(), writable.bytes().end());
    const auto semantic_sidecar_bytes =
        sources.root_dependency_entries().size() * 8 +
        sources.dependency_entries().size() * 4 +
        sources.dependency_index_entries().size() * 12 +
        sources.dependent_root_entries().size() * 4;

    const auto initialization_sidecar_bytes =
        sources.root_initialization_entries().size() * 8 +
        sources.initialization_target_entries().size() * 8;

    altered[
        altered.size() -
        32 -
        initialization_sidecar_bytes -
        semantic_sidecar_bytes -
        16] = std::byte{1};
    source_save_view stale;
    tests.expect(stale.bind(altered) == source_save_result::success &&
                     verify_source_save_presence(stale, compiled) ==
                         source_save_result::invalid_image,
                 "cross-artifact validation rejects stale presence");

    const auto initialization_range_bytes =
        sources.root_initialization_entries().size() * 8;

    const auto initialization_target_bytes =
        sources.initialization_target_entries().size() * 8;

    const auto initialization_ranges_offset =
        writable.bytes().size() -
        32 -
        initialization_target_bytes -
        initialization_range_bytes;

    const auto initialization_targets_offset =
        initialization_ranges_offset +
        initialization_range_bytes;

    {
        auto corrupted =
            std::vector<std::byte>(
                writable.bytes().begin(),
                writable.bytes().end());

        // first root count: 1 -> impossible 3, while total target count is 2.
        corrupted[
            initialization_ranges_offset + 4] =
                std::byte{3};

        source_save_view corrupted_view;
        std::size_t corrupted_count = 0;

        tests.expect(
            corrupted_view.bind(
                corrupted) ==
                    source_save_result::success &&
            !corrupted_view.initialization_targets(
                first,
                corrupted_count) &&
            validate_source_save_image(
                corrupted) ==
                    source_save_result::invalid_image,
            "source.bin rejects corrupted initialization root range");
    }

    {
        auto corrupted =
            std::vector<std::byte>(
                writable.bytes().begin(),
                writable.bytes().end());

        // Persisted semantic object identity becomes invalid.
        corrupted[
            initialization_targets_offset] =
                std::byte{0};

        source_save_view corrupted_view;
        object_endpoint corrupted_target;

        tests.expect(
            corrupted_view.bind(
                corrupted) ==
                    source_save_result::success &&
            !corrupted_view.initialization_target(
                first,
                0,
                corrupted_target) &&
            validate_source_save_image(
                corrupted) ==
                    source_save_result::invalid_image,
            "source.bin rejects corrupted initialization endpoint");
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

    object_endpoint persisted_initialization;

    tests.expect(
        persisted_view.initialization_target(
            first,
            0,
            persisted_initialization) &&
        persisted_initialization ==
            source,
        "read-only source.bin exposes exact initialization producer target");

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
    std::vector<file_id> unrelated_files;

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

        unrelated_files.push_back(unrelated);
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

    for (const std::size_t count : {48u, 49u, 64u}) {
        std::vector<file_id> changed(
            unrelated_files.begin(), unrelated_files.begin() + count);
        std::vector<file_id> result;
        tests.expect(
            succeeded(collect_source_save_affected(
                persisted, changed, result)) &&
            result == changed,
            "sparse affected-file set grows beyond 48 entries without losing roots");
    }
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
