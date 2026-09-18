/*
 * Minimal Project bootstrap implementation.
 */
#include "project.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <fstream>

namespace cw::server {

server_status project::load(
    const std::filesystem::path& configuration_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    std::ifstream stream(
        configuration_path,
        std::ios::binary);

    if (!stream) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_load_failed,
                operation)
                .file(configuration_path)
                .detail("Cannot open Project configuration file")
                .build());

        return server_status::project_load_failed;
    }

    // Publish Project state only after bootstrap validation succeeds.
    path = configuration_path;

    return server_status::success;
}

}
