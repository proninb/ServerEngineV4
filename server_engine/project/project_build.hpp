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

struct project_build_telemetry final {
    std::uint64_t total_ns = 0;
    std::uint64_t sparse_reconstruction_ns = 0;
    std::uint64_t dense_projection_ns = 0;
    std::uint64_t artifact_materialization_ns = 0;
    std::uint64_t runtime_publication_ns = 0;
    std::uint64_t promotion_ns = 0;

    std::uint64_t candidate_files = 0;
    std::uint64_t changed_files = 0;
    std::uint64_t affected_files = 0;
    std::uint64_t affected_roots = 0;
    std::uint64_t invalidated_roots = 0;
    std::uint64_t replay_roots = 0;

    std::uint64_t retire_types = 0;
    std::uint64_t clear_type_definitions = 0;
    std::uint64_t retire_objects = 0;
    std::uint64_t retire_links = 0;

    std::uint64_t graph_type_patches = 0;
    std::uint64_t graph_appended_types = 0;
    std::uint64_t graph_object_patches = 0;
    std::uint64_t graph_appended_objects = 0;
    std::uint64_t graph_link_patches = 0;
    std::uint64_t graph_appended_links = 0;

    bool rebuild_fallback = false;
};

[[nodiscard]] server_status build_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output,
    project_build_telemetry* telemetry = nullptr);

}
