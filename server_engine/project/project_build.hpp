/*
 * Project BUILD pipeline.
 *
 * BUILD starts from persisted Project identity and construction metadata.
 * project.json is parsed only when its content identity changed or cannot be
 * proven unchanged. No universal Project construction context is used.
 */
#pragma once

#include "project.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>
#include <memory>

namespace cw::server {

[[nodiscard]] server_status build_project(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output);

}
