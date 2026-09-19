/*
 * Project construction file context.
 *
 * file_context owns compact physical-file identity and cold physical-content
 * state for one candidate construction. Parser state, Graph, Runtime, and
 * resident Project state remain outside this class.
 */
#pragma once

#include "file_identity.hpp"
#include "../project_path.hpp"
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

enum class file_role : std::uint8_t {
    type,
    source,
};

struct file_root final {
    file_id file{};
    file_role role = file_role::type;
};

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

// Mutable physical-file state for one candidate construction. The candidate
// construction owner is the rollback boundary, so File Context has no nested
// update transaction and no mutex.
class file_context final {
public:
    file_context() = default;

    file_context(const file_context&) = delete;
    file_context& operator=(const file_context&) = delete;

    [[nodiscard]] server_status resolve(
        const std::filesystem::path& path,
        file_id& output) noexcept;

    [[nodiscard]] server_status find(
        const std::filesystem::path& path,
        file_id& output) const noexcept;

    [[nodiscard]] server_status add_root(
        file_id file,
        file_role role) noexcept;

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

    [[nodiscard]] bool contains(
        file_id file) const noexcept;

    // Precondition: contains(file) == true. The view remains valid until File
    // Context mutates its path arena.
    [[nodiscard]] file_path_view path(
        file_id file) const noexcept;

    [[nodiscard]] const file_physical_record* physical(
        file_id file) const noexcept;

    [[nodiscard]] std::span<const file_root> roots() const noexcept {
        return root_files;
    }

    [[nodiscard]] std::span<const file_physical_record>
    physical_records() const noexcept {
        return physical_files;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return files.size();
    }

private:
    enum class root_role : std::uint8_t {
        none,
        type,
        source,
    };

    struct file_record final {
        std::uint32_t path_offset = 0;
        std::uint32_t path_length = 0;

        // Derived in-memory accelerator. Rebuild from the physical path when a
        // persisted File Context is restored; never use as durable identity.
        std::uint32_t path_hash = 0;

        root_role role = root_role::none;
        std::uint8_t reserved[3]{};
    };

    struct path_slot final {
        std::uint32_t fingerprint = 0;
        file_id file{};
    };

    static_assert(sizeof(file_record) == 16);
    static_assert(sizeof(path_slot) == 8);

    [[nodiscard]] static std::uint32_t fingerprint(
        const project_path_key& key) noexcept;

    [[nodiscard]] server_status same_key(
        file_id file,
        const project_path_key& key,
        bool& output) const noexcept;

    [[nodiscard]] server_status find_key(
        const project_path_key& key,
        std::uint32_t hash,
        file_id& output) const noexcept;

    [[nodiscard]] server_status ensure_index_capacity(
        std::size_t additional) noexcept;

    void insert_index(
        std::vector<path_slot>& index,
        file_id file,
        std::uint32_t hash) const noexcept;

    std::vector<file_record> files;
    std::vector<file_path_char> path_chars;
    std::vector<path_slot> path_index;
    std::vector<file_root> root_files;

    // Kept separate from hot path/identity records so SHA-256/change-token data
    // is not pulled into cache during path lookup.
    std::vector<file_physical_record> physical_files;
};

}
