/*
 * Composed Project configuration input manifest.
 *
 * The manifest is construction state, never resident Project state. It records
 * every project.json participating in one committed composition, per-file byte
 * identity/change proof, and one aggregate configuration-input hash.
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
#include <span>
#include <vector>

namespace cw::server {

struct project_configuration_hash final {
    std::array<std::byte, 32> bytes{};

    friend constexpr bool operator==(
        const project_configuration_hash&,
        const project_configuration_hash&) noexcept = default;
};

// Path is normalized relative to the root Project directory. It is a locator in
// the composition manifest, not semantic identity.
struct project_configuration_file_proof final {
    std::filesystem::path path;
    project_content_hash content_hash{};
    file_change_token change_token{};
    bool change_token_available = false;
};

// Root-first declaration-order DFS. No sorting is performed.
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
