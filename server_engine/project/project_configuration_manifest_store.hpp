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
#include <span>
#include <vector>

namespace cw::server {

enum class project_configuration_manifest_store_result : std::uint8_t {
    success,
    not_found,
    invalid,
    io_failed,
};

[[nodiscard]] project_configuration_manifest_store_result
encode_project_configuration_manifest(
    const project_configuration_manifest& value,
    std::vector<std::byte>& output) noexcept;

[[nodiscard]] project_configuration_manifest_store_result
decode_project_configuration_manifest(
    std::span<const std::byte> image,
    project_configuration_manifest& output) noexcept;

// Thin explicit-path I/O adapter. Artifact slot selection belongs to the
// persistence owner, not to this configuration-proof codec.
class project_configuration_manifest_store final {
public:
    explicit project_configuration_manifest_store(
        std::filesystem::path path);

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
