#include "microsoft_entra_server_identity.hpp"

namespace cw::server {

server_status acquire_microsoft_entra_server_identity(
    const microsoft_entra_server_identity_configuration&,
    std::chrono::system_clock::time_point,
    microsoft_entra_server_identity_token& output,
    std::string& detail) noexcept {

    output = {};
    detail =
        "Microsoft Entra Server identity certificate authentication is not implemented on this platform";

    return server_status::unsupported;
}

}
