/*
 * Resident Project Runtime publication.
 *
 * This is the one lifecycle bridge from an immutable compiled_project_view to
 * final Runtime storage ownership. runtime_layout is construction-only and is
 * discarded before the resident Project is returned.
 */
#pragma once

#include "../project.hpp"
#include "fixed_direct_materializer.hpp"

#include "../../configuration/server_configuration.hpp"
#include "../../diagnostics/diagnostic_collection.hpp"
#include "../../operation.hpp"
#include "../../read_only_file_mapping.hpp"
#include "../../server_status.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace cw::server {

struct project_runtime_telemetry final {
    std::uint64_t layout_ns = 0;
    std::uint64_t shm_create_ns = 0;
    std::uint64_t materialization_ns = 0;
    std::uint64_t runtime_bytes = 0;
    std::uint64_t shm_bytes = 0;
    fixed_direct_materialization_telemetry materializer{};
};

// Creates the final resident Project state for the configured Runtime/SHM mode.
// FIXED_DIRECT allocates the one named Project SHM at the configured exact VA
// and materializes the final ABI-native Runtime image directly into that SHM.
[[nodiscard]] server_status create_resident_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    read_only_file_mapping&& compiled_mapping,
    compiled_project_view compiled,
    std::unique_ptr<project>& output,
    project_runtime_telemetry* telemetry = nullptr);

}
