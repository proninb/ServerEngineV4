/*
 * Resident Project state.
 *
 * Phase 1 resident state owns the immutable compiled.bin mapping and its
 * mmap-native compiled_project_view. Runtime and SHM are added in Phase 2;
 * construction-mode state never enters this class.
 */
#pragma once

#include "persistence/compiled_project.hpp"
#include "../read_only_file_mapping.hpp"

#include <filesystem>

namespace cw::server {

// Resident state published only after a Project lifecycle mode succeeds.
class project final {
public:
    project(
        std::filesystem::path path,
        read_only_file_mapping&& compiled_mapping,
        compiled_project_view compiled);

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return project_path;
    }

    [[nodiscard]] const compiled_project_view& compiled() const noexcept {
        return compiled_view;
    }

private:
    std::filesystem::path project_path;
    read_only_file_mapping compiled_mapping;
    compiled_project_view compiled_view;
};

}
