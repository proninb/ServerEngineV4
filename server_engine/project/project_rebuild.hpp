/*
 * Project REBUILD pipeline.
 *
 * REBUILD ignores incremental construction state, streams project.json through
 * the ordered Project schema, then constructs a new G0 from explicit roots.
 */
#pragma once

#include "project.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>
#include <memory>

namespace cw::server {

[[nodiscard]] server_status rebuild_project(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output);

}
