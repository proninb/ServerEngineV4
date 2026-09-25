/*
 * Resident Project state.
 *
 * Resident state owns the immutable compiled.bin mapping plus final Project
 * SHM. Runtime layout construction state never enters this class.
 */
#pragma once

#include "persistence/compiled_project.hpp"
#include "../fixed_shared_memory.hpp"
#include "../read_only_file_mapping.hpp"

#include <cstdint>
#include <filesystem>

namespace cw::server {

// Resident state published only after a Project lifecycle mode succeeds.
class project final {
public:
    project(
        std::filesystem::path path,
        read_only_file_mapping&& compiled_mapping,
        compiled_project_view compiled,
        fixed_shared_memory&& shared_memory,
        std::uint64_t runtime_size);

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return project_path;
    }

    [[nodiscard]] const compiled_project_view& compiled() const noexcept {
        return compiled_view;
    }

    [[nodiscard]] const fixed_shared_memory& shm() const noexcept {
        return shared_memory;
    }

    [[nodiscard]] std::uint64_t runtime_size() const noexcept {
        return runtime_size_value;
    }

private:
    std::filesystem::path project_path;
    read_only_file_mapping compiled_mapping;
    compiled_project_view compiled_view;
    fixed_shared_memory shared_memory;
    std::uint64_t runtime_size_value = 0;
};

}
