#include "server_identity.hpp"

#include "microsoft_entra_server_identity.hpp"

#include <utility>

namespace cw::server {

server_status server_identity_service::start(
    const server_identity_configuration& configuration,
    std::chrono::system_clock::time_point now) noexcept {

    stop();

    switch (configuration.mode) {
    case server_identity_mode::none:
        active_mode = server_identity_mode::none;
        initialized = true;
        return server_status::success;

    case server_identity_mode::microsoft_entra:
        if (!configuration.microsoft_entra) {
            failure_detail =
                "Microsoft Entra Server identity configuration is missing";
            return server_status::server_identity_start_failed;
        }

        {
            microsoft_entra_server_identity_token acquired;

            const auto status =
                acquire_microsoft_entra_server_identity(
                    *configuration.microsoft_entra,
                    now,
                    acquired,
                    failure_detail);

            if (!succeeded(status)) {
                return status;
            }

            token =
                std::move(acquired.access_token);

            expiration =
                acquired.expires_at;

            active_mode =
                server_identity_mode::microsoft_entra;

            initialized = true;
            return server_status::success;
        }
    }

    failure_detail =
        "Configured Server identity mode is unsupported";

    return server_status::unsupported;
}

void server_identity_service::stop() noexcept {
    token.clear();
    expiration = {};
    failure_detail.clear();
    active_mode = server_identity_mode::none;
    initialized = false;
}

bool server_identity_service::ready() const noexcept {
    return initialized;
}

server_identity_mode server_identity_service::mode() const noexcept {
    return active_mode;
}

std::string_view server_identity_service::access_token() const noexcept {
    return token;
}

std::chrono::system_clock::time_point
server_identity_service::expires_at() const noexcept {
    return expiration;
}

bool server_identity_service::token_valid_at(
    std::chrono::system_clock::time_point now) const noexcept {

    if (!initialized) {
        return false;
    }

    if (active_mode ==
        server_identity_mode::none) {

        return true;
    }

    return !token.empty() &&
           now < expiration;
}

std::string_view server_identity_service::detail() const noexcept {
    return failure_detail;
}

}
