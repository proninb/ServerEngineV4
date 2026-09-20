/*
 * Construction lexical generation.
 *
 * Builds one compact lexical_stream per physical Header/Source file_id.
 * File acquisition and lexing run in parallel from immutable inputs; File
 * Context publication and lexical-stream publication remain single-owner.
 */
#pragma once

#include "lexical_stream.hpp"
#include "../file/file_context.hpp"
#include "../../server_status.hpp"

#include <vector>

namespace cw::server {

[[nodiscard]] server_status build_lexical_generation(
    file_context& files,
    std::vector<lexical_stream>& output) noexcept;

}
