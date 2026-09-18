/*
 * Streaming project.json schema/composition-reference boundary.
 *
 * One file is validated without materializing a project_configuration tree.
 * Direct child Project references are emitted in declaration order so recursive
 * composition can be owned by the Project composition layer.
 */
#pragma once

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cw::server {

[[nodiscard]] server_status read_project_configuration(
    std::string& bytes,
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::vector<std::filesystem::path>& project_references);

}
