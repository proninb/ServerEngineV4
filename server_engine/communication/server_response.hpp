/*
 * Transport-neutral response produced for one Server request.
 */
#pragma once

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

namespace cw::server {

// Common response envelope for one request execution.
struct server_response {
    // Process-local operation identity.
    operation_id operation;

    // Control-flow result.
    server_status status = server_status::success;

    // Diagnostics produced by this command only.
    diagnostic_collection diagnostics;
};

}
