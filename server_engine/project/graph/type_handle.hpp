/*
 * Graph-local type handle.
 *
 * type_handle identifies one type slot inside one G. It is not semantic
 * identity and is not stable across independent REBUILD results.
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace cw::server {

class graph;
class compiled_project_view;

class type_handle final {
public:
    constexpr type_handle() noexcept = default;

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
        type_handle,
        type_handle) noexcept = default;

    static constexpr std::uint32_t maximum_slot =
        0x3fffffffu;

private:
    explicit constexpr type_handle(
        std::uint32_t value) noexcept
        : slot(value) {
    }

    std::uint32_t slot = 0;

    friend class graph;
    friend class compiled_project_view;
    friend class source_map;
};

static_assert(sizeof(type_handle) == 4);
static_assert(std::is_trivially_copyable_v<type_handle>);
static_assert(std::is_standard_layout_v<type_handle>);

}
