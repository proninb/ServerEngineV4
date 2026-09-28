/*
 * Server process identity used for outbound service-to-service authentication.
 *
 * This identity belongs to the Server process, not to Client Sessions and not
 * to the resident Project. Access tokens are process-memory state only.
 */
#pragma once

#include "../configuration/server_configuration.hpp"
#include "../server_status.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace cw::server {

class server_identity_service final {
public:
    [[nodiscard]] server_status start(
        const server_identity_configuration& configuration,
        std::chrono::system_clock::time_point now) noexcept;

    void stop() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] server_identity_mode mode() const noexcept;
    [[nodiscard]] std::string_view access_token() const noexcept;
    [[nodiscard]] std::chrono::system_clock::time_point expires_at() const noexcept;
    [[nodiscard]] bool token_valid_at(
        std::chrono::system_clock::time_point now) const noexcept;
    [[nodiscard]] std::string_view detail() const noexcept;

private:
    server_identity_mode active_mode = server_identity_mode::none;
    std::string token;
    std::chrono::system_clock::time_point expiration{};
    std::string failure_detail;
    bool initialized = false;
};

}
