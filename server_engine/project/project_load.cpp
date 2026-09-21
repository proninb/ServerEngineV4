#include "project_load.hpp"

#include "project_lifecycle_context.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <memory>

namespace cw::server {

server_status load_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    output.reset();

    load_context context{
        settings};
    (void)context;

    diagnostics.emit(
        diagnostic(
            diagnostics::project_load_incomplete,
            operation)
            .file(project_path)
            .detail(
                "Committed Project generation restore is not implemented")
            .build());

    return server_status::unsupported;
}

}
