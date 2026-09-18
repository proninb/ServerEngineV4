#include "project_build.hpp"

#include "project_configuration_loader.hpp"
#include "project_identity.hpp"
#include "project_identity_store.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

namespace cw::server {
namespace {

[[nodiscard]] server_status report_snapshot_failure(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_snapshot_result result) {

    diagnostics.emit(
        diagnostic(
            diagnostics::project_load_failed,
            operation)
            .file(project_path)
            .detail(
                result == project_snapshot_result::missing
                    ? "Project configuration file does not exist"
                    : result == project_snapshot_result::changed_during_read
                        ? "Project configuration changed during acquisition"
                        : "Cannot acquire Project configuration snapshot")
            .build());

    return server_status::project_load_failed;
}

[[nodiscard]] server_status report_build_incomplete(
    operation_id operation,
    diagnostic_collection& diagnostics,
    const char* detail) {

    diagnostics.emit(
        diagnostic(
            diagnostics::project_build_incomplete,
            operation)
            .detail(detail)
            .build());

    return server_status::unsupported;
}

} // namespace

server_status build_project(
    const project& resident,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    (void)output;

    const auto& project_path =
        resident.path();

    project_identity_store store{
        project_path};

    persisted_project_identity persisted;

    const auto stored =
        store.load(
            persisted);

    if (stored ==
        project_identity_store_result::invalid) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_identity_invalid,
                operation)
                .file(store.path())
                .detail(
                    "Persisted Project identity failed format or checksum validation")
                .build());

        return
            server_status::
                project_artifact_invalid;
    }

    if (stored ==
        project_identity_store_result::io_failed) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_identity_io_failed,
                operation)
                .file(store.path())
                .detail(
                    "Cannot read persisted Project identity")
                .build());

        return server_status::io_error;
    }

    if (stored ==
        project_identity_store_result::not_found) {

        project_content_snapshot snapshot;

        const auto acquired =
            acquire_project_content(
                project_path,
                snapshot);

        if (acquired !=
            project_snapshot_result::acquired) {

            return report_snapshot_failure(
                project_path,
                operation,
                diagnostics,
                acquired);
        }

        const auto validation =
            validate_project_configuration(
                snapshot.bytes,
                project_path,
                operation,
                diagnostics);

        if (!succeeded(validation)) {
            return validation;
        }

        return report_build_incomplete(
            operation,
            diagnostics,
            "No persisted Project identity exists; composition and initial construction are not implemented yet");
    }

    project_identity_decision decision =
        project_identity_decision::
            semantic_check_required;

    project_content_snapshot snapshot;

    const auto acquired =
        decide_project_identity(
            project_path,
            persisted,
            decision,
            snapshot);

    if (acquired !=
        project_snapshot_result::acquired) {

        return report_snapshot_failure(
            project_path,
            operation,
            diagnostics,
            acquired);
    }

    switch (decision) {
    case project_identity_decision::proven_unchanged:
        return report_build_incomplete(
            operation,
            diagnostics,
            "Project configuration is proven unchanged; Source Manager change detection is not implemented yet");

    case project_identity_decision::content_unchanged:
        return report_build_incomplete(
            operation,
            diagnostics,
            "Project content hash is unchanged; Source Manager change detection is not implemented yet");

    case project_identity_decision::semantic_check_required:
        break;
    }

    const auto validation =
        validate_project_configuration(
            snapshot.bytes,
            project_path,
            operation,
            diagnostics);

    if (!succeeded(validation)) {
        return validation;
    }

    return report_build_incomplete(
        operation,
        diagnostics,
        "Project content changed; composition and semantic fingerprint construction are not implemented yet");
}

}
