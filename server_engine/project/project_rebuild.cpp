#include "project_rebuild.hpp"

#include "project_configuration_loader.hpp"
#include "project_identity.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

namespace cw::server {

server_status rebuild_project(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    (void)output;

    project_content_snapshot snapshot;

    const auto acquired =
        acquire_project_content(
            project_path,
            snapshot);

    if (acquired != project_snapshot_result::acquired) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_load_failed,
                operation)
                .file(project_path)
                .detail(
                    acquired == project_snapshot_result::missing
                        ? "Project configuration file does not exist"
                        : acquired == project_snapshot_result::changed_during_read
                            ? "Project configuration changed during acquisition"
                            : "Cannot acquire Project configuration snapshot")
                .build());

        return server_status::project_load_failed;
    }

    const auto status =
        validate_project_configuration(
            snapshot.bytes,
            project_path,
            operation,
            diagnostics);

    if (!succeeded(status)) {
        return status;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::project_rebuild_incomplete,
            operation)
            .detail(
                "REBUILD Project composition and Source Manager are not "
                "implemented yet")
            .build());

    return server_status::unsupported;
}

}
