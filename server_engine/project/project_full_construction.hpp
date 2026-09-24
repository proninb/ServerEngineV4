/*
 * Shared full Project source-construction pipeline.
 *
 * PUBLISH and REBUILD compile the same project.json/source inputs into the same
 * final G. Their only difference is persistence policy after construction:
 * PUBLISH persists compiled.bin only; REBUILD also persists BUILD acceleration.
 */
#pragma once

#include "project.hpp"
#include "../configuration/server_configuration.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace cw::server {

enum class full_construction_mode : std::uint8_t {
    publish,
    rebuild,
};

[[nodiscard]] server_status construct_full_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    full_construction_mode mode,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output);

}
