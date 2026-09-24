/*
 * Normalized Graph construction value.
 *
 * construction_value stores source-independent initialization semantics.
 * It contains no source spans or process pointers and is suitable for compiled
 * persistence and Runtime construction planning.
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace cw::server {

enum class construction_kind : std::uint32_t {
    zero = 0,
    signed_integer,
    unsigned_integer,
    real,
    member_binding,
    object_binding,
    unsupported,
};

struct construction_value final {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    std::uint32_t operand = 0;
    construction_kind kind =
        construction_kind::zero;

    [[nodiscard]] constexpr std::uint64_t bits() const noexcept {
        return
            static_cast<std::uint64_t>(low) |
            (static_cast<std::uint64_t>(high) << 32);
    }

    [[nodiscard]] static constexpr construction_value constant(
        construction_kind kind,
        std::uint64_t bits) noexcept {

        return {
            static_cast<std::uint32_t>(bits),
            static_cast<std::uint32_t>(bits >> 32),
            0,
            kind,
        };
    }

    [[nodiscard]] static constexpr construction_value member_binding(
        std::uint32_t one_based_member) noexcept {

        return one_based_member != 0
            ? construction_value{
                0,
                0,
                one_based_member,
                construction_kind::member_binding,
            }
            : construction_value{
                0,
                0,
                0,
                construction_kind::unsupported,
            };
    }

    [[nodiscard]] static constexpr construction_value object_binding(
        std::uint32_t object) noexcept {

        return object != 0
            ? construction_value{
                0,
                0,
                object,
                construction_kind::object_binding,
            }
            : construction_value{
                0,
                0,
                0,
                construction_kind::unsupported,
            };
    }

    friend constexpr bool operator==(
        const construction_value&,
        const construction_value&) noexcept = default;
};

static_assert(sizeof(construction_value) == 16);
static_assert(std::is_trivially_copyable_v<construction_value>);
static_assert(std::is_standard_layout_v<construction_value>);

[[nodiscard]] inline constexpr bool valid_construction(
    construction_value value) noexcept {

    switch (value.kind) {
    case construction_kind::zero:
    case construction_kind::unsupported:
        return value.bits() == 0 &&
            value.operand == 0;

    case construction_kind::member_binding:
    case construction_kind::object_binding:
        return value.bits() == 0 &&
            value.operand != 0;

    case construction_kind::signed_integer:
    case construction_kind::unsigned_integer:
    case construction_kind::real:
        return value.operand == 0;
    }

    return false;
}

}
