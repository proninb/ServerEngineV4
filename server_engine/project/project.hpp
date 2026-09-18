/*
 * Resident Project state.
 *
 * project owns only data required while one Project is active. Graph, Runtime,
 * and SHM will be added here as their resident representations are implemented.
 * Construction-mode state must remain outside this class.
 */
#pragma once

#include <filesystem>

namespace cw::server {

// Resident state published only after a Project lifecycle mode succeeds.
class project final {
public:
    explicit project(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return project_path;
    }

private:
    std::filesystem::path project_path;
};

}
