/*
 * Server-level Authentication subsystem lifecycle.
 */
#include "authentication.hpp"

namespace cw::server {

server_status authentication_service::start(
    const authentication_configuration& configuration) noexcept {

    stop();

    switch (configuration.mode) {
    case authentication_mode::none:
        active_mode = authentication_mode::none;
        initialized = true;
        return server_status::success;

    case authentication_mode::contract:
    case authentication_mode::external:
        // Provider contracts are intentionally deferred to the next slice.
        return server_status::unsupported;
    }

    return server_status::authentication_start_failed;
}

void authentication_service::stop() noexcept {
    initialized = false;
    active_mode = authentication_mode::none;
}

bool authentication_service::ready() const noexcept {
    return initialized;
}

authentication_mode authentication_service::mode() const noexcept {
    return active_mode;
}

}
