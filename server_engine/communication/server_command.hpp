/*
 * Internal transport-neutral Server command representation.
 *
 * External transports must translate their syntax into this representation
 * before lifecycle execution. Transport code never owns lifecycle behavior.
 */
#pragma once

#include <filesystem>

namespace cw::server {

// Server lifecycle commands currently accepted by the control layer.
enum class server_command_kind {
    // Load one Project configuration while the Server is UNLOADED.
    load,

    // Destroy the currently active Project and return to UNLOADED.
    unload,

    // Stop communication, release Project ownership, and exit server.run().
    shutdown,
};

// Fully parsed command published by a communication endpoint.
struct server_command {
    // Operation selected by the external command.
    server_command_kind kind = server_command_kind::shutdown;

    // Command-specific Project path used by LOAD; empty for other commands.
    std::filesystem::path path;
};

}
