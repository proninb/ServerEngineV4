#include "project_build.hpp"

#include "project_lifecycle_context.hpp"
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
    const server_abi_configuration& abi,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    (void)output;

    build_context context{abi};

    const auto& project_path =
        resident.path();

    project_configuration_manifest_store store{
        project_path};

    const auto stored =
        store.load(
            context.manifest);

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
            context.manifest,
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

    const auto previous_configuration_hash =
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

    if (context.manifest.configuration_hash ==
        previous_configuration_hash) {

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
