/*
 * Transport-independent Communication protocol contracts.
 */
#pragma once

#include "../request_id.hpp"
#include "../../server_state.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cw::server {

struct server_state_action final {
    project_state old_state = project_state::unloaded;
    project_state new_state = project_state::unloaded;
};

struct client_message final {
    std::string login;
    std::string arg;
    std::vector<std::byte> parameters;
};

struct client_action final {
    std::string from;
    std::string arg;
    std::vector<std::byte> parameters;
};

struct subscription_id final {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }

    friend constexpr bool operator==(subscription_id, subscription_id) noexcept = default;
};

struct subscription_sequence final {
    std::uint64_t value = 0;
};

}

