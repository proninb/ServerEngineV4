#pragma once
#include "type_ref.hpp"
#include "construction_value.hpp"

namespace cw::server {
// Fixed-width integral representations shared by supported ABIs. 'long' is
// checked again by ABI materialization (the compiled graph is ABI independent).
inline bool enum_value_fits(intrinsic_type type, construction_value value) noexcept {
    bool sign = false;
    unsigned bits = 0;
    switch (type) {
    case intrinsic_type::bool_type: return value.bits() <= 1;
    case intrinsic_type::char_type:
    case intrinsic_type::signed_char: sign = true; bits = 8; break;
    case intrinsic_type::unsigned_char:
    case intrinsic_type::char8_type: bits = 8; break;
    case intrinsic_type::signed_short: sign = true; bits = 16; break;
    case intrinsic_type::unsigned_short:
    case intrinsic_type::char16_type: bits = 16; break;
    case intrinsic_type::signed_int: sign = true; bits = 32; break;
    case intrinsic_type::unsigned_int:
    case intrinsic_type::char32_type: bits = 32; break;
    case intrinsic_type::signed_long:
    case intrinsic_type::signed_long_long: sign = true; bits = 64; break;
    case intrinsic_type::unsigned_long:
    case intrinsic_type::unsigned_long_long: bits = 64; break;
    case intrinsic_type::wchar_type: sign = true; bits = 64; break;
    default: return false;
    }
    const bool negative = value.kind == construction_kind::signed_integer &&
        (value.bits() >> 63) != 0;
    if (negative) return sign && (bits == 64 ||
        value.bits() >= (std::uint64_t{0} - (std::uint64_t{1} << (bits - 1))));
    const auto magnitude = value.bits();
    return bits == 64 && !sign ? true :
        magnitude <= ((std::uint64_t{1} << (bits - (sign ? 1 : 0))) - 1);
}

}
