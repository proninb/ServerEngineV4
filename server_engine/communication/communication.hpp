/*
 * Communication endpoint owner.
 *
 * The object materializes exactly the endpoints declared in server.json.
 * Endpoints are owned for the lifetime of the active Server configuration.
 */
#pragma once

#include "../configuration/server_configuration.hpp"
#include "../server_status.hpp"
#include "request_queue.hpp"
#include "connection/communication_connection.hpp"
#include "console/server_console.hpp"
#include "tcp/tcp_endpoint.hpp"
#include "../server_mode.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
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
        request_queue& requests,
        server_mode mode,
        std::uint32_t max_connections);

    // Stops every owned endpoint and releases all communication resources.
    void stop() noexcept;

    // Server-control-thread-only active logged-in connection registry.
    [[nodiscard]] bool register_connection(communication_connection& connection);
    void unregister_connection(client_session_id id) noexcept;

    void publish_server_state(project_state old_state, project_state new_state);

    // Routes to all matching logged-in sessions. Empty login broadcasts to all.
    [[nodiscard]] std::size_t route_client(
        const communication_connection& sender,
        const client_message& message);

private:
    // Owned console endpoints. Empty when no console transport is configured.
    // Future transport types receive their own owned collections/backends.
    std::vector<std::unique_ptr<server_console>> consoles;

    std::vector<std::unique_ptr<tcp_endpoint>> tcp_endpoints;
    tcp_connection_gate tcp_gate;
    std::atomic_uint64_t next_session_id = 1;

    struct registered_connection final {
        communication_connection* connection = nullptr;
        connection_lifetime_token lifetime;
    };

    // The registry owns one lifetime token for every logged-in connection.
    // Transport ownership and queued control items are independent lifetimes.
    std::vector<registered_connection> connections;
};

}
