/*
 * Project PUBLISH pipeline.
 *
 * PUBLISH compiles project.json and its source inputs from scratch, persists only
 * the final LOAD artifact compiled.bin, and publishes the resulting Project.
 * It deliberately creates no BUILD acceleration state.
 */
#pragma once

#include "project.hpp"
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
    std::unique_ptr<project>& output);

}
