/*
 * Process-level Server license state.
 *
 * server.license is independent from Project state and Authentication.
 * V1 carries startup validity and the maximum active TCP connection count.
 */
#pragma once

#include <chrono>
#include <cstdint>

namespace cw::server {

struct server_limits final {
    std::uint32_t max_connections = 0;
};

class server_license final {
public:
    void clear() noexcept;

    void assign(
        std::chrono::system_clock::time_point expires_at,
        server_limits limits) noexcept;

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] std::chrono::system_clock::time_point expires_at() const noexcept;
    [[nodiscard]] const server_limits& limits() const noexcept;

private:
    std::chrono::system_clock::time_point expiration{};
    server_limits server_limits_value;
    bool loaded_value = false;
};

}
