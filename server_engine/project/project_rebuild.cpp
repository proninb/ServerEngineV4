#include "project_rebuild.hpp"

#include "project_configuration_manifest.hpp"
#include "file/file_context.hpp"
#include "frontend/lexical_generation.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <string>

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

    lexical_generation lexical;
    lexical_failure failure;

    const auto tokenized =
        build_lexical_generation(
            files,
            lexical,
            &failure);

    if (!succeeded(tokenized)) {
        if (failure.file &&
            failure.error.reason !=
                lexical_error_reason::none &&
            files.content_available(
                failure.file)) {

            const auto path_view =
                files.path(
                    failure.file);

            std::filesystem::path source_path{
                path_view.begin(),
                path_view.end()};

            const auto source =
                files.content(
                    failure.file);

            try {
                const auto diagnostic_file =
                    diagnostics.add_source(
                        source_path,
                        std::string{
                            source.data(),
                            source.size()});

                const auto location =
                    diagnostics.locate(
                        diagnostic_file,
                        failure.error.offset,
                        failure.error.length);

                diagnostics.emit(
                    diagnostic(
                        diagnostics::project_lexical_error,
                        operation)
                        .location(location)
                        .detail(
                            lexical_error_message(
                                failure.error.reason))
                        .build());
            }
            catch (...) {
                return server_status::io_error;
            }
        }

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
