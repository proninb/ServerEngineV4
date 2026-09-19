#include "project_build.hpp"

#include "project_configuration_manifest.hpp"
#include "project_configuration_manifest_store.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

namespace cw::server {
namespace {

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

}

server_status build_project(
    const project& resident,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    (void)output;

    const auto& project_path =
        resident.path();

    project_configuration_manifest_store store{
        project_path};

    project_configuration_manifest persisted;

    const auto stored =
        store.load(
            persisted);

    if (stored ==
        project_configuration_manifest_store_result::
            not_found) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_manifest_missing,
                operation)
                .file(store.path())
                .detail(
                    "BUILD requires the configuration manifest committed by the resident generation")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    if (stored ==
        project_configuration_manifest_store_result::
            invalid) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_manifest_invalid,
                operation)
                .file(store.path())
                .detail(
                    "Persisted Project configuration manifest failed structural or checksum validation")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    if (stored ==
        project_configuration_manifest_store_result::
            io_failed) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_manifest_io_failed,
                operation)
                .file(store.path())
                .detail(
                    "Cannot read persisted Project configuration manifest")
                .build());

        return server_status::io_error;
    }

    project_configuration_manifest_verification verification =
        project_configuration_manifest_verification::
            changed;

    const auto verified =
        verify_project_configuration_manifest(
            project_path,
            persisted,
            operation,
            diagnostics,
            verification);

    if (!succeeded(verified)) {
        return verified;
    }

    if (verification ==
        project_configuration_manifest_verification::
            unchanged) {

        return report_build_incomplete(
            operation,
            diagnostics,
            "Complete Project configuration manifest is unchanged; File Context change detection is not implemented yet");
    }

    project_configuration_manifest candidate;

    const auto composed =
        compose_project_configuration_manifest(
            project_path,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(composed)) {
        return composed;
    }

    if (candidate.configuration_hash ==
        persisted.configuration_hash) {

        return report_build_incomplete(
            operation,
            diagnostics,
            "Project configuration inputs recomposed to the same aggregate hash; File Context change detection is not implemented yet");
    }

    return report_build_incomplete(
        operation,
        diagnostics,
        "Project configuration aggregate hash changed; Gn -> Gn+1 construction is not implemented yet");
}

}
