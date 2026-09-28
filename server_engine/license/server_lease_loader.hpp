#pragma once

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"
#include "server_lease.hpp"

#include <chrono>
#include <filesystem>

namespace cw::server {

[[nodiscard]] server_status load_server_lease(
    const std::filesystem::path& path,
    std::chrono::system_clock::time_point now,
    const server_license& license,
    operation_id operation,
    diagnostic_collection& diagnostics,
    server_lease& output);

}
