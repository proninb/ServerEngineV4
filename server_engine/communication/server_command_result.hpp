/*
 * Result of one externally visible Server command.
 */
#pragma once

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

namespace cw::server {

// Complete result of one command execution.
struct server_command_result {
    // Process-local operation identity.
    operation_id operation;

    // Control-flow result.
    server_status status = server_status::success;

    // Diagnostics produced by this command only.
    diagnostic_collection diagnostics;
};

}
