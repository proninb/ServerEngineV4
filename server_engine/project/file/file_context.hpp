/*
 * Project construction file context.
 *
 * file_context owns construction-file identity, cold physical-content state,
 * and compact direct dependency topology for one construction operation.
 * Parser state, semantic Graph, Runtime, and resident Project state remain
 * outside this class.
 */
#pragma once

#include "file_identity.hpp"
#include "file_kind.hpp"
#include "../project_path.hpp"
#include "../../filesystem_path.hpp"
#include "../../server_status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {

using file_path_char =
    std::filesystem::path::value_type;

using file_path_view =
    std::basic_string_view<file_path_char>;

// Dense construction-lineage identity of one Project input file. REBUILD creates
// a fresh identity space. BUILD restores existing slots and appends only new
// identities; existing IDs are never renumbered or recycled within the lineage.
class file_id final {
public:
    constexpr file_id() noexcept = default;

    explicit constexpr file_id(
        std::uint32_t value) noexcept
        : id(value) {
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return id;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        const file_id&,
        const file_id&) noexcept = default;

private:
    std::uint32_t id = 0;
};

static_assert(sizeof(file_id) == 4);

// Direct adjacency range inside one global file_id edge arena.
struct file_edge_range final {
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

static_assert(sizeof(file_edge_range) == 8);

// Additional SoA state indexed directly by file_id. No dependency/node identity
// exists beyond file_id itself.
struct file_dependency_record final {
    file_edge_range dependencies;
    file_edge_range dependents;
};

static_assert(sizeof(file_dependency_record) == 16);

// Construction-only staging relation. Source is not stored in committed edge
// arenas because it is implied by the owning file_dependency_record.
struct file_dependency_edge final {
    file_id source{};
    file_id target{};
};

static_assert(sizeof(file_dependency_edge) == 8);

inline constexpr std::uint32_t file_physical_present =
    0x00000001u;

inline constexpr std::uint32_t file_physical_change_token =
    0x00000002u;

// Cold exact-byte/change-proof state. path_hash is deliberately not here:
// platform path hash is a rebuildable in-memory accelerator, never durable
// physical identity.
struct file_physical_record final {
    file_content_hash content_hash{};
    file_change_token change_token{};
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;

    [[nodiscard]] constexpr bool present() const noexcept {
        return (flags & file_physical_present) != 0;
    }

    [[nodiscard]] constexpr bool has_change_token() const noexcept {
        return (flags & file_physical_change_token) != 0;
    }
};

static_assert(sizeof(file_physical_record) == 64);

inline constexpr std::uint32_t invalid_file_content_offset =
    0xffffffffu;

// Construction-only location of one current immutable byte image. Bytes live in
// File Context's content arena and are never part of resident Project state.
struct file_content_record final {
    std::uint32_t offset = invalid_file_content_offset;
    std::uint32_t size = 0;

    [[nodiscard]] constexpr bool materialized() const noexcept {
        return offset != invalid_file_content_offset;
    }
};

static_assert(sizeof(file_content_record) == 8);

enum class file_acquire_result_kind : std::uint8_t {
    unchanged,
    present,
    missing,
    changed_during_read,
    failed,
    allocation_failed,
};

// Borrowed acquisition description. Its path view remains valid only while the
// owning File Context path arena is not mutated.
struct file_acquire_job final {
    file_id file{};
    file_path_view path;
    file_change_token baseline_token{};
    bool baseline_present = false;
    bool baseline_token_available = false;
};

struct file_acquire_result final {
    file_id file{};
    file_acquire_result_kind kind =
        file_acquire_result_kind::failed;
    file_content_snapshot snapshot;
};

// SHA-256 over the ordered raw 32-byte per-file content hashes. This is only
// aggregate byte-content identity; path/role/topology belong to higher identity
// layers.
struct construction_content_hash final {
    std::array<std::byte, 32> bytes{};

    friend constexpr bool operator==(
        const construction_content_hash&,
        const construction_content_hash&) noexcept = default;
};

class database_view;

// Borrowed exact-source baseline used by BUILD. The provider owns the bytes;
// File Context only overlays changed/appended content and never copies unchanged
// committed source text into its construction arena.
class file_content_baseline_view final {
public:
    using reader_function = bool (*)(
        const void* context,
        file_id file,
        std::string_view& output) noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return context != nullptr &&
            reader != nullptr &&
            file_count_value != 0;
    }

    [[nodiscard]] std::size_t file_count() const noexcept {
        return file_count_value;
    }

    [[nodiscard]] bool read(
        file_id file,
        std::string_view& output) const noexcept {

        output = {};

        return valid() &&
            file &&
            file.value() <= file_count_value &&
            reader(context, file, output);
    }

private:
    const void* context = nullptr;
    std::size_t file_count_value = 0;
    reader_function reader = nullptr;

    friend class database_view;
};

// Lightweight adjacency view used by fresh arenas, persisted source.bin edges,
// and sparse BUILD reverse deltas. The overlay form filters removed baseline
// edges and appends only true additions without materializing full adjacency.
class file_dependency_view final {
public:
    class iterator final {
    public:
        [[nodiscard]] file_id operator*() const noexcept;

        iterator& operator++() noexcept;

        friend bool operator==(
            const iterator& left,
            const iterator& right) noexcept {

            return left.owner == right.owner &&
                left.base_index == right.base_index &&
                left.addition_index == right.addition_index &&
                left.additions == right.additions;
        }

        friend bool operator!=(
            const iterator& left,
            const iterator& right) noexcept {

            return !(left == right);
        }

    private:
        iterator(
            const file_dependency_view* value,
            bool end) noexcept;

        const file_dependency_view* owner = nullptr;
        std::size_t base_index = 0;
        std::size_t addition_index = 0;
        bool additions = false;

        friend class file_dependency_view;
    };

    file_dependency_view() noexcept = default;

    [[nodiscard]] static file_dependency_view from_native(
        std::span<const file_id> values) noexcept;

    [[nodiscard]] static file_dependency_view from_encoded(
        std::span<const std::byte> values) noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return count;
    }

    [[nodiscard]] bool empty() const noexcept {
        return count == 0;
    }

    [[nodiscard]] file_id operator[](
        std::size_t index) const noexcept;

    [[nodiscard]] iterator begin() const noexcept {
        return iterator{this, false};
    }

    [[nodiscard]] iterator end() const noexcept {
        return iterator{this, true};
    }

private:
    using filter_function = bool (*)(
        const void* context,
        file_id owner,
        file_id value) noexcept;

    [[nodiscard]] static file_dependency_view from_overlay(
        const file_dependency_view& base,
        std::span<const file_id> added,
        std::size_t final_count,
        const void* context,
        file_id owner,
        filter_function filter) noexcept;

    [[nodiscard]] file_id base_value(
        std::size_t index) const noexcept;

    [[nodiscard]] bool filtered(
        file_id value) const noexcept;

    void seek(
        iterator& value) const noexcept;

    std::span<const file_id> native;
    std::span<const std::byte> encoded;
    std::span<const file_id> added;
    std::size_t base_count = 0;
    std::size_t count = 0;
    const void* filter_context = nullptr;
    file_id filter_owner{};
    filter_function filter = nullptr;

    friend class file_context;
};

class source_save_view;

// Mutable physical-file state for one construction operation. REBUILD owns fresh
// dense arrays. BUILD may bind one immutable source.bin lineage and keeps only
// touched existing files plus newly appended identities in mutable memory.
class file_context final {
public:
    file_context() = default;

    file_context(const file_context&) = delete;
    file_context& operator=(const file_context&) = delete;

    // Binds immutable BUILD lineage without reconstructing committed file/path/
    // physical/topology arrays. The source_save_view must outlive this context.
    [[nodiscard]] server_status bind_baseline(
        const source_save_view& source) noexcept;

    [[nodiscard]] bool baseline_bound() const noexcept {
        return baseline != nullptr;
    }

    // Binds exact Header/Source bytes from the same committed BUILD snapshot as
    // the lexical baseline. The provider must outlive this File Context.
    [[nodiscard]] server_status bind_content_baseline(
        file_content_baseline_view source) noexcept;

    [[nodiscard]] bool content_baseline_bound() const noexcept {
        return content_baseline.valid();
    }

    [[nodiscard]] server_status resolve(
        const std::filesystem::path& path,
        file_kind kind,
        file_id& output) noexcept;

    [[nodiscard]] server_status find(
        const std::filesystem::path& path,
        file_id& output) const noexcept;

    [[nodiscard]] server_status prepare_acquire(
        file_id file,
        file_acquire_job& output) const noexcept;

    static void execute_acquire(
        const file_acquire_job& job,
        file_acquire_result& output) noexcept;

    [[nodiscard]] server_status apply_acquire(
        const file_acquire_result& result,
        bool& content_changed) noexcept;

    [[nodiscard]] server_status calculate_content_hash(
        std::span<const file_id> ordered_files,
        construction_content_hash& output) const noexcept;

    // Marks one BUILD source whose complete outgoing adjacency will be replaced.
    // Zero newly staged edges is a valid replacement that removes all old edges.
    [[nodiscard]] server_status begin_dependency_replacement(
        file_id source) noexcept;

    // REBUILD stages the fresh DAG directly. BUILD accepts edges only for an
    // explicitly replaced existing source or an appended new source.
    [[nodiscard]] server_status add_dependency(
        file_id source,
        file_id target) noexcept;

    [[nodiscard]] server_status finalize_dependency_topology() noexcept;

    [[nodiscard]] file_dependency_view dependencies(
        file_id file) const noexcept;

    [[nodiscard]] file_dependency_view dependents(
        file_id file) const noexcept;

    [[nodiscard]] bool contains(
        file_id file) const noexcept;

    // Baseline paths are converted from persisted UTF-8 only when that file is
    // touched. The returned view follows the existing path-arena lifetime rule.
    [[nodiscard]] file_path_view path(
        file_id file) const noexcept;

    [[nodiscard]] file_kind kind(
        file_id file) const noexcept;

    [[nodiscard]] const file_physical_record* physical(
        file_id file) const noexcept;

    [[nodiscard]] bool content_available(
        file_id file) const noexcept;

    [[nodiscard]] std::string_view content(
        file_id file) const noexcept;

    // Fresh-only contiguous physical storage. BUILD baseline state is sparse and
    // intentionally cannot be exposed as a synthetic contiguous copy.
    [[nodiscard]] std::span<const file_physical_record>
    physical_records() const noexcept {
        return baseline == nullptr
            ? std::span<const file_physical_record>{physical_files}
            : std::span<const file_physical_record>{};
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return baseline_file_count +
            files.size();
    }

    [[nodiscard]] bool dependency_topology_finalized() const noexcept {
        return topology_finalized;
    }

private:
    struct file_record final {
        std::uint32_t path_offset = 0;
        std::uint32_t path_length = 0;

        std::uint32_t path_hash = 0;

        file_kind kind = file_kind::project;
        std::uint8_t reserved[3]{};
    };

    struct path_slot final {
        std::uint32_t fingerprint = 0;
        file_id file{};
    };

    struct baseline_overlay_record final {
        file_id file{};
        std::uint32_t path_offset = 0;
        std::uint32_t path_length = 0;
        file_physical_record physical;
        file_content_record content;
    };

    struct baseline_overlay_slot final {
        file_id file{};
        std::uint32_t record = 0;
    };

    static_assert(sizeof(file_record) == 16);
    static_assert(sizeof(path_slot) == 8);
    static_assert(sizeof(baseline_overlay_slot) == 8);

    [[nodiscard]] static std::uint32_t fingerprint(
        const filesystem_path_key& key) noexcept;

    [[nodiscard]] bool local_index(
        file_id file,
        std::size_t& output) const noexcept;

    [[nodiscard]] server_status same_key(
        file_id file,
        const filesystem_path_key& key,
        bool& output) const noexcept;

    [[nodiscard]] server_status find_key(
        const filesystem_path_key& key,
        std::uint32_t hash,
        file_id& output) const noexcept;

    [[nodiscard]] server_status ensure_index_capacity(
        std::size_t additional) noexcept;

    void insert_index(
        std::vector<path_slot>& index,
        file_id file,
        std::uint32_t hash) const noexcept;

    [[nodiscard]] bool find_baseline_overlay(
        file_id file,
        std::size_t& output) const noexcept;

    [[nodiscard]] server_status ensure_baseline_overlay_capacity(
        std::size_t additional) const noexcept;

    void insert_baseline_overlay(
        std::vector<baseline_overlay_slot>& index,
        file_id file,
        std::uint32_t record) const noexcept;

    [[nodiscard]] server_status ensure_baseline_overlay(
        file_id file,
        std::size_t& output) const noexcept;

    struct topology_source_record final {
        file_id source{};
        file_edge_range dependencies;
    };

    struct topology_source_slot final {
        file_id source{};
        std::uint32_t record = 0;
    };

    struct topology_target_record final {
        file_id target{};
        file_edge_range additions;
        std::uint32_t removals = 0;
    };

    struct topology_target_slot final {
        file_id target{};
        std::uint32_t record = 0;
    };

    static_assert(sizeof(topology_source_slot) == 8);
    static_assert(sizeof(topology_target_slot) == 8);

    [[nodiscard]] bool find_topology_source(
        file_id source,
        std::size_t& output) const noexcept;

    [[nodiscard]] server_status ensure_topology_source_capacity(
        std::size_t additional) noexcept;

    void insert_topology_source(
        std::vector<topology_source_slot>& index,
        file_id source,
        std::uint32_t record) const noexcept;

    [[nodiscard]] bool find_topology_target(
        file_id target,
        std::size_t& output) const noexcept;

    [[nodiscard]] bool reverse_dependency_removed(
        file_id target,
        file_id source) const noexcept;

    [[nodiscard]] static bool reverse_dependency_filter(
        const void* context,
        file_id target,
        file_id source) noexcept;

    [[nodiscard]] server_status finalize_baseline_dependency_topology() noexcept;

    const source_save_view* baseline = nullptr;
    std::size_t baseline_file_count = 0;
    file_content_baseline_view content_baseline;

    std::vector<file_record> files;
    std::vector<file_path_char> path_chars;
    std::vector<path_slot> path_index;

    std::vector<file_physical_record> physical_files;
    std::vector<file_content_record> content_files;
    std::vector<char> content_bytes;

    std::vector<file_dependency_record> dependency_files;
    std::vector<file_id> forward_edges;
    std::vector<file_id> reverse_edges;

    std::vector<file_dependency_edge> dependency_edges;
    bool topology_finalized = false;

    // BUILD-only sparse replacement state. Forward replacements are complete
    // per marked source; reverse state stores only true add/remove deltas.
    std::vector<topology_source_record> topology_sources;
    std::vector<topology_source_slot> topology_source_index;
    std::vector<file_id> topology_forward_edges;

    std::vector<topology_target_record> topology_targets;
    std::vector<topology_target_slot> topology_target_index;
    std::vector<file_id> topology_reverse_additions;
    std::vector<std::uint64_t> topology_reverse_removals;

    mutable std::vector<baseline_overlay_record> baseline_overlays;
    mutable std::vector<baseline_overlay_slot> baseline_overlay_index;
    mutable std::vector<file_path_char> baseline_path_chars;

};

}
