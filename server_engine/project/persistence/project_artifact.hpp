/*
 * Project persisted-artifact layout and committed-baseline boundary.
 *
 * A/B slots are crash-safe persistence mechanics only. baseline.bin names the
 * authoritative slot and proves the exact four artifacts committed in it.
 */
#pragma once

#include "../file/file_identity.hpp"
#include "../../configuration/server_configuration.hpp"
#include "../../read_only_file_mapping.hpp"

#include <array>
#include <cstddef>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace cw::server {

enum class project_artifact_slot : std::uint8_t {
    slot0 = 0,
    slot1 = 1,
};

enum class project_artifact_kind : std::uint8_t {
    manifest = 0,
    source_save = 1,
    database = 2,
    compiled = 3,
};

inline constexpr std::size_t project_artifact_kind_count = 4;

struct project_artifact_image final {
    std::vector<std::byte> bytes;
    file_content_hash hash{};
};

struct project_artifact_proof final {
    std::uint64_t size = 0;
    file_content_hash hash{};
    file_change_token change_token{};
    bool change_token_available = false;
};

struct project_baseline_descriptor final {
    project_artifact_slot slot =
        project_artifact_slot::slot0;

    std::array<
        project_artifact_proof,
        project_artifact_kind_count>
        artifacts{};

    [[nodiscard]] const project_artifact_proof& artifact(
        project_artifact_kind kind) const noexcept {

        return artifacts[
            static_cast<std::size_t>(
                kind)];
    }
};

struct project_artifact_slot_layout final {
    std::filesystem::path directory;
    std::filesystem::path manifest;
    std::filesystem::path source_save;
    std::filesystem::path database;
    std::filesystem::path compiled;
};

struct project_artifact_layout final {
    std::filesystem::path root;
    std::filesystem::path baseline;
    std::array<project_artifact_slot_layout, 2> slots;
};

enum class project_artifact_layout_result : std::uint8_t {
    success,
    failed,
};

enum class project_baseline_image_result : std::uint8_t {
    success,
    invalid_state,
    invalid_image,
    failed,
};

enum class project_artifact_open_result : std::uint8_t {
    success,
    not_found,
    invalid,
    io_failed,
};

[[nodiscard]] project_artifact_layout_result make_project_artifact_layout(
    const std::filesystem::path& root_project_path,
    const server_files_configuration& files,
    project_artifact_layout& output) noexcept;

void finalize_project_artifact_image(
    project_artifact_image& image) noexcept;

[[nodiscard]] project_baseline_image_result build_project_baseline_image(
    const project_baseline_descriptor& baseline,
    project_artifact_image& output) noexcept;

[[nodiscard]] project_baseline_image_result decode_project_baseline_image(
    std::span<const std::byte> image,
    project_baseline_descriptor& output) noexcept;

// Cold commit helper for producing one artifact proof without retaining bytes.
[[nodiscard]] project_artifact_open_result capture_project_artifact_proof(
    const std::filesystem::path& path,
    project_artifact_proof& output) noexcept;

// Opens the immutable artifact by mmap and authenticates it against baseline.bin.
// Native change-token equality is O(1); otherwise SHA-256 is computed directly
// over mapped pages without a whole-file heap copy.
[[nodiscard]] project_artifact_open_result open_verified_project_artifact(
    const std::filesystem::path& path,
    const project_artifact_proof& proof,
    read_only_file_mapping& output) noexcept;

}
