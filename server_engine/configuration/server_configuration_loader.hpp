/*
 * Server configuration loading boundary.
 *
 * Generic JSON parsing remains schema-agnostic. This layer owns server.json
 * schema and emits failures into the caller-owned operation diagnostics.
 */
#pragma once

#include "server_configuration.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>

namespace cw::server {

// Loads and validates server.json.
//
// The caller owns diagnostics for the entire operation. This function never
// creates a nested diagnostic_collection, preventing diagnostic loss.
[[nodiscard]] server_status load_server_configuration(
    const std::filesystem::path& path,
    operation_id operation,
    diagnostic_collection& diagnostics,
    server_configuration& configuration);

}
