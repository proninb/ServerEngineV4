/*
 * Short-lived process-level Server runtime authorization.
 *
 * A lease is independent from Project identity and narrows the longer-lived
 * server.license entitlement.
 */
#pragma once

#include "server_license.hpp"

#include <chrono>
#include <string>

namespace cw::server {

class server_lease final {
public:
    void clear() noexcept;

    void assign(
        std::string lease_id,
        std::chrono::system_clock::time_point not_before,
        std::chrono::system_clock::time_point expires_at,
        server_limits limits);

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] const std::string& lease_id() const noexcept;
    [[nodiscard]] std::chrono::system_clock::time_point not_before() const noexcept;
    [[nodiscard]] std::chrono::system_clock::time_point expires_at() const noexcept;
    [[nodiscard]] const server_limits& limits() const noexcept;

    [[nodiscard]] bool valid_at(
        std::chrono::system_clock::time_point now) const noexcept;

private:
    std::string lease_id_value;
    std::chrono::system_clock::time_point not_before_value{};
    std::chrono::system_clock::time_point expiration{};
    server_limits limits_value;
    bool loaded_value = false;
};

}
