/*
 * Type-local member index.
 *
 * member_index is zero-based inside one type definition. It is not a global
 * Graph identity and has meaning only together with the owning type/object.
 */
#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace cw::server {

class graph;
class graph_delta;
class compiled_project_view;

class member_index final {
public:
    constexpr member_index() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return index;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_value;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        member_index,
        member_index) noexcept = default;

private:
    static constexpr std::uint32_t invalid_value =
        (std::numeric_limits<std::uint32_t>::max)();

    explicit constexpr member_index(
        std::uint32_t value) noexcept
        : index(value) {
    }

    std::uint32_t index = invalid_value;

    friend class graph;
    friend class graph_delta;
    friend class compiled_project_view;
};

static_assert(sizeof(member_index) == 4);
static_assert(std::is_trivially_copyable_v<member_index>);
static_assert(std::is_standard_layout_v<member_index>);

}
