#pragma once

#include "project_configuration.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>

namespace cw::server {

[[nodiscard]] server_status load_project_configuration(
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_configuration& configuration);

}
