/*
 * Current Project ownership boundary.
 *
 * This bootstrap type establishes LOAD/UNLOAD ownership semantics before
 * Project parsing, build, Graph, Runtime, and persistence are added.
 */
#pragma once

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>

namespace cw::server {

// Represents the one Project currently owned by a Server.
class project final {
public:
    // Verifies bootstrap accessibility of the requested Project configuration.
    // Failures are appended to the caller-owned operation diagnostics.
    [[nodiscard]] server_status load(
        const std::filesystem::path& configuration_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    // Returns the resolved Project configuration path associated with this object.
    [[nodiscard]] const std::filesystem::path& configuration_path() const noexcept {
        return path;
    }

private:
    // Resolved path established only after successful load().
    std::filesystem::path path;
};

}
