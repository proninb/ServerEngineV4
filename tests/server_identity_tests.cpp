#include "server_identity/server_identity.hpp"

#include <chrono>
#include <iostream>

int main() {
    using namespace cw::server;

    server_identity_configuration configuration;
    configuration.mode =
        server_identity_mode::none;

    server_identity_service identity;

    const auto now =
        std::chrono::system_clock::now();

    const auto status =
        identity.start(
            configuration,
            now);

    if (!succeeded(status) ||
        !identity.ready() ||
        identity.mode() !=
            server_identity_mode::none ||
        !identity.token_valid_at(now) ||
        !identity.access_token().empty()) {

        std::cerr
            << "mode=none Server identity failed\n";
        return 1;
    }

    identity.stop();

    if (identity.ready()) {
        std::cerr
            << "Server identity stop failed\n";
        return 1;
    }

    return 0;
}
