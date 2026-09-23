/*
 * Persisted SourceSave BUILD state.
 *
 * source.bin stores file_id lineage, physical paths, SHA-256 byte identity,
 * direct forward/reverse topology, and optional Windows USN journal acceleration
 * state. It never stores a second copy of Project source bytes.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../source/source_map.hpp"

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

struct source_save_build_options final {
    file_change_checkpoint change_checkpoint{};
};

// Frozen direct-encoding plan for one finalized, unchanged File Context.
// It owns only persistence-specific tracking accelerators; paths, records, and
// dependency arenas remain in File Context and are never duplicated here.
class source_save_layout final {
public:
    source_save_layout() = default;

    source_save_layout(
        const source_save_layout&) = delete;

    source_save_layout& operator=(
        const source_save_layout&) = delete;

    source_save_layout(
        source_save_layout&&) noexcept = default;

    source_save_layout& operator=(
        source_save_layout&&) noexcept = default;

    [[nodiscard]] std::size_t size() const noexcept {
        return size_value;
    }

private:
    void reset() noexcept {
        size_value = 0;
        records_offset = 0;
        paths_offset = 0;
        forward_offset = 0;
        reverse_offset = 0;
        file_index_offset = 0;
        directory_index_offset = 0;
        checksum_offset = 0;
        presence_offset = 0;
        type_count = object_count = link_count = 0;

        file_count = 0;
        path_bytes = 0;
        forward_count = 0;
        reverse_count = 0;
        file_index_count = 0;
        directory_index_count = 0;

        checkpoint = {};
        file_index_references.clear();
        file_index_files.clear();
        directory_index_references.clear();
        directory_index_flags.clear();
    }

    std::size_t size_value = 0;
    std::size_t records_offset = 0;
    std::size_t paths_offset = 0;
    std::size_t forward_offset = 0;
    std::size_t reverse_offset = 0;
    std::size_t file_index_offset = 0;
    std::size_t directory_index_offset = 0;
    std::size_t checksum_offset = 0;
    std::size_t presence_offset = 0;
    std::uint32_t type_count = 0, object_count = 0, link_count = 0;

    std::uint32_t file_count = 0;
    std::uint32_t path_bytes = 0;
    std::uint32_t forward_count = 0;
    std::uint32_t reverse_count = 0;
    std::uint32_t file_index_count = 0;
    std::uint32_t directory_index_count = 0;

    file_change_checkpoint checkpoint;

    // SoA avoids native padding: transient memory is the same 12 bytes/slot as
    // the persisted identity-index payload instead of a padded native struct.
    std::vector<std::uint64_t> file_index_references;
    std::vector<file_id> file_index_files;
    std::vector<std::uint64_t> directory_index_references;
    std::vector<std::uint32_t> directory_index_flags;

    friend source_save_result prepare_source_save_layout(const file_context &,
                                                         const source_map &,
                                                         const source_save_build_options &,
                                                         source_save_layout &) noexcept;

    friend source_save_result encode_source_save_image(const file_context &,
                                                       const source_map &,
                                                       const source_save_layout &,
                                                       std::span<std::byte>) noexcept;
};

class source_save_edge_view final {
public:
    source_save_edge_view() noexcept = default;

    [[nodiscard]] std::size_t size() const noexcept {
        return count;
    }

    [[nodiscard]] file_id operator[](
        std::size_t index) const noexcept;

private:
    friend class source_save_view;

    std::span<const std::byte> bytes;
    std::size_t count = 0;
};

struct source_save_file_view final {
    file_id file{};
    file_kind kind = file_kind::project;
    bool current_member = false;
    file_physical_record physical;
    std::uint64_t file_reference = 0;
    std::string_view path_utf8;
    source_save_edge_view dependencies;
    source_save_edge_view dependents;
};

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

    [[nodiscard]] bool contains(
        file_id file) const noexcept;

    [[nodiscard]] bool file(
        file_id id,
        source_save_file_view& output) const noexcept;

    [[nodiscard]] file_change_checkpoint
    change_checkpoint() const noexcept {
        return checkpoint;
    }

    [[nodiscard]] file_id find_file_reference(
        std::uint64_t file_reference) const noexcept;

    [[nodiscard]] std::uint32_t directory_watch_flags(
        std::uint64_t file_reference) const noexcept;

    [[nodiscard]] std::size_t type_presence_count() const noexcept {
        return type_count;
    }
    [[nodiscard]] std::size_t object_presence_count() const noexcept {
        return object_count;
    }
    [[nodiscard]] std::size_t link_presence_count() const noexcept {
        return link_count;
    }
    [[nodiscard]] source_type_presence type_presence(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t object_presence(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t link_presence(std::size_t index) const noexcept;

  private:
    friend source_save_result validate_source_save_image(
        std::span<const std::byte>) noexcept;

    std::size_t presence_offset = 0;
    std::uint32_t type_count = 0, object_count = 0, link_count = 0;
    std::span<const std::byte> bytes;
    std::size_t records_offset = 0;
    std::size_t paths_offset = 0;
    std::size_t forward_offset = 0;
    std::size_t reverse_offset = 0;
    std::size_t file_index_offset = 0;
    std::size_t directory_index_offset = 0;

    std::uint32_t file_count_value = 0;
    std::uint32_t path_bytes_value = 0;
    std::uint32_t forward_count_value = 0;
    std::uint32_t reverse_count_value = 0;
    std::uint32_t file_index_count_value = 0;
    std::uint32_t directory_index_count_value = 0;

    file_change_checkpoint checkpoint;
};

struct source_save_change_scan_metrics final {
    file_change_backend backend =
        file_change_backend::none;

    std::uint64_t journal_records = 0;
    std::uint64_t matched_files = 0;

    std::uint64_t current_files = 0;
    std::uint64_t files_read = 0;
    std::uint64_t bytes_read = 0;
    std::uint64_t dirty_files = 0;
    std::uint64_t missing_files = 0;

    bool fast_path = false;
    bool fallback = false;
};

// Checkpoint for source.bin produced by this BUILD. It is captured before
// dirty detection begins. Filesystem changes after that point remain visible to
// the next BUILD. Portable fallback clears it because journal identity
// continuity was not used to establish this BUILD.
struct source_save_change_scan final {
    source_save_change_scan_metrics metrics;
    file_change_checkpoint next_checkpoint;
};

[[nodiscard]] source_save_result
prepare_source_save_layout(const file_context &files,
                           const source_map &sources,
                           const source_save_build_options &options,
                           source_save_layout &output) noexcept;

[[nodiscard]] source_save_result prepare_source_save_layout(const file_context &files,
                                                            const source_map &sources,
                                                            source_save_layout &output) noexcept;

// Direct encoder. Precondition: File Context is unchanged since layout
// preparation and its dependency topology remains finalized. No filesystem
// discovery or whole-image allocation occurs here.
[[nodiscard]] source_save_result encode_source_save_image(const file_context &files,
                                                          const source_map &sources,
                                                          const source_save_layout &layout,
                                                          std::span<std::byte> output) noexcept;

[[nodiscard]] source_save_result validate_source_save_image(
    std::span<const std::byte> image) noexcept;

[[nodiscard]] server_status scan_source_save_changes(
    const source_save_view& persisted,
    std::vector<file_id>& dirty,
    source_save_change_scan* scan = nullptr) noexcept;

[[nodiscard]] server_status collect_source_save_affected(
    const source_save_view& persisted,
    std::span<const file_id> dirty,
    std::vector<file_id>& affected) noexcept;

class compiled_project_view;
// Cold cross-artifact audit: presence is derived from root -> contribution ownership.
[[nodiscard]] source_save_result
verify_source_save_presence(const source_save_view &source,
                            const compiled_project_view &compiled) noexcept;
}
