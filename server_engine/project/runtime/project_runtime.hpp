/*
 * Resident Project Runtime publication.
 *
 * This is the one lifecycle bridge from an immutable compiled_project_view to
 * final Runtime storage ownership. runtime_layout is construction-only and is
 * discarded before the resident Project is returned.
 */
#pragma once

#include "../project.hpp"
#include "../shm/shm_runtime_v2.hpp"
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
    std::uint64_t shm_pretouch_ns = 0;
    std::uint64_t shm_pretouch_lanes = 0;
    std::uint64_t materialization_ns = 0;
    std::uint64_t runtime_bytes = 0;
    std::uint64_t shm_bytes = 0;

    // RUNTIME-V2-RESIDENT-01.
    // Exactly one construction path is selected for one publication.
    bool runtime_v2 = false;
    std::uint8_t reserved[7]{};
    std::uint64_t runtime_v2_prepare_ns = 0;
    std::uint64_t runtime_v2_metadata_bytes = 0;

    // Runtime V2 semantic construction inventory.
    std::uint64_t runtime_v2_constructor_defaults = 0;
    std::uint64_t runtime_v2_link_count = 0;
    std::uint64_t runtime_v2_link_dereferences = 0;
    std::uint64_t runtime_v2_initialization_count = 0;
    std::uint64_t runtime_v2_initialization_dereferences = 0;

    shm_runtime_v2_prepare_telemetry runtime_v2_prepare{};
    shm_runtime_v2_execute_telemetry runtime_v2_canonical{};
    shm_runtime_v2_execute_telemetry runtime_v2_objects{};
    shm_runtime_v2_link_telemetry runtime_v2_links{};
    shm_runtime_v2_initialization_telemetry
        runtime_v2_initializations{};

    fixed_direct_materialization_telemetry materializer{};
};

struct project_runtime_profile final {
    fixed_direct_materialization_profile materializer{};
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
    project_runtime_telemetry* telemetry = nullptr,
    project_runtime_profile* profile = nullptr);

}
