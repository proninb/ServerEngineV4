#include "project_build.hpp"

#include "project_lifecycle_context.hpp"
#include "project_path.hpp"
#include "project_configuration_loader.hpp"
#include "construction/execution_lanes.hpp"
#include "frontend/source_discovery.hpp"
#include "project_configuration_manifest_store.hpp"
#include "persistence/project_artifact.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

[[nodiscard]] server_status report_build_incomplete(
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::string_view detail) {

    diagnostics.emit(
        diagnostic(
            diagnostics::project_build_incomplete,
            operation)
            .detail(detail)
            .build());

    return server_status::unsupported;
}

[[nodiscard]] server_status report_manifest_open(
    read_only_file_mapping_result result,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result == read_only_file_mapping_result::failed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_manifest_io_failed,
                operation)
                .file(path)
                .detail("Cannot memory-map project.manifest")
                .build());

        return server_status::io_error;
    }

    diagnostics.emit(
        diagnostic(
            result == read_only_file_mapping_result::not_found
                ? diagnostics::project_manifest_missing
                : diagnostics::project_manifest_invalid,
            operation)
            .file(path)
            .detail(
                result == read_only_file_mapping_result::not_found
                    ? "BUILD requires project.manifest; REBUILD is required when persisted BUILD state is missing"
                    : "Persisted project.manifest is empty")
            .build());

    return server_status::project_artifact_invalid;
}

[[nodiscard]] server_status report_source_open(
    read_only_file_mapping_result result,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result == read_only_file_mapping_result::failed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_io_failed,
                operation)
                .file(path)
                .detail("Cannot memory-map source.bin")
                .build());

        return server_status::io_error;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::project_source_save_invalid,
            operation)
            .file(path)
            .detail(
                result == read_only_file_mapping_result::not_found
                    ? "BUILD requires source.bin; REBUILD is required when persisted BUILD state is missing"
                    : "Persisted source.bin is empty")
            .build());

    return server_status::project_artifact_invalid;
}

[[nodiscard]] server_status report_database_open(
    read_only_file_mapping_result result,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result == read_only_file_mapping_result::failed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_database_io_failed,
                operation)
                .file(path)
                .detail("Cannot memory-map database.bin")
                .build());

        return server_status::io_error;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::project_database_invalid,
            operation)
            .file(path)
            .detail(
                result == read_only_file_mapping_result::not_found
                    ? "BUILD requires database.bin when affected files need retained lexical state; REBUILD is required when it is missing"
                    : "Persisted database.bin is empty")
            .build());

    return server_status::project_artifact_invalid;
}

[[nodiscard]] server_status report_compiled_open(
    read_only_file_mapping_result result,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result == read_only_file_mapping_result::failed) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_io_failed,
                operation)
                .file(path)
                .detail("Cannot memory-map compiled.bin for BUILD baseline")
                .build());

        return server_status::io_error;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::project_compiled_invalid,
            operation)
            .file(path)
            .detail(
                result == read_only_file_mapping_result::not_found
                    ? "BUILD requires compiled.bin as string/identity/G baseline; REBUILD is required when it is missing"
                    : "Persisted compiled.bin is empty")
            .build());

    return server_status::project_artifact_invalid;
}

[[nodiscard]] constexpr bool file_id_less(
    file_id left,
    file_id right) noexcept {

    return left.value() <
        right.value();
}

void normalize_file_ids(
    std::vector<file_id>& values) {

    std::sort(
        values.begin(),
        values.end(),
        file_id_less);

    values.erase(
        std::unique(
            values.begin(),
            values.end()),
        values.end());
}

[[nodiscard]] bool contains_file_id(
    const std::vector<file_id>& values,
    file_id value) noexcept {

    return std::binary_search(
        values.begin(),
        values.end(),
        value,
        file_id_less);
}

void append_file_id_difference(
    const std::vector<file_id>& left,
    const std::vector<file_id>& right,
    std::vector<file_id>& output) {

    std::size_t left_index = 0;
    std::size_t right_index = 0;

    while (left_index < left.size()) {
        while (right_index < right.size() &&
               right[right_index].value() <
                   left[left_index].value()) {

            ++right_index;
        }

        if (right_index >= right.size() ||
            left[left_index] !=
                right[right_index]) {

            output.push_back(
                left[left_index]);
        }

        ++left_index;
    }
}

[[nodiscard]] server_status collect_persisted_configuration_roots(
    const std::filesystem::path& root_project_path,
    const project_configuration_manifest& manifest,
    const source_save_view& source,
    std::vector<file_id>& roots) noexcept {

    roots.clear();

    if (!source.valid() ||
        manifest.files.empty()) {

        return server_status::
            project_artifact_invalid;
    }

    std::filesystem::path root;

    if (resolve_project_path(
            root_project_path,
            root) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    try {
        std::vector<std::filesystem::path>
            resolved_paths;

        resolved_paths.reserve(
            manifest.files.size());

        for (std::size_t index = 0;
             index < manifest.files.size();
             ++index) {

            const auto& proof =
                manifest.files[index];

            std::filesystem::path path;

            if (index == 0) {
                if (proof.declaring_file !=
                        invalid_configuration_file ||
                    proof.path_type !=
                        project_configuration_path_type::relative ||
                    proof.path !=
                        root.filename()) {

                    return server_status::
                        project_artifact_invalid;
                }

                path = root;
            } else {
                if (proof.declaring_file >=
                    index) {

                    return server_status::
                        project_artifact_invalid;
                }

                const auto& parent =
                    resolved_paths[
                        proof.declaring_file];

                const auto input =
                    proof.path_type ==
                        project_configuration_path_type::absolute
                    ? proof.path
                    : parent.parent_path() /
                        proof.path;

                if (resolve_project_path(
                        input,
                        path) !=
                    project_path_result::success) {

                    return server_status::io_error;
                }
            }

            resolved_paths.push_back(
                path);

            file_id project_file;

            const auto found =
                source.find_path(
                    path,
                    project_file);

            if (!succeeded(found)) {
                return found;
            }

            source_save_file_view
                project_state;

            if (!project_file ||
                !source.file(
                    project_file,
                    project_state) ||
                !project_state.current_member ||
                project_state.kind !=
                    file_kind::project) {

                return server_status::
                    project_artifact_invalid;
            }

            for (const auto dependency :
                 project_state.dependencies) {

                source_save_file_view
                    dependency_state;

                if (!source.file(
                        dependency,
                        dependency_state) ||
                    !dependency_state.current_member) {

                    return server_status::
                        project_artifact_invalid;
                }

                if (dependency_state.kind ==
                        file_kind::header ||
                    dependency_state.kind ==
                        file_kind::source) {

                    roots.push_back(
                        dependency);
                }
            }
        }

        normalize_file_ids(
            roots);

        return server_status::success;
    }
    catch (...) {
        roots.clear();
        return server_status::io_error;
    }
}

[[nodiscard]] server_status load_root_preprocessor_configuration(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    preprocessor_configuration& output) {

    output.predefines.clear();

    std::filesystem::path root;

    if (resolve_project_path(
            project_path,
            root) !=
        project_path_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_configuration_read_failed,
                operation)
                .file(project_path)
                .detail(
                    "Cannot resolve root Project configuration path for BUILD semantic replay")
                .build());

        return server_status::io_error;
    }

    file_content_snapshot snapshot;

    const auto acquired =
        acquire_file_content(
            root,
            snapshot);

    if (acquired !=
        file_content_result::acquired) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_configuration_read_failed,
                operation)
                .file(root)
                .detail(
                    acquired ==
                            file_content_result::missing
                        ? "Root Project configuration is missing during BUILD semantic replay"
                        : acquired ==
                                file_content_result::changed_during_read
                            ? "Root Project configuration changed during BUILD semantic replay acquisition"
                            : acquired ==
                                    file_content_result::allocation_failed
                                ? "Cannot allocate root Project configuration snapshot for BUILD semantic replay"
                                : "Cannot acquire root Project configuration for BUILD semantic replay")
                .build());

        return acquired ==
                file_content_result::allocation_failed
            ? server_status::io_error
            : server_status::
                project_configuration_invalid;
    }

    std::vector<project_configuration_dependency>
        dependencies;

    return read_project_configuration(
        snapshot.bytes,
        root,
        operation,
        diagnostics,
        dependencies,
        project_configuration_scope::root,
        &output);
}


}

server_status build_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    output.reset();

    build_context context{
        settings};

    project_artifact_layout layout;

    if (make_project_artifact_layout(
            project_path,
            context.settings.files,
            layout) != project_artifact_layout_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_build_incomplete,
                operation)
                .file(project_path)
                .detail("Cannot construct Project artifact layout")
                .build());

        return server_status::io_error;
    }

    const auto manifest_opened =
        context.manifest_mapping.open(
            layout.manifest);

    if (manifest_opened !=
        read_only_file_mapping_result::success) {

        return report_manifest_open(
            manifest_opened,
            layout.manifest,
            operation,
            diagnostics);
    }

    const auto manifest_decoded =
        decode_project_configuration_manifest(
            context.manifest_mapping.bytes(),
            context.manifest);

    if (manifest_decoded !=
        project_configuration_manifest_store_result::success) {

        diagnostics.emit(
            diagnostic(
                manifest_decoded ==
                        project_configuration_manifest_store_result::io_failed
                    ? diagnostics::project_manifest_io_failed
                    : diagnostics::project_manifest_invalid,
                operation)
                .file(layout.manifest)
                .detail(
                    "Committed Project configuration manifest could not be decoded")
                .build());

        return manifest_decoded ==
                project_configuration_manifest_store_result::io_failed
            ? server_status::io_error
            : server_status::project_artifact_invalid;
    }

    project_configuration_manifest_verification verification =
        project_configuration_manifest_verification::changed;

    const auto verified =
        verify_project_configuration_manifest(
            project_path,
            context.manifest,
            operation,
            diagnostics,
            verification);

    if (!succeeded(verified)) {
        return verified;
    }

    const bool configuration_probe_changed =
        verification !=
            project_configuration_manifest_verification::
                unchanged;

    const auto source_opened =
        context.source_mapping.open(
            layout.source_save);

    if (source_opened !=
        read_only_file_mapping_result::success) {

        return report_source_open(
            source_opened,
            layout.source_save,
            operation,
            diagnostics);
    }

    if (context.source.bind(
            context.source_mapping.bytes()) !=
        source_save_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Committed source.bin failed structural binding")
                .build());

        return server_status::project_artifact_invalid;
    }

    const auto files_bound =
        context.files.bind_baseline(
            context.source);

    if (!succeeded(files_bound)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Committed source.bin could not initialize mmap-backed BUILD File Context")
                .build());

        return files_bound;
    }

    project_configuration_manifest
        committed_manifest;

    std::vector<file_id>
        old_configuration_roots;

    std::vector<file_id>
        current_configuration_roots;

    bool configuration_identity_changed = false;
    bool preprocessor_changed = false;
    bool preprocessor_loaded = false;

    if (configuration_probe_changed) {
        committed_manifest =
            std::move(
                context.manifest);

        const auto composed =
            compose_project_configuration(
                project_path,
                operation,
                diagnostics,
                context.manifest,
                context.files,
                context.preprocessor,
                &current_configuration_roots);

        if (!succeeded(composed)) {
            return composed;
        }

        preprocessor_loaded = true;

        configuration_identity_changed =
            !(context.manifest.configuration_hash ==
              committed_manifest.configuration_hash);

        if (configuration_identity_changed) {
            const auto old_roots_collected =
                collect_persisted_configuration_roots(
                    project_path,
                    committed_manifest,
                    context.source,
                    old_configuration_roots);

            if (!succeeded(
                    old_roots_collected)) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::project_source_save_invalid,
                        operation)
                        .file(layout.source_save)
                        .detail(
                            "Committed Project composition could not recover OLD semantic roots from SourceSave")
                        .build());

                return old_roots_collected;
            }

            preprocessor_changed =
                !(context.manifest.preprocessor_hash ==
                  committed_manifest.preprocessor_hash);
        }
    }

    std::vector<file_id> candidates;
    source_save_change_scan scan;

    const auto scanned =
        scan_source_save_change_candidates(
            context.source,
            candidates,
            &scan);

    if (!succeeded(scanned)) {
        diagnostics.emit(
            diagnostic(
                scanned == server_status::io_error
                    ? diagnostics::project_source_save_io_failed
                    : diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Physical change-candidate discovery over committed SourceSave failed")
                .build());

        return scanned;
    }

    std::vector<file_id> semantic_changed;
    source_save_change_classification_metrics
        classification;

    const auto classified =
        classify_source_save_changes(
            context.source,
            candidates,
            context.files,
            semantic_changed,
            &classification);

    if (!succeeded(classified)) {
        diagnostics.emit(
            diagnostic(
                classified == server_status::io_error
                    ? diagnostics::project_source_save_io_failed
                    : diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Exact SourceSave candidate acquisition/classification failed")
                .build());

        return classified;
    }

    std::vector<file_id> affected;

    source_save_affected_metrics
        affected_metrics;

    const auto collected =
        collect_source_save_affected(
            context.source,
            semantic_changed,
            affected,
            &affected_metrics);

    if (!succeeded(collected)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "OLD reverse dependency topology failed while computing affected closure")
                .build());

        return collected;
    }

    std::vector<file_id>
        affected_semantic_roots;

    const auto roots_collected =
        collect_source_save_semantic_roots(
            context.source,
            affected,
            affected_semantic_roots);

    if (!succeeded(roots_collected)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "OLD physical dependency topology failed while selecting affected semantic roots")
                .build());

        return roots_collected;
    }

    std::vector<file_id>
        semantic_invalidated_roots;

    std::vector<file_id>
        semantic_replay_roots;

    try {
        semantic_invalidated_roots =
            affected_semantic_roots;

        if (!configuration_identity_changed) {
            semantic_replay_roots =
                affected_semantic_roots;
        } else {
            semantic_replay_roots.reserve(
                affected_semantic_roots.size() +
                current_configuration_roots.size());

            for (const auto root :
                 affected_semantic_roots) {

                if (contains_file_id(
                        current_configuration_roots,
                        root)) {

                    semantic_replay_roots.push_back(
                        root);
                }
            }

            append_file_id_difference(
                old_configuration_roots,
                current_configuration_roots,
                semantic_invalidated_roots);

            append_file_id_difference(
                current_configuration_roots,
                old_configuration_roots,
                semantic_replay_roots);

            if (preprocessor_changed) {
                semantic_invalidated_roots.insert(
                    semantic_invalidated_roots.end(),
                    old_configuration_roots.begin(),
                    old_configuration_roots.end());

                semantic_replay_roots.insert(
                    semantic_replay_roots.end(),
                    current_configuration_roots.begin(),
                    current_configuration_roots.end());
            }

            normalize_file_ids(
                semantic_invalidated_roots);

            normalize_file_ids(
                semantic_replay_roots);
        }
    }
    catch (...) {
        return server_status::io_error;
    }

    std::vector<file_id>
        lexical_replacement_files;

    try {
        lexical_replacement_files =
            semantic_changed;

        if (configuration_identity_changed) {
            for (const auto root :
                 current_configuration_roots) {

                if (root.value() >
                    context.source.file_count()) {

                    lexical_replacement_files.push_back(
                        root);
                }
            }
        }

        normalize_file_ids(
            lexical_replacement_files);
    }
    catch (...) {
        return server_status::io_error;
    }

    if (!semantic_replay_roots.empty() &&
        !preprocessor_loaded) {

        const auto preprocessor_loaded_status =
            load_root_preprocessor_configuration(
                project_path,
                operation,
                diagnostics,
                context.preprocessor);

        if (!succeeded(
                preprocessor_loaded_status)) {

            return preprocessor_loaded_status;
        }

        preprocessor_loaded = true;
    }

    const auto compiled_opened =
        context.compiled_mapping.open(
            layout.compiled);

    if (compiled_opened !=
        read_only_file_mapping_result::success) {

        return report_compiled_open(
            compiled_opened,
            layout.compiled,
            operation,
            diagnostics);
    }

    if (context.compiled.bind(
            context.compiled_mapping.bytes()) !=
        compiled_project_image_result::success ||
        context.compiled.source_file_count() != context.source.file_count() ||
        context.compiled.type_count() != context.source.type_presence_count() ||
        context.compiled.object_count() != context.source.object_presence_count() ||
        context.compiled.link_count() != context.source.link_presence_count()) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Committed compiled.bin failed structural BUILD-baseline binding or SourceSave presence cardinalities disagree")
                .build());

        return server_status::project_artifact_invalid;
    }

    std::vector<file_id>
        semantic_dependency_roots;

    source_save_semantic_dependency_metrics
        semantic_dependency_metrics;

    const auto semantic_dependency_collected =
        collect_source_save_semantic_dependency_closure(
            context.source,
            context.compiled,
            semantic_invalidated_roots,
            semantic_dependency_roots,
            &semantic_dependency_metrics);

    if (!succeeded(
            semantic_dependency_collected)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Persisted semantic dependency topology failed while expanding OLD invalidated roots")
                .build());

        return semantic_dependency_collected;
    }

    semantic_invalidated_roots =
        semantic_dependency_roots;

    try {
        for (const auto root :
             semantic_dependency_roots) {

            if (!configuration_identity_changed ||
                contains_file_id(
                    current_configuration_roots,
                    root)) {

                semantic_replay_roots.push_back(
                    root);
            }
        }

        normalize_file_ids(
            semantic_replay_roots);
    }
    catch (...) {
        return server_status::io_error;
    }

    if (!semantic_replay_roots.empty() &&
        !preprocessor_loaded) {

        const auto preprocessor_loaded_status =
            load_root_preprocessor_configuration(
                project_path,
                operation,
                diagnostics,
                context.preprocessor);

        if (!succeeded(
                preprocessor_loaded_status)) {

            return preprocessor_loaded_status;
        }

        preprocessor_loaded = true;
    }

    if (!succeeded(
            context.strings.bind_baseline(
                context.compiled)) ||
        !succeeded(
            context.identities.bind_baseline(
                context.compiled))) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Committed compiled.bin could not initialize append-only BUILD string/identity overlays")
                .build());

        return server_status::project_artifact_invalid;
    }

    bool database_bound = false;

    if (!semantic_replay_roots.empty()) {
        const auto database_opened =
            context.database_mapping.open(
                layout.database);

        if (database_opened !=
            read_only_file_mapping_result::success) {

            return report_database_open(
                database_opened,
                layout.database,
                operation,
                diagnostics);
        }

        if (context.database.bind(
                context.database_mapping.bytes()) !=
                database_image_result::success ||
            context.database.file_count() !=
                context.source.file_count()) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::project_database_invalid,
                    operation)
                    .file(layout.database)
                    .detail(
                        "Committed database.bin failed structural binding or does not match SourceSave file_id cardinality")
                    .build());

            return server_status::project_artifact_invalid;
        }

        const auto content_bound =
            context.files.bind_content_baseline(
                context.database.content_baseline());

        if (!succeeded(content_bound)) {
            diagnostics.emit(
                diagnostic(
                    diagnostics::project_database_invalid,
                    operation)
                    .file(layout.database)
                    .detail(
                        "Committed database.bin could not initialize mmap-backed BUILD source-content baseline")
                    .build());

            return content_bound;
        }

        const auto lexical_bound =
            context.lexical.bind_baseline(
                context.database.lexical_baseline(),
                execution_lane_capacity());

        if (!succeeded(lexical_bound) ||
            context.lexical.size() != context.database.file_count()) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::project_database_invalid,
                    operation)
                    .file(layout.database)
                    .detail(
                        "Committed database.bin could not initialize mmap-backed BUILD lexical baseline")
                    .build());

            return succeeded(lexical_bound)
                ? server_status::project_artifact_invalid
                : lexical_bound;
        }

        database_bound = true;
    }

    source_preparation_failure lexical_failure;
    source_replacement_metrics lexical_metrics;

    if (!semantic_replay_roots.empty()) {
        if (!database_bound) {
            return server_status::project_artifact_invalid;
        }

        const auto replaced = replace_source_lexical_state(
            context.files,
            context.lexical,
            lexical_replacement_files,
            &lexical_failure,
            &lexical_metrics);

        if (!succeeded(replaced)) {
            if (lexical_failure.kind == source_preparation_failure_kind::lexical &&
                lexical_failure.file && context.files.contains(lexical_failure.file)) {
                try {
                    const auto path_view = context.files.path(lexical_failure.file);
                    diagnostics.emit(
                        diagnostic(diagnostics::project_lexical_error, operation)
                            .file(std::filesystem::path{path_view.begin(), path_view.end()})
                            .detail(lexical_error_message(lexical_failure.lexical.reason))
                            .build());
                }
                catch (...) {
                    return server_status::io_error;
                }
            }
            return replaced;
        }
    }

    std::string detail;

    try {
        detail =
            "Persisted BUILD baseline ready: backend=" +
            std::to_string(
                static_cast<std::uint32_t>(
                    scan.metrics.backend)) +
            ", journal_records=" +
            std::to_string(
                scan.metrics.journal_records) +
            ", matched_files=" +
            std::to_string(
                scan.metrics.matched_files) +
            ", fallback=" +
            std::to_string(
                scan.metrics.fallback ? 1 : 0) +
            ", candidates=" +
            std::to_string(
                candidates.size()) +
            ", files_read=" +
            std::to_string(
                classification.files_read) +
            ", bytes_read=" +
            std::to_string(
                classification.bytes_read) +
            ", missing=" +
            std::to_string(
                classification.missing_files) +
            ", semantic_changed=" +
            std::to_string(
                semantic_changed.size()) +
            ", classify_lanes=" +
            std::to_string(
                classification.active_lanes) +
            ", next_checkpoint=" +
            std::to_string(
                scan.next_checkpoint ? 1 : 0) +
            ", affected=" +
            std::to_string(
                affected.size()) +
            ", affected_edges=" +
            std::to_string(
                affected_metrics.dependency_edges) +
            ", affected_slots=" +
            std::to_string(
                affected_metrics.visited_slots) +
            ", affected_semantic_roots=" +
            std::to_string(
                affected_semantic_roots.size()) +
            ", configuration_changed=" +
            std::to_string(
                configuration_identity_changed
                    ? 1
                    : 0) +
            ", preprocessor_changed=" +
            std::to_string(
                preprocessor_changed
                    ? 1
                    : 0) +
            ", semantic_invalidated_roots=" +
            std::to_string(
                semantic_invalidated_roots.size()) +
            ", semantic_replay_roots=" +
            std::to_string(
                semantic_replay_roots.size()) +
            ", semantic_dependency_roots=" +
            std::to_string(
                semantic_dependency_metrics.
                    visited_roots) +
            ", semantic_dependency_entities=" +
            std::to_string(
                semantic_dependency_metrics.
                    semantic_entities) +
            ", semantic_dependency_edges=" +
            std::to_string(
                semantic_dependency_metrics.
                    dependency_edges) +
            ", semantic_dependency_slots=" +
            std::to_string(
                semantic_dependency_metrics.
                    visited_slots) +
            ", database_mapped=" +
            std::to_string(
                database_bound ? 1 : 0) +
            ", lexical_baseline=" +
            std::to_string(
                context.lexical.baseline_bound()
                    ? context.lexical.size()
                    : 0) +
            ", lexical_replacement_files=" +
            std::to_string(
                lexical_replacement_files.size()) +
            ", lexical_masked=" + std::to_string(lexical_metrics.masked_files) +
            ", lexical_retokenized=" + std::to_string(lexical_metrics.retokenized_files) +
            ", lexical_missing=" + std::to_string(lexical_metrics.missing_files) +
            ", lexical_lanes=" + std::to_string(lexical_metrics.active_lanes) +
            ", baseline_strings=" +
            std::to_string(
                context.compiled.string_count()) +
            ", baseline_identities=" +
            std::to_string(
                context.compiled.identity_count()) +
            "; selected semantic-root Parser/Semantic replay and final G construction are not implemented yet";
    }
    catch (...) {
        return server_status::io_error;
    }

    return report_build_incomplete(
        operation,
        diagnostics,
        detail);
}

}
