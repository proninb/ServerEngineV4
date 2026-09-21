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
    // Restore one persisted Project while the Server is UNLOADED.
    load,

    // Incrementally construct one Project from its persisted baseline while UNLOADED.
    build,

    // Destroy the currently active Project and return to UNLOADED.
    unload,

    // Construct a new G0 while the Server is UNLOADED.
    rebuild,

    // Stop communication, release Project ownership, and exit server.run().
    shutdown,
};

// Fully parsed command published by a communication endpoint.
struct server_command {
    // Operation selected by the external command.
    server_command_kind kind = server_command_kind::shutdown;

    // Project entry path used by LOAD/BUILD/REBUILD; empty for UNLOAD/SHUTDOWN.
    std::filesystem::path path;
};

}
