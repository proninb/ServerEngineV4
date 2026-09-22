/*
 * Graph-local connection handle.
 *
 * link_handle identifies one static connection inside one G.
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace cw::server {

class graph;

class link_handle final {
public:
    constexpr link_handle() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return slot;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        link_handle,
        link_handle) noexcept = default;

private:
    explicit constexpr link_handle(
        std::uint32_t value) noexcept
        : slot(value) {
    }

    std::uint32_t slot = 0;

    friend class graph;
};

static_assert(sizeof(link_handle) == 4);
static_assert(std::is_trivially_copyable_v<link_handle>);
static_assert(std::is_standard_layout_v<link_handle>);

}
