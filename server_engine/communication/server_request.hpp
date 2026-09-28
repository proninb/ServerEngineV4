/*
 * Internal transport-neutral Server request representation.
 *
 * External transports translate their syntax/protocol into this representation
 * before Server execution. Transport code never owns Server semantics.
 */
#pragma once

#include <filesystem>
#include <string>

namespace cw::server {

// Server request kinds currently accepted by the control layer.
enum class server_request_kind {
    // Restore one persisted Project while the Server is UNLOADED.
    load,

    // Compile one Project from source and persist only the final compiled artifact.
    publish,

    // Incrementally construct one Project from persisted BUILD state while UNLOADED.
    build,

    // Destroy the currently active Project and return to UNLOADED.
    unload,

    // Full source construction plus fresh BUILD acceleration while UNLOADED.
    rebuild,

    // Read the current observable Server/Project state.
    get_state,

    // Read one scalar value from the committed Runtime view.
    get_value,

    // Stop communication, release Project ownership, and exit server.run().
    shutdown,
};

// Fully parsed transport-neutral request published by a communication endpoint.
struct server_request {
    // Operation selected by the external request.
    server_request_kind kind = server_request_kind::shutdown;

    // Project entry path used by LOAD/PUBLISH/BUILD/REBUILD.
    std::filesystem::path path;

    // Query target used by GET_VALUE.
    std::string name;
};

}
