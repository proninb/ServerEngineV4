/*
 * Client-owned request correlation identity.
 */
#pragma once
#include <cstdint>

namespace cw::server {

struct request_id final {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }

    friend constexpr bool operator==(request_id, request_id) noexcept = default;
};

}

