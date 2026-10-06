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

struct full_construction_telemetry final {
    bool detailed_source = true;
    std::uint64_t cleanup_ns = 0;
    std::uint64_t configuration_ns = 0;
    std::uint64_t lexical_ns = 0;
    std::uint64_t assign_ns = 0;
    std::uint64_t semantic_ns = 0;
    std::uint64_t header_ns = 0;
    std::uint64_t include_ns = 0;
    std::uint64_t include_count = 0;
    std::uint64_t include_lexical_ns = 0;
    std::uint64_t prepared_include_count = 0;
    std::uint64_t source_ns = 0;
    std::uint64_t source_object_ns = 0;
    std::uint64_t source_assignment_ns = 0;
    std::uint64_t source_link_ns = 0;
    std::uint64_t source_endpoint_ns = 0;
    std::uint64_t source_member_ns = 0;
    std::uint64_t source_member_lookups = 0;
    std::uint64_t source_initialization_commit_ns = 0;
    std::uint64_t source_link_commit_ns = 0;
    std::uint64_t source_provenance_ns = 0;
    std::uint64_t source_decode_ns = 0;
    std::uint64_t source_intern_ns = 0;
    std::uint64_t source_token_count = 0;
    std::uint64_t source_identifier_count = 0;


    std::uint64_t source_finalize_ns = 0;
    std::uint64_t persistence_ns = 0;
    std::uint64_t compiled_prepare_ns = 0;
    std::uint64_t compiled_map_ns = 0;
    std::uint64_t compiled_encode_ns = 0;
    std::uint64_t compiled_validate_ns = 0;
    std::uint64_t compiled_layout_ns = 0;
    std::uint64_t compiled_type_ns = 0;
    std::uint64_t compiled_plan_ns = 0;
    std::uint64_t compiled_runtime_prepare_ns = 0;

    // PUBLISH-RUNTIME-PLAN-PROFILE-01:
    // coarse producer-only profiling; no per-record clocks.
    std::uint64_t runtime_plan_links_ns = 0;
    std::uint64_t runtime_plan_initializations_ns = 0;
    std::uint64_t runtime_plan_link_slots = 0;
    std::uint64_t runtime_plan_live_links = 0;
    std::uint64_t runtime_plan_initializations = 0;
    std::uint64_t runtime_plan_link_dereferences = 0;
    std::uint64_t runtime_plan_initialization_dereferences = 0;
    std::uint64_t runtime_plan_total_dereferences = 0;
    std::uint64_t runtime_plan_parallel_lanes = 1;

    std::uint64_t compiled_remap_ns = 0;
    std::uint64_t compiled_physical_ns = 0;
    std::uint64_t compiled_abi_encode_ns = 0;
    std::uint64_t compiled_type_encode_ns = 0;
    std::uint64_t compiled_runtime_encode_ns = 0;
    std::uint64_t compiled_flush_ns = 0;
    std::uint64_t resident_ns = 0;
};

[[nodiscard]] server_status construct_full_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    full_construction_mode mode,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output,
    full_construction_telemetry* telemetry = nullptr);

}
