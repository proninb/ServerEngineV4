/*
 * Project LOAD pipeline.
 *
 * LOAD restores persisted resident state. It must not parse project.json into
 * construction state, run Source Manager, or silently become BUILD/REBUILD.
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

[[nodiscard]] server_status load_project(
    const std::filesystem::path& project_path,
    const server_abi_configuration& abi,
    const project_files_configuration& project_files,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output);

}
