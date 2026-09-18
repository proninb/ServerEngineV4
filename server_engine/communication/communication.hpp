/*
 * Communication endpoint owner.
 *
 * The object materializes exactly the endpoints declared in server.json.
 * Endpoints are owned for the lifetime of the active Server configuration.
 */
#pragma once

#include "../configuration/server_configuration.hpp"
#include "../server_status.hpp"
#include "command_queue.hpp"
#include "console/server_console.hpp"

#include <memory>
#include <vector>

namespace cw::server {

// Owns all communication endpoint instances created for one Server.
class communication final {
public:
    // Creates and starts each configured endpoint.
    // Startup is fail-closed: failure stops endpoints already created in this call.
    [[nodiscard]] server_status start(
        const communication_configuration& configuration,
        command_queue& commands);

    // Stops every owned endpoint and releases all communication resources.
    void stop() noexcept;

private:
    // Owned console endpoints. Empty when no console transport is configured.
    // Future transport types receive their own owned collections/backends.
    std::vector<std::unique_ptr<server_console>> consoles;
};

}
