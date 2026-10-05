/*
 * Resident Project state.
 *
 * Resident state owns immutable compiled.bin, final Runtime/SHM state, and
 * resolved references to Project runtime resources loaded after Runtime
 * publication. Construction-only state never enters this class.
 */
#pragma once

#include "persistence/compiled_project.hpp"
#include "ic/ic_catalog.hpp"
#include "runtime/runtime_binding.hpp"
#include "runtime/runtime_system.hpp"
#include "../fixed_shared_memory.hpp"
#include "../read_only_file_mapping.hpp"

#include <cstdint>
#include <filesystem>
#include <utility>

namespace cw::server {

class project final {
public:
    project(
        std::filesystem::path path,
        read_only_file_mapping&& compiled_mapping,
        compiled_project_view compiled,
        fixed_shared_memory&& shared_memory,
        runtime_binding_index&& runtime_bindings,
        std::uint64_t runtime_size);

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return project_path;
    }

    [[nodiscard]] const compiled_project_view& compiled() const noexcept {
        return compiled_view;
    }

    [[nodiscard]] fixed_shared_memory& shm() noexcept {
        return shared_memory;
    }

    [[nodiscard]] const fixed_shared_memory& shm() const noexcept {
        return shared_memory;
    }

    [[nodiscard]] std::uint64_t runtime_size() const noexcept {
        return runtime_size_value;
    }

    [[nodiscard]] runtime_system& system() noexcept {
        return *reinterpret_cast<runtime_system*>(
            shared_memory.data() +
                static_cast<std::size_t>(
                    runtime_system_offset));
    }

    [[nodiscard]] const runtime_system& system() const noexcept {
        return *reinterpret_cast<const runtime_system*>(
            shared_memory.data() +
                static_cast<std::size_t>(
                    runtime_system_offset));
    }

    [[nodiscard]] const runtime_binding_index&
    runtime_bindings() const noexcept {
        return runtime_bindings_value;
    }

    void set_ic_catalog_path(std::filesystem::path path) {
        ic_catalog_path_value = std::move(path);
    }

    [[nodiscard]] const std::filesystem::path&
    ic_catalog_path() const noexcept {
        return ic_catalog_path_value;
    }

    void set_ic_catalog(ic_catalog&& value) {
        ic_catalog_value =
            std::move(value);
    }

    [[nodiscard]] const ic_catalog&
    ic_catalog_data() const noexcept {
        return ic_catalog_value;
    }

private:
    std::filesystem::path project_path;
    read_only_file_mapping compiled_mapping;
    compiled_project_view compiled_view;
    fixed_shared_memory shared_memory;
    runtime_binding_index runtime_bindings_value;
    std::uint64_t runtime_size_value = 0;
    std::filesystem::path ic_catalog_path_value;
    ic_catalog ic_catalog_value;
};

}
