/*
 * Compact semantic type expression reference.
 *
 * type_ref packs kind + payload into four bytes. Named payloads are semantic
 * type identity slots; derived expressions use a Graph-owned canonical slot.
 */
#pragma once

#include "type_handle.hpp"

#include <cstdint>
#include <type_traits>

namespace cw::server {

class graph;
class graph_delta;
class compiled_project_view;

enum class intrinsic_type : std::uint8_t {
    none = 0,
    void_type,
    bool_type,
    char_type,
    signed_char,
    unsigned_char,
    wchar_type,
    char8_type,
    char16_type,
    char32_type,
    signed_short,
    unsigned_short,
    signed_int,
    unsigned_int,
    signed_long,
    unsigned_long,
    signed_long_long,
    unsigned_long_long,
    float_type,
    double_type,
    long_double_type,
    nullptr_type,
};

enum class type_ref_kind : std::uint8_t {
    invalid = 0,
    intrinsic = 1,
    named = 2,
    derived = 3,
};

enum class derived_type_kind : std::uint8_t {
    const_qualified,
    volatile_qualified,
    pointer,
    lvalue_reference,
    rvalue_reference,
    bounded_array,
    unbounded_array,
};

class type_ref final {
public:
    constexpr type_ref() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return packed;
    }

    [[nodiscard]] constexpr type_ref_kind kind() const noexcept {
        return static_cast<type_ref_kind>(
            packed >> kind_shift);
    }

    [[nodiscard]] constexpr std::uint32_t payload() const noexcept {
        return packed & payload_mask;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return kind() != type_ref_kind::invalid &&
            payload() != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        type_ref,
        type_ref) noexcept = default;

    static constexpr std::uint32_t maximum_payload =
        0x3fffffffu;

private:
    static constexpr std::uint32_t kind_shift = 30;
    static constexpr std::uint32_t payload_mask =
        maximum_payload;

    [[nodiscard]] static constexpr type_ref make(
        type_ref_kind kind,
        std::uint32_t payload) noexcept {

        return kind != type_ref_kind::invalid &&
            payload != 0 &&
            payload <= maximum_payload
            ? type_ref{
                (static_cast<std::uint32_t>(kind) << kind_shift) |
                payload}
            : type_ref{};
    }

    explicit constexpr type_ref(
        std::uint32_t value) noexcept
        : packed(value) {
    }

    std::uint32_t packed = 0;

    friend class graph;
    friend class graph_delta;
    friend class compiled_project_view;
};

static_assert(sizeof(type_ref) == 4);
static_assert(std::is_trivially_copyable_v<type_ref>);
static_assert(std::is_standard_layout_v<type_ref>);

// The backing member carries a signed 24-bit origin without changing the image layout.
inline constexpr std::int32_t array_origin_min = -0x800000;
inline constexpr std::int32_t array_origin_max = 0x7fffff;

inline constexpr std::int32_t array_origin(const std::uint8_t (&bytes)[3]) noexcept {
    const auto value = std::int32_t{bytes[0]} | (std::int32_t{bytes[1]} << 8) |
        (std::int32_t{bytes[2]} << 16);
    return value < 0x800000 ? value : value - 0x1000000;
}

inline constexpr void set_array_origin(std::uint8_t (&bytes)[3], std::int32_t origin) noexcept {
    const auto value = static_cast<std::uint32_t>(origin);
    for (unsigned i = 0; i < 3; ++i)
        bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

inline constexpr bool array_offset(std::int64_t index, std::int32_t origin,
    std::uint64_t count, std::uint64_t& offset) noexcept {
    if (index < origin) return false;
    offset = static_cast<std::uint64_t>(index) - static_cast<std::uint64_t>(origin);
    return offset < count;
}

struct derived_type_record final {
    std::uint64_t payload = 0;
    type_ref child{};
    derived_type_kind kind =
        derived_type_kind::pointer;
    std::uint8_t reserved[3]{};
};

static_assert(sizeof(derived_type_record) == 16);
static_assert(std::is_trivially_copyable_v<derived_type_record>);
static_assert(std::is_standard_layout_v<derived_type_record>);

}
