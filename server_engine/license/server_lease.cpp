#include "server_lease.hpp"

#include <utility>

namespace cw::server {

void server_lease::clear() noexcept {
    lease_id_value.clear();
    not_before_value = {};
    expiration = {};
    limits_value = {};
    loaded_value = false;
}

void server_lease::assign(
    std::string lease_id,
    std::chrono::system_clock::time_point not_before,
    std::chrono::system_clock::time_point expires_at,
    server_limits limits) {

    lease_id_value = std::move(lease_id);
    not_before_value = not_before;
    expiration = expires_at;
    limits_value = limits;
    loaded_value = true;
}

bool server_lease::loaded() const noexcept {
    return loaded_value;
}

const std::string& server_lease::lease_id() const noexcept {
    return lease_id_value;
}

std::chrono::system_clock::time_point
server_lease::not_before() const noexcept {
    return not_before_value;
}

std::chrono::system_clock::time_point
server_lease::expires_at() const noexcept {
    return expiration;
}

const server_limits& server_lease::limits() const noexcept {
    return limits_value;
}

bool server_lease::valid_at(
    std::chrono::system_clock::time_point now) const noexcept {

    return loaded_value &&
           now >= not_before_value &&
           now < expiration;
}

}
