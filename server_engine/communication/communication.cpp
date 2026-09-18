/*
 * Communication endpoint construction/destruction.
 *
 * Endpoint startup is transactional at this layer: if creation of any endpoint
 * fails, endpoints already started by this call are stopped before returning.
 */
#include "communication.hpp"

namespace cw::server {

// Materializes exactly the endpoint set declared by server.json.
server_status communication::start(
    const communication_configuration& configuration,
    command_queue& commands) {

    // Make repeated start() calls deterministic by discarding prior endpoint state.
    stop();

    for (const auto& endpoint : configuration.endpoints) {
        // TCP belongs to the configuration contract but its backend is not part
        // of this V4 step. Fail instead of silently dropping the endpoint.
        if (endpoint.transport == transport_kind::tcp) {
            stop();
            return server_status::unsupported;
        }

        // console is currently the only implemented endpoint type.
        auto console = std::make_unique<server_console>();

        if (!console->start(commands)) {
            stop();
            return server_status::communication_start_failed;
        }

        consoles.push_back(std::move(console));
    }

    // Until another backend exists, a running Server needs at least one usable
    // command ingress path.
    if (consoles.empty()) {
        return server_status::communication_start_failed;
    }

    return server_status::success;
}

// Stops endpoints before destroying their owning objects.
void communication::stop() noexcept {
    for (auto& console : consoles) {
        console->stop();
    }

    consoles.clear();
}

}
