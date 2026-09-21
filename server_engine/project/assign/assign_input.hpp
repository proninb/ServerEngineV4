/*
 * Construction Assign input boundary.
 *
 * This stage owns only exact-byte materialization for file_kind::assign.
 * Assign grammar, variable resolution, Graph facts, and dependency emission
 * remain later semantic work.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../../server_status.hpp"

namespace cw::server {

[[nodiscard]] server_status materialize_assign_inputs(
    file_context& files) noexcept;

}
