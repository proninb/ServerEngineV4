/*
 * Composed Project configuration input manifest.
 *
 * The manifest is construction state, never resident Project state. It records
 * every project.json participating in one committed composition, the declaring
 * edge/locator semantics, per-file byte identity/change proof, and one aggregate
 * configuration-input hash.
 */
#pragma once

#include "project_identity.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <vector>

namespace cw::server {

inline constexpr std::uint32_t invalid_configuration_file =
    (std::numeric_limits<std::uint32_t>::max)();

enum class project_configuration_path_type : std::uint8_t {
    relative,
    absolute,
};

struct project_configuration_hash final {
    std::array<std::byte, 32> bytes{};

    friend constexpr bool operator==(
        const project_configuration_hash&,
        const project_configuration_hash&) noexcept = default;
};

// Root-first declaration-order DFS. Relative locators resolve from declaring_file;
// absolute locators are location-bound. Resolved paths and platform path keys are
// temporary construction state and are never persisted here.
struct project_configuration_file_proof final {
    std::uint32_t declaring_file = invalid_configuration_file;
    project_configuration_path_type path_type =
        project_configuration_path_type::relative;
    std::filesystem::path path;
    project_content_hash content_hash{};
    file_change_token change_token{};
    bool change_token_available = false;
};

// Root-first declaration-order DFS. No sorting or lookup table is required for
// persisted path resolution: every non-root declaring_file precedes its child.
struct project_configuration_manifest final {
    std::vector<project_configuration_file_proof> files;
    project_configuration_hash configuration_hash{};
};

enum class project_configuration_manifest_verification : std::uint8_t {
    unchanged,
    changed,
};

[[nodiscard]] project_configuration_hash calculate_project_configuration_hash(
    std::span<const project_configuration_file_proof> files);

[[nodiscard]] server_status compose_project_configuration_manifest(
    const std::filesystem::path& root_project_path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_configuration_manifest& output);

[[nodiscard]] server_status verify_project_configuration_manifest(
    const std::filesystem::path& root_project_path,
    const project_configuration_manifest& persisted,
    operation_id operation,
    diagnostic_collection& diagnostics,
    project_configuration_manifest_verification& verification);

}
