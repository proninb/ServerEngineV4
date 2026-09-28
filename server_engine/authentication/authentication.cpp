/*
 * Server-level Authentication subsystem lifecycle.
 */
#include "authentication.hpp"

namespace cw::server {

server_status authentication_service::start(
    const authentication_configuration& configuration,
    const std::filesystem::path& configuration_directory) noexcept {

    stop();

    switch (configuration.mode) {
    case authentication_mode::none:
        active_mode = authentication_mode::none;
        initialized = true;
        return server_status::success;

    case authentication_mode::contract:
        failure_detail =
            "Configured Authentication Contract provider is not implemented";
        return server_status::unsupported;

    case authentication_mode::external:
        if (!configuration.external) {
            failure_detail =
                "External Authentication configuration is missing";
            return server_status::authentication_start_failed;
        }

        switch (configuration.external->provider) {
        case external_authentication_provider::microsoft_entra: {
            const auto status =
                entra.start(
                    *configuration.external,
                    configuration_directory,
                    failure_detail);

            if (!succeeded(status)) {
                return status;
            }

            active_mode =
                authentication_mode::external;
            initialized = true;
            return server_status::success;
        }
        }

        failure_detail =
            "Configured external Authentication provider is unsupported";
        return server_status::unsupported;
    }

    return server_status::authentication_start_failed;
}

void authentication_service::stop() noexcept {
    entra.stop();
    failure_detail.clear();
    initialized = false;
    active_mode = authentication_mode::none;
}

bool authentication_service::ready() const noexcept {
    return initialized;
}

authentication_mode authentication_service::mode() const noexcept {
    return active_mode;
}

std::string_view authentication_service::detail() const noexcept {
    return failure_detail;
}

const microsoft_entra_authentication&
authentication_service::microsoft_entra() const noexcept {

    return entra;
}

}
