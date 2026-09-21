/*
 * Persisted BUILD database image boundary.
 *
 * database.bin is sectioned and canonical BUILD acceleration/lineage state.
 * Current sections preserve string_id/identity_ref continuity and retained
 * lexical state without becoming a second semantic representation of Project.
 */
#pragma once

#include "project_artifact.hpp"
#include "../file/file_context.hpp"
#include "../frontend/lexical_generation.hpp"
#include "../semantic/identity.hpp"
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
    const identity_space& identities,
    const lexical_generation& lexical,
    project_artifact_image& output) noexcept;

[[nodiscard]] database_image_result validate_database_image(
    std::span<const std::byte> image) noexcept;

}

