/*
 * Project PUBLISH pipeline.
 *
 * PUBLISH compiles project.json and its source inputs from scratch, writes the
 * final LOAD artifacts compiled.bin/runtime.bin, and publishes the Project.
 * It creates no BUILD acceleration state or USN tokens and issues no explicit
 * durable flush. BUILD/REBUILD own durability.
 */
#pragma once

#include "project.hpp"
#include "project_full_construction.hpp"
#include "../configuration/server_configuration.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>
#include <memory>

namespace cw::server {

[[nodiscard]] server_status publish_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output,
    full_construction_telemetry* telemetry = nullptr);

}
