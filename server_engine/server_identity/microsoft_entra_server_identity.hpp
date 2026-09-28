/*
 * Platform boundary for Microsoft Entra confidential-client authentication.
 */
#pragma once

#include "../configuration/server_configuration.hpp"
#include "../server_status.hpp"

#include <chrono>
#include <string>

namespace cw::server {

struct microsoft_entra_server_identity_token final {
    std::string access_token;
    std::chrono::system_clock::time_point expires_at{};
};

[[nodiscard]] server_status acquire_microsoft_entra_server_identity(
    const microsoft_entra_server_identity_configuration& configuration,
    std::chrono::system_clock::time_point now,
    microsoft_entra_server_identity_token& output,
    std::string& detail) noexcept;

}
