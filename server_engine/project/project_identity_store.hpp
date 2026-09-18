/*
 * Persisted Project identity boundary used by BUILD decision logic.
 *
 * The store owns only Project input proof. It does not own Graph, Source Manager,
 * frontend state, or any generic Project persistence abstraction.
 */
#pragma once

#include "project_identity.hpp"

#include <cstdint>
#include <filesystem>

namespace cw::server {

enum class project_identity_store_result : std::uint8_t {
    success,
    not_found,
    invalid,
    io_failed,
};

// Stores one fixed-size, versioned, self-checking Project identity artifact.
class project_identity_store final {
public:
    explicit project_identity_store(
        const std::filesystem::path& project_path);

    [[nodiscard]] project_identity_store_result load(
        persisted_project_identity& output) const noexcept;

    [[nodiscard]] project_identity_store_result save(
        const persisted_project_identity& identity) const noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return identity_path;
    }

private:
    std::filesystem::path identity_path;
};

}
