#pragma once

#include "../graph/type_ref.hpp"
#include <string_view>

namespace cw::server {

// Parser conveniences, not nominal declarations. Explicit Header identities
// take precedence. Fixed-width spellings use the supported 8-bit-char,
// 16-bit-short, 32-bit-int, 64-bit-long-long ABI family.
[[nodiscard]] inline constexpr intrinsic_type scalar_type_name(std::string_view name, bool standard_only = false) noexcept {
    if (name.empty() || name.size() > 10) return intrinsic_type::none;
    if (name.front()!='i' && name.front()!='u' && name.front()!='b' && name.front()!='s' && name.front()!='l')
        return intrinsic_type::none;
    struct entry { std::string_view name; intrinsic_type type; };
    constexpr entry fixed[] = {
        {"int8_t",intrinsic_type::signed_char}, {"uint8_t",intrinsic_type::unsigned_char},
        {"int16_t",intrinsic_type::signed_short}, {"uint16_t",intrinsic_type::unsigned_short},
        {"int32_t",intrinsic_type::signed_int}, {"uint32_t",intrinsic_type::unsigned_int},
        {"int64_t",intrinsic_type::signed_long_long}, {"uint64_t",intrinsic_type::unsigned_long_long},
        {"intmax_t",intrinsic_type::signed_long_long}, {"uintmax_t",intrinsic_type::unsigned_long_long},
    };
    for (const auto& entry : fixed) if (entry.name == name) return entry.type;
    if (standard_only) return intrinsic_type::none;
    constexpr entry short_names[] = {
        {"byte",intrinsic_type::unsigned_char}, {"sbyte",intrinsic_type::signed_char},
        {"uchar",intrinsic_type::unsigned_char}, {"ushort",intrinsic_type::unsigned_short},
        {"uint",intrinsic_type::unsigned_int}, {"ulong",intrinsic_type::unsigned_long},
        {"longlong",intrinsic_type::signed_long_long}, {"ulonglong",intrinsic_type::unsigned_long_long},
        {"int8",intrinsic_type::signed_char}, {"uint8",intrinsic_type::unsigned_char},
        {"int16",intrinsic_type::signed_short}, {"uint16",intrinsic_type::unsigned_short},
        {"int32",intrinsic_type::signed_int}, {"uint32",intrinsic_type::unsigned_int},
        {"int64",intrinsic_type::signed_long_long}, {"uint64",intrinsic_type::unsigned_long_long},
        {"i8",intrinsic_type::signed_char}, {"u8",intrinsic_type::unsigned_char},
        {"i16",intrinsic_type::signed_short}, {"u16",intrinsic_type::unsigned_short},
        {"i32",intrinsic_type::signed_int}, {"u32",intrinsic_type::unsigned_int},
        {"i64",intrinsic_type::signed_long_long}, {"u64",intrinsic_type::unsigned_long_long},
    };
    for (const auto& entry : short_names) if (entry.name == name) return entry.type;
    return intrinsic_type::none;
}

}
