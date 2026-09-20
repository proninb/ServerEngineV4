#include "project_rebuild.hpp"

#include "project_configuration_manifest.hpp"
#include "file/file_context.hpp"
#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

namespace cw::server {

server_status rebuild_project(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    (void)output;

    project_configuration_manifest candidate;
    file_context files;

    const auto composed =
        compose_project_configuration(
            project_path,
            operation,
            diagnostics,
            candidate,
            files);

    if (!succeeded(composed)) {
        return composed;
    }

    // The candidate manifest belongs to candidate G0. Persist it only as part
    // of the eventual coordinated successful REBUILD commit.

    diagnostics.emit(
        diagnostic(
            diagnostics::project_rebuild_incomplete,
            operation)
            .detail(
                "Project configuration manifest and flat File Context are complete; Project-declared dependencies are staged, while Header/Source/Assign dependency discovery, topology finalization, and Graph construction are not implemented yet")
            .build());

    return server_status::unsupported;
}

}
