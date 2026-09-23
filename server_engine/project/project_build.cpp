#include "project_build.hpp"

#include "project_lifecycle_context.hpp"
#include "project_configuration_manifest_store.hpp"
#include "persistence/project_artifact.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <string>
#include <string_view>
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

    if (verification !=
        project_configuration_manifest_verification::unchanged) {

        const auto committed_hash =
            context.manifest.configuration_hash;

        const auto composed =
            compose_project_configuration_manifest(
                project_path,
                operation,
                diagnostics,
                context.manifest,
                context.preprocessor);

        if (!succeeded(composed)) {
            return composed;
        }

        if (!(context.manifest.configuration_hash ==
              committed_hash)) {

            return report_build_incomplete(
                operation,
                diagnostics,
                "Project configuration identity changed; current File Context membership/topology must be reconciled before physical dirty analysis can reuse the committed SourceSave");
        }
    }

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

    std::vector<file_id> dirty;
    source_save_change_scan scan;

    const auto scanned =
        scan_source_save_changes(
            context.source,
            dirty,
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
                    "Physical change detection over committed SourceSave failed")
                .build());

        return scanned;
    }

    std::vector<file_id> affected;

    const auto collected =
        collect_source_save_affected(
            context.source,
            dirty,
            affected);

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

    if (!affected.empty()) {
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

        database_bound = true;
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
            ", files_read=" +
            std::to_string(
                scan.metrics.files_read) +
            ", bytes_read=" +
            std::to_string(
                scan.metrics.bytes_read) +
            ", dirty=" +
            std::to_string(
                dirty.size()) +
            ", next_checkpoint=" +
            std::to_string(
                scan.next_checkpoint ? 1 : 0) +
            ", affected=" +
            std::to_string(
                affected.size()) +
            ", database_mapped=" +
            std::to_string(
                database_bound ? 1 : 0) +
            ", baseline_strings=" +
            std::to_string(
                context.compiled.string_count()) +
            ", baseline_identities=" +
            std::to_string(
                context.compiled.identity_count()) +
            "; sparse File Context mutation, lexical replacement/reuse, affected frontend/Parser/Semantic reconstruction, and final G construction are not implemented yet";
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
