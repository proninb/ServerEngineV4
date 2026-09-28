/*
 * server.license loading and validation boundary.
 *
 * V1 validates structure, UTC expiration, and connection limits. Cryptographic
 * license authenticity is intentionally a later slice and is not claimed here.
 */
#pragma once

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"
#include "server_license.hpp"

#include <chrono>
#include <filesystem>

namespace cw::server {

[[nodiscard]] server_status load_server_license(
    const std::filesystem::path& path,
    std::chrono::system_clock::time_point now,
    operation_id operation,
    diagnostic_collection& diagnostics,
    server_license& output);

}
