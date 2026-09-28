/*
 * Process-level Server license state.
 */
#include "server_license.hpp"

namespace cw::server {

void server_license::clear() noexcept {
    expiration = {};
    server_limits_value = {};
    loaded_value = false;
}

void server_license::assign(
    std::chrono::system_clock::time_point expires_at,
    server_limits limits) noexcept {

    expiration = expires_at;
    server_limits_value = limits;
    loaded_value = true;
}

bool server_license::loaded() const noexcept {
    return loaded_value;
}

std::chrono::system_clock::time_point
server_license::expires_at() const noexcept {
    return expiration;
}

const server_limits& server_license::limits() const noexcept {
    return server_limits_value;
}

}
