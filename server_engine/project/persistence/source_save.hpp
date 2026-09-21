/*
 * Persisted SourceSave image boundary.
 *
 * SourceSave is BUILD acceleration state for physical construction inputs:
 * file_id lineage, UTF-8 physical paths, file kind, exact-content proof, native
 * change-token proof, and finalized direct forward/reverse dependency topology.
 * It contains no Parser, Semantic, Graph, Runtime, or SHM state.
 */
#pragma once

#include "project_artifact.hpp"
#include "../file/file_context.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {

enum class source_save_result : std::uint8_t {
    success,
    invalid_state,
    invalid_image,
    failed,
};

// Zero-copy read-only view over source.bin. The authoritative baseline owner
// performs validate_source_save_image() once before binding; bind itself only
// establishes structurally bounded section views and allocates nothing.
// The caller owns the image bytes for the complete lifetime of the view.
class source_save_view final {
public:
    source_save_view() noexcept = default;

    [[nodiscard]] source_save_result bind(
        std::span<const std::byte> image) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return !bytes.empty();
    }

    [[nodiscard]] std::size_t file_count() const noexcept {
        return file_count_value;
    }

    [[nodiscard]] bool contains(file_id file) const noexcept;
    [[nodiscard]] bool current_member(file_id file) const noexcept;
    [[nodiscard]] file_kind kind(file_id file) const noexcept;

    [[nodiscard]] std::string_view path_utf8(
        file_id file) const noexcept;

    [[nodiscard]] bool physical(
        file_id file,
        file_physical_record& output) const noexcept;

    [[nodiscard]] std::size_t dependency_count(
        file_id file) const noexcept;

    [[nodiscard]] file_id dependency_at(
        file_id file,
        std::size_t index) const noexcept;

    [[nodiscard]] std::size_t dependent_count(
        file_id file) const noexcept;

    [[nodiscard]] file_id dependent_at(
        file_id file,
        std::size_t index) const noexcept;

private:
    std::span<const std::byte> bytes;
    std::size_t records_offset = 0;
    std::size_t paths_offset = 0;
    std::size_t forward_offset = 0;
    std::size_t reverse_offset = 0;
    std::uint32_t file_count_value = 0;
    std::uint32_t path_bytes_value = 0;
    std::uint32_t forward_count_value = 0;
    std::uint32_t reverse_count_value = 0;
};

struct source_save_change_scan_metrics final {
    std::uint64_t current_files = 0;
    std::uint64_t token_proved_unchanged = 0;
    std::uint64_t files_read = 0;
    std::uint64_t bytes_read = 0;
    std::uint64_t dirty_files = 0;
    std::uint64_t missing_files = 0;
};

[[nodiscard]] source_save_result build_source_save_image(
    const file_context& files,
    project_artifact_image& output) noexcept;

[[nodiscard]] source_save_result validate_source_save_image(
    std::span<const std::byte> image) noexcept;

// BUILD physical scan over the committed SourceSave baseline. Native
// change-token proof avoids file reads when possible; otherwise current bytes
// are read and compared by SHA-256. Output order is ascending file_id.
[[nodiscard]] server_status scan_source_save_changes(
    const source_save_view& baseline,
    std::vector<file_id>& dirty,
    source_save_change_scan_metrics* metrics = nullptr) noexcept;

// Computes the affected closure from the OLD committed reverse topology before
// any dependency replacement. Traversal is deterministic and unsorted.
[[nodiscard]] server_status collect_source_save_affected(
    const source_save_view& baseline,
    std::span<const file_id> dirty,
    std::vector<file_id>& affected) noexcept;

}

