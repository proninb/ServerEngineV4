/*
 * Portable logical name contract for the one Server Project SHM.
 *
 * Windows uses the configured name directly. POSIX adds only the leading '/'
 * required by shm_open; no product or process prefix is hidden here.
 */
#pragma once

#include <cstddef>
#include <string_view>

namespace cw::server {

inline constexpr std::size_t
shared_memory_name_max_length = 128;

[[nodiscard]] constexpr bool valid_shared_memory_name(
    std::string_view name) noexcept {

    if (name.empty() ||
        name.size() >
            shared_memory_name_max_length) {

        return false;
    }

    for (const auto value : name) {
        const auto alpha =
            (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z');

        const auto digit =
            value >= '0' && value <= '9';

        if (!alpha &&
            !digit &&
            value != '.' &&
            value != '_' &&
            value != '-') {

            return false;
        }
    }

    return true;
}

}
