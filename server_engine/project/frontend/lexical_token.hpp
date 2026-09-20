/*
 * Compact construction lexical token.
 *
 * lexical_token is the four-byte physical token emitted by the parallel
 * per-file lexer. It carries lexical classification, source-start delta, and
 * source length; textual and semantic identity remain outside this layer.
 */
#pragma once

#include <cstdint>

namespace cw::server {

enum class token_kind : std::uint8_t {
    invalid = 0,

    identifier,
    pp_number,
    character_literal,
    string_literal,
    raw_string_literal,
    header_name_quoted,
    header_name_angled,

    pp_include,
    pp_define,
    pp_undef,
    pp_if,
    pp_ifdef,
    pp_ifndef,
    pp_elif,
    pp_else,
    pp_endif,
    pp_line,
    pp_error,
    pp_pragma,
    pp_unknown,
    pp_defined,
    pp_end,

    kw_alignas,
    kw_alignof,
    kw_asm,
    kw_auto,
    kw_bool,
    kw_break,
    kw_case,
    kw_catch,
    kw_char,
    kw_char8_t,
    kw_char16_t,
    kw_char32_t,
    kw_class,
    kw_concept,
    kw_const,
    kw_consteval,
    kw_constexpr,
    kw_constinit,
    kw_const_cast,
    kw_continue,
    kw_co_await,
    kw_co_return,
    kw_co_yield,
    kw_decltype,
    kw_default,
    kw_delete,
    kw_do,
    kw_double,
    kw_dynamic_cast,
    kw_else,
    kw_enum,
    kw_explicit,
    kw_export,
    kw_extern,
    kw_false,
    kw_float,
    kw_for,
    kw_friend,
    kw_goto,
    kw_if,
    kw_inline,
    kw_int,
    kw_long,
    kw_mutable,
    kw_namespace,
    kw_new,
    kw_noexcept,
    kw_nullptr,
    kw_operator,
    kw_private,
    kw_protected,
    kw_public,
    kw_register,
    kw_reinterpret_cast,
    kw_requires,
    kw_return,
    kw_short,
    kw_signed,
    kw_sizeof,
    kw_static,
    kw_static_assert,
    kw_static_cast,
    kw_struct,
    kw_switch,
    kw_template,
    kw_this,
    kw_thread_local,
    kw_throw,
    kw_true,
    kw_try,
    kw_typedef,
    kw_typeid,
    kw_typename,
    kw_union,
    kw_unsigned,
    kw_using,
    kw_virtual,
    kw_void,
    kw_volatile,
    kw_wchar_t,
    kw_while,

    l_brace,
    r_brace,
    l_bracket,
    r_bracket,
    l_paren,
    r_paren,
    semicolon,
    colon,
    scope,
    comma,
    dot,
    ellipsis,
    question,
    hash,
    hash_hash,

    plus,
    minus,
    star,
    slash,
    percent,
    caret,
    ampersand,
    pipe,
    tilde,
    exclamation,
    assign,
    less,
    greater,

    plus_plus,
    minus_minus,
    arrow,
    arrow_star,
    dot_star,

    plus_assign,
    minus_assign,
    star_assign,
    slash_assign,
    percent_assign,
    caret_assign,
    ampersand_assign,
    pipe_assign,

    equal,
    not_equal,
    less_equal,
    greater_equal,
    logical_and,
    logical_or,

    shift_left,
    shift_right,
    shift_left_assign,
    shift_right_assign,
    spaceship,
};

// Physical layout:
//
//   [ token_kind : 8 ][ source-start delta : 16 ][ source length : 8 ]
//
// Escape values are resolved through lexical_stream's sparse extended records.
class lexical_token final {
public:
    constexpr lexical_token() noexcept = default;

    [[nodiscard]] static constexpr lexical_token make(
        token_kind kind,
        std::uint32_t delta,
        std::uint32_t length) noexcept {

        return delta <= delta_max && length <= length_max
            ? lexical_token(
                (static_cast<std::uint32_t>(kind) << kind_shift) |
                (delta << delta_shift) |
                length)
            : lexical_token{};
    }

    [[nodiscard]] constexpr token_kind kind() const noexcept {
        return static_cast<token_kind>(word >> kind_shift);
    }

    [[nodiscard]] constexpr std::uint32_t delta() const noexcept {
        return (word >> delta_shift) & delta_mask;
    }

    [[nodiscard]] constexpr std::uint32_t length() const noexcept {
        return word & length_mask;
    }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return word;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return kind() != token_kind::invalid;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(
        lexical_token,
        lexical_token) noexcept = default;

    static constexpr std::uint32_t delta_max = 0x0000ffffu;
    static constexpr std::uint32_t extended_delta = delta_max;
    static constexpr std::uint32_t inline_delta_max = delta_max - 1;

    static constexpr std::uint32_t length_max = 0x000000ffu;
    static constexpr std::uint32_t extended_length = length_max;
    static constexpr std::uint32_t inline_length_max = length_max - 1;

private:
    static constexpr std::uint32_t kind_shift = 24;
    static constexpr std::uint32_t delta_shift = 8;
    static constexpr std::uint32_t delta_mask = 0x0000ffffu;
    static constexpr std::uint32_t length_mask = 0x000000ffu;

    explicit constexpr lexical_token(
        std::uint32_t value) noexcept
        : word(value) {
    }

    std::uint32_t word = 0;
};

static_assert(sizeof(lexical_token) == 4);
static_assert(static_cast<std::uint32_t>(token_kind::spaceship) < 256);

}
