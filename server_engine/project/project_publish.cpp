#include "project_publish.hpp"

#include "project_full_construction.hpp"

namespace cw::server {

server_status publish_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output,
    full_construction_telemetry* telemetry) {

    return construct_full_project(
        project_path,
        settings,
        full_construction_mode::publish,
        operation,
        diagnostics,
        output,
        telemetry);
}

}
