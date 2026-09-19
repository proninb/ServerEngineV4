/*
 * Project construction file context.
 *
 * file_context owns physical file identity and root parser roles for one
 * candidate construction. Type/Source parser state, Graph, Runtime, and
 * resident Project state remain outside this class.
 */
#pragma once

#include "../project_path.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace cw::server {

// Dense generation-local identity of one physical Project input file.
class file_id final {
public:
    constexpr file_id() noexcept = default;

    explicit constexpr file_id(std::uint32_t value) noexcept
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

enum class file_role : std::uint8_t {
    type,
    source,
};

struct file_root final {
    file_id file{};
    file_role role = file_role::type;
};

// Mutable physical-file identity state for one candidate construction. The
// candidate construction owner is the rollback boundary, so File Context has
// no nested update transaction.
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

    [[nodiscard]] bool contains(file_id file) const noexcept;

    // Precondition: contains(file) == true.
    [[nodiscard]] const std::filesystem::path& path(
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
        std::filesystem::path path;
        project_path_key key;
        root_role role = root_role::none;
    };

    struct path_slot final {
        std::uint32_t fingerprint = 0;
        file_id file{};
    };

    [[nodiscard]] static std::uint32_t fingerprint(
        const project_path_key& key) noexcept;

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
    std::vector<path_slot> path_index;
    std::vector<file_root> root_files;
};

}
