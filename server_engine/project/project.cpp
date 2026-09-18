#include "project.hpp"

#include "project_configuration_loader.hpp"

#include <utility>

namespace cw::server {

server_status project::load(
    const std::filesystem::path& configuration_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    project_configuration configuration;

    const auto status =
        load_project_configuration(
            configuration_path,
            operation,
            diagnostics,
            configuration);

    if (!succeeded(status)) {
        return status;
    }

    path = configuration_path;
    configuration_value = std::move(configuration);

    return server_status::success;
}

}
