/*
 * Persisted BUILD database image boundary.
 *
 * database.bin is sectioned and canonical. The initial persisted sections own
 * string_id lineage and retained lexical facts in file_id order. Semantic and
 * Builder sections extend this persistence boundary without exposing temporary
 * construction layouts.
 */
#pragma once

#include "project_artifact.hpp"
#include "../file/file_context.hpp"
#include "../frontend/lexical_generation.hpp"
#include "../string/string_table.hpp"

#include <cstdint>
#include <span>

namespace cw::server {

enum class database_image_result : std::uint8_t {
    success,
    invalid_state,
    invalid_image,
    failed,
};

[[nodiscard]] database_image_result build_database_image(
    const file_context& files,
    const string_table& strings,
    const lexical_generation& lexical,
    project_artifact_image& output) noexcept;

[[nodiscard]] database_image_result validate_database_image(
    std::span<const std::byte> image) noexcept;

}

