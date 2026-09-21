/*
 * Project BUILD pipeline.
 *
 * BUILD incrementally constructs from the last successful persisted baseline.
 * The current implementation still receives the resident Project only as a
 * temporary entry-path source until the lifecycle boundary is corrected.
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
    const project& resident,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output);

}
