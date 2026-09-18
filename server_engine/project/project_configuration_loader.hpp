/*
 * Streaming project.json schema boundary.
 *
 * The loader validates exactly one ordered project.json without materializing a
 * project_configuration tree. BUILD/REBUILD consumers will attach directly to
 * this streaming boundary when composition is implemented.
 */
#pragma once

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>
#include <string>

namespace cw::server {

[[nodiscard]] server_status validate_project_configuration(
    std::string& bytes,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics);

}
