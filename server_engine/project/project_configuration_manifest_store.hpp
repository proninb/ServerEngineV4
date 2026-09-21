/*
 * Persisted composed Project configuration manifest boundary.
 *
 * This store owns only configuration-input proof. It does not own Graph,
 * File Context, frontend state, Runtime, or resident Project state.
 */
#pragma once

#include "project_configuration_manifest.hpp"

#include <cstdint>
#include <filesystem>

namespace cw::server {

enum class project_configuration_manifest_store_result : std::uint8_t {
    success,
    not_found,
    invalid,
    io_failed,
};

class project_configuration_manifest_store final {
public:
    project_configuration_manifest_store(
        const std::filesystem::path& root_project_path,
        const std::filesystem::path& manifest_file);

    [[nodiscard]]
    project_configuration_manifest_store_result load(
        project_configuration_manifest& output) const noexcept;

    [[nodiscard]]
    project_configuration_manifest_store_result save(
        const project_configuration_manifest& value) const noexcept;

    [[nodiscard]]
    const std::filesystem::path& path() const noexcept {
        return manifest_path;
    }

private:
    std::filesystem::path manifest_path;
};

}
