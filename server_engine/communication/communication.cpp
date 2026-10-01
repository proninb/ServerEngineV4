/*
 * Communication endpoint construction/destruction.
 *
 * Endpoint startup is transactional at this layer: if creation of any endpoint
 * fails, endpoints already started by this call are stopped before returning.
 */
#include "communication.hpp"

#include <algorithm>
#include <string>

namespace cw::server {

// Materializes exactly the endpoint set declared by server.json.
server_status communication::start(
    const communication_configuration& configuration,
    request_queue& requests,
    server_mode mode,
    std::uint32_t max_connections) {

    // Make repeated start() calls deterministic by discarding prior endpoint state.
    stop();

    const auto effective_limit =
        mode == server_mode::demo
            ? 1u
            : max_connections;

    tcp_gate.reset(effective_limit);
    next_session_id.store(1, std::memory_order_release);

    const auto demo_lifetime =
        mode == server_mode::demo
            ? std::chrono::minutes{5}
            : std::chrono::steady_clock::duration::zero();

    for (const auto& endpoint : configuration.endpoints) {
        if (endpoint.transport == transport_kind::tcp) {
            if (endpoint.protocol != "json") {
                stop();
                return server_status::unsupported;
            }

            auto tcp =
                std::make_unique<tcp_endpoint>();

            if (!tcp->start(
                    endpoint,
                    requests,
                    tcp_gate,
                    next_session_id,
                    demo_lifetime)) {

                stop();
                return server_status::communication_start_failed;
            }

            tcp_endpoints.push_back(
                std::move(tcp));
            continue;
        }

        auto console = std::make_unique<server_console>();

        if (!console->start(requests)) {
            stop();
            return server_status::communication_start_failed;
        }

        consoles.push_back(std::move(console));
    }

    // Until another backend exists, a running Server needs at least one usable
    // command ingress path.
    if (consoles.empty() &&
        tcp_endpoints.empty()) {

        return server_status::communication_start_failed;
    }

    return server_status::success;
}

// Stops endpoints before destroying their owning objects.
void communication::stop() noexcept {
    for (auto& console : consoles) {
        console->stop();
    }

    for (auto& entry : connections) {
        if (entry.connection == nullptr) {
            continue;
        }

        (void)entry.connection->begin_close();
        entry.connection->teardown_subscriptions();
    }

    connections.clear();

    for (auto& tcp : tcp_endpoints) {
        tcp->stop();
    }

    tcp_endpoints.clear();
    consoles.clear();
}

bool communication::register_connection(
    communication_connection& connection) {

    const auto duplicate =
        std::find_if(
            connections.begin(),
            connections.end(),
            [&connection](const registered_connection& current) {
                return current.connection == &connection ||
                    (current.connection != nullptr &&
                     current.connection->id() == connection.id());
            });

    if (duplicate != connections.end()) {
        return false;
    }

    try {
        connections.push_back({
            &connection,
            connection.hold(),
        });
    }
    catch (...) {
        return false;
    }

    return true;
}

void communication::unregister_connection(
    client_session_id id) noexcept {

    const auto position =
        std::find_if(
            connections.begin(),
            connections.end(),
            [id](const registered_connection& entry) {
                return entry.connection != nullptr &&
                    entry.connection->id() == id;
            });

    if (position == connections.end()) {
        return;
    }

    position->connection->teardown_subscriptions();
    connections.erase(position);
}

void communication::publish_server_state(
    project_state old_state,
    project_state new_state) {

    if (old_state == new_state) {
        return;
    }

    for (auto& entry : connections) {
        auto* connection =
            entry.connection;

        if (connection != nullptr &&
            connection->logged_in() &&
            !connection->closing()) {

            (void)connection->enqueue_server_state(
                old_state,
                new_state);
        }
    }
}

std::size_t communication::route_client(
    const communication_connection& sender,
    const client_message& message) {

    if (!sender.logged_in() || sender.closing()) {
        return 0;
    }

    std::size_t routed = 0;
    const std::string from(sender.login_name());

    for (auto& entry : connections) {
        auto* connection =
            entry.connection;

        if (connection == nullptr ||
            !connection->logged_in() ||
            connection->closing()) {

            continue;
        }

        if (!message.login.empty() &&
            connection->login_name() != message.login) {
            continue;
        }

        client_action action;
        action.from = from;
        action.arg = message.arg;
        action.parameters = message.parameters;

        if (connection->enqueue_client_action(
                std::move(action))) {
            ++routed;
        }
    }

    return routed;
}

}
