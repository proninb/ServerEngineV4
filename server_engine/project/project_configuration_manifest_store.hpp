/*
 * Persisted composed Project configuration manifest boundary.
 *
 * This store owns only configuration-input proof. It does not own Graph,
 * File Context, frontend state, Runtime, or resident Project state.
 */
#pragma once

#include "project_configuration_manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace cw::server {

enum class project_configuration_manifest_store_result : std::uint8_t {
    success,
    not_found,
    invalid,
    io_failed,
};

// Exact-size preparation state for direct project.manifest encoding.
class project_configuration_manifest_layout final {
public:
    [[nodiscard]] std::size_t size() const noexcept {
        return size_value;
    }

private:
    std::size_t size_value = 0;
    std::uint32_t file_count = 0;
    project_configuration_hash configuration_hash{};

    friend project_configuration_manifest_store_result
    prepare_project_configuration_manifest_layout(
        const project_configuration_manifest&,
        project_configuration_manifest_layout&) noexcept;

    friend project_configuration_manifest_store_result
    encode_project_configuration_manifest(
        const project_configuration_manifest&,
        const project_configuration_manifest_layout&,
        std::span<std::byte>) noexcept;
};

// Computes the exact final image size and validates manifest invariants without
// constructing a full serialized image.
[[nodiscard]] project_configuration_manifest_store_result
prepare_project_configuration_manifest_layout(
    const project_configuration_manifest& value,
    project_configuration_manifest_layout& output) noexcept;

// Encodes directly into caller-owned bytes, including writable mmap pages.
// layout must come from prepare_project_configuration_manifest_layout() for the
// same unchanged manifest.
[[nodiscard]] project_configuration_manifest_store_result
encode_project_configuration_manifest(
    const project_configuration_manifest& value,
    const project_configuration_manifest_layout& layout,
    std::span<std::byte> output) noexcept;

// Decodes directly from mapped/borrowed bytes. No whole-image copy is made.
[[nodiscard]] project_configuration_manifest_store_result
decode_project_configuration_manifest(
    std::span<const std::byte> image,
    project_configuration_manifest& output) noexcept;

// Read-only explicit-path adapter over the direct span codec.
class project_configuration_manifest_store final {
public:
    explicit project_configuration_manifest_store(
        std::filesystem::path path);

    [[nodiscard]]
    project_configuration_manifest_store_result load(
        project_configuration_manifest& output) const noexcept;

    [[nodiscard]]
    const std::filesystem::path& path() const noexcept {
        return manifest_path;
    }

private:
    std::filesystem::path manifest_path;
};

}
