#include "project_rebuild.hpp"

#include "project_configuration_manifest.hpp"
#include "file/file_context.hpp"
#include "frontend/lexical_generation.hpp"

#include <vector>
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

    std::vector<lexical_stream> lexical_streams;

    const auto tokenized =
        build_lexical_generation(
            files,
            lexical_streams);

    if (!succeeded(tokenized)) {
        return tokenized;
    }

    // The candidate manifest belongs to candidate G0. Persist it only as part
    // of the eventual coordinated successful REBUILD commit.

    diagnostics.emit(
        diagnostic(
            diagnostics::project_rebuild_incomplete,
            operation)
            .detail(
                "Project configuration manifest, flat File Context, and Header/Source lexical generation are complete; Project-declared dependencies are staged, while executed-include dependency discovery, Assign processing, topology finalization, and Graph construction are not implemented yet")
            .build());

    return server_status::unsupported;
}

}
