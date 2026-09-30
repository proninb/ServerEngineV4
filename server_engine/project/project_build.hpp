/*
 * Project BUILD pipeline.
 *
 * REBUILD creates the persisted BUILD lineage. BUILD consumes that lineage;
 * absence of source.bin routes to REBUILD. An exact no-change BUILD publishes
 * the persisted compiled.bin directly, while changed input continues through
 * sparse BUILD reconstruction. Resident Project state is never a BUILD input.
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

[[nodiscard]] server_status build_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output);

}
