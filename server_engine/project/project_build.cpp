#include "project_build.hpp"

#include "project_lifecycle_context.hpp"
#include "project_configuration_manifest_store.hpp"
#include "persistence/project_artifact.hpp"
#include "persistence/source_save.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"
#include "../read_only_file_mapping.hpp"

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

    read_only_file_mapping
        manifest_mapping;

    const auto manifest_opened =
        manifest_mapping.open(
            layout.manifest);

    if (manifest_opened !=
        read_only_file_mapping_result::
            success) {

        return report_manifest_open(
            manifest_opened,
            layout.manifest,
            operation,
            diagnostics);
    }

    const auto manifest_decoded =
        decode_project_configuration_manifest(
            manifest_mapping.bytes(),
            context.manifest);

    if (manifest_decoded !=
        project_configuration_manifest_store_result::
            success) {

        diagnostics.emit(
            diagnostic(
                manifest_decoded ==
                        project_configuration_manifest_store_result::
                            io_failed
                    ? diagnostics::
                        project_manifest_io_failed
                    : diagnostics::
                        project_manifest_invalid,
                operation)
                .file(layout.manifest)
                .detail(
                    "Committed Project configuration manifest could not be decoded")
                .build());

        return manifest_decoded ==
                project_configuration_manifest_store_result::
                    io_failed
            ? server_status::io_error
            : server_status::
                project_artifact_invalid;
    }

    project_configuration_manifest_verification
        verification =
            project_configuration_manifest_verification::
                changed;

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
        project_configuration_manifest_verification::
            unchanged) {

        const auto committed_hash =
            context.manifest.
                configuration_hash;

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

        if (!(context.manifest.
                configuration_hash ==
            committed_hash)) {

            return report_build_incomplete(
                operation,
                diagnostics,
                "Project configuration identity changed; current File Context membership/topology must be reconciled before physical dirty analysis can reuse the committed SourceSave");
        }
    }

    read_only_file_mapping
        source_mapping;

    const auto source_opened =
        source_mapping.open(
            layout.source_save);

    if (source_opened !=
        read_only_file_mapping_result::
            success) {

        return report_source_open(
            source_opened,
            layout.source_save,
            operation,
            diagnostics);
    }

    source_save_view source;

    if (source.bind(
            source_mapping.bytes()) !=
        source_save_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_source_save_invalid,
                operation)
                .file(layout.source_save)
                .detail(
                    "Committed source.bin failed structural binding")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    std::vector<file_id> dirty;
    source_save_change_scan scan;

    const auto scanned =
        scan_source_save_changes(
            source,
            dirty,
            &scan);

    if (!succeeded(scanned)) {
        diagnostics.emit(
            diagnostic(
                scanned ==
                        server_status::io_error
                    ? diagnostics::
                        project_source_save_io_failed
                    : diagnostics::
                        project_source_save_invalid,
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
            source,
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

    std::string detail;

    try {
        detail =
            "Persisted SourceSave analysis complete: backend=" +
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
            "; sparse File Context mutation and affected frontend reconstruction are not implemented yet";
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
