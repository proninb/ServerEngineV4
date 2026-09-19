/*
 * Project construction file context.
 *
 * file_context owns compact physical-file identity state for one candidate
 * construction. Type/Source parser state, Graph, Runtime, and resident Project
 * state remain outside this class.
 */
#pragma once

#include "../project_path.hpp"
#include "../../server_status.hpp"

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

    [[nodiscard]] bool contains(
        file_id file) const noexcept;

    // Precondition: contains(file) == true. The view remains valid until File
    // Context mutates its path arena.
    [[nodiscard]] file_path_view path(
        file_id file) const noexcept;

    [[nodiscard]] std::span<const file_root> roots() const noexcept {
        return root_files;
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
};

}
