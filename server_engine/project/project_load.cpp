#include "project_load.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <fstream>
#include <memory>

namespace cw::server {

server_status load_project(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    // Temporary gate until persisted Graph restore replaces direct entry access.
    std::ifstream stream(
        project_path,
        std::ios::binary);

    if (!stream) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_load_failed,
                operation)
                .file(project_path)
                .detail("Cannot open Project entry file")
                .build());

        return server_status::project_load_failed;
    }

    output =
        std::make_unique<project>(
            project_path);

    return server_status::success;
}

}
