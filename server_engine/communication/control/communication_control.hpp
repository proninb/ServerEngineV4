/*
 * Server-control-thread messages produced by Communication.
 *
 * LOGIN, peer close, and CLIENT routing are Communication/session semantics,
 * not server_request_kind values. They are serialized with Server state
 * transitions by the same control thread.
 */
#pragma once

#include "../connection/communication_connection.hpp"
#include "../protocol/protocol_contract.hpp"
#include "../server_request_message.hpp"

#include <string>
#include <variant>

namespace cw::server {

struct login_control_message final {
    communication_connection* connection = nullptr;
    connection_lifetime_token lifetime;
    request_id request;
    std::string name;
};

struct connection_close_control_message final {
    communication_connection* connection = nullptr;
    connection_lifetime_token lifetime;
};

struct client_control_message final {
    communication_connection* connection = nullptr;
    connection_lifetime_token lifetime;
    request_id request;
    client_message message;
};

using communication_control_message =
    std::variant<
        server_request_message,
        login_control_message,
        connection_close_control_message,
        client_control_message>;

}
