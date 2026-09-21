/*
 * Persisted SourceSave image boundary.
 *
 * SourceSave is BUILD acceleration state for physical construction inputs:
 * file_id lineage, UTF-8 physical paths, file kind, exact-content proof, native
 * change-token proof, and finalized direct forward/reverse dependency topology.
 * It contains no Parser, Semantic, Graph, Runtime, or SHM state.
 */
#pragma once

#include "project_artifact.hpp"
#include "../file/file_context.hpp"

#include <cstdint>
#include <span>

namespace cw::server {

enum class source_save_result : std::uint8_t {
    success,
    invalid_state,
    invalid_image,
    failed,
};

[[nodiscard]] source_save_result build_source_save_image(
    const file_context& files,
    project_artifact_image& output) noexcept;

[[nodiscard]] source_save_result validate_source_save_image(
    std::span<const std::byte> image) noexcept;

}

