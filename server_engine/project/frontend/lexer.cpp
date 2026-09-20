#include "lexer.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cw::server {
namespace {

[[nodiscard]] constexpr bool ascii_alpha(char value) noexcept {
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z');
}

[[nodiscard]] constexpr bool ascii_digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr bool identifier_first(char value) noexcept {
    return ascii_alpha(value) || value == '_';
}

[[nodiscard]] constexpr bool identifier_next(char value) noexcept {
    return identifier_first(value) || ascii_digit(value);
}

[[nodiscard]] constexpr bool horizontal_space(char value) noexcept {
    return value == ' ' ||
           value == '\t' ||
           value == '\v' ||
           value == '\f';
}

[[nodiscard]] bool newline_at(
    std::string_view source,
    std::size_t position,
    std::size_t& length) noexcept {

    length = 0;

    if (position >= source.size()) {
        return false;
    }

    if (source[position] == '\n') {
        length = 1;
        return true;
    }

    if (source[position] == '\r') {
        length =
            position + 1 < source.size() &&
            source[position + 1] == '\n'
                ? 2
                : 1;
        return true;
    }

    return false;
}

[[nodiscard]] constexpr std::uint32_t spelling_hash(
    std::string_view value) noexcept {

    std::uint32_t hash = 2166136261u;
    for (const auto byte : value) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 16777619u;
    }
    return hash;
}

[[nodiscard]] token_kind keyword_kind(
    std::string_view value) noexcept {

    switch (spelling_hash(value)) {
    case 0x0b069958u: return value == "false" ? token_kind::kw_false : token_kind::identifier;
    case 0x0bbde79eu: return value == "nullptr" ? token_kind::kw_nullptr : token_kind::identifier;
    case 0x0c547726u: return value == "unsigned" ? token_kind::kw_unsigned : token_kind::identifier;
    case 0x0dc628ceu: return value == "while" ? token_kind::kw_while : token_kind::identifier;
    case 0x0f29c2a6u: return value == "and" ? token_kind::logical_and : token_kind::identifier;
    case 0x13251e95u: return value == "reinterpret_cast" ? token_kind::kw_reinterpret_cast : token_kind::identifier;
    case 0x1472c0a0u: return value == "asm" ? token_kind::kw_asm : token_kind::identifier;
    case 0x19a9984eu: return value == "typename" ? token_kind::kw_typename : token_kind::identifier;
    case 0x1e54727du: return value == "protected" ? token_kind::kw_protected : token_kind::identifier;
    case 0x221ede24u: return value == "typedef" ? token_kind::kw_typedef : token_kind::identifier;
    case 0x28999611u: return value == "new" ? token_kind::kw_new : token_kind::identifier;
    case 0x29b19c8au: return value == "not" ? token_kind::exclamation : token_kind::identifier;
    case 0x2d6871c0u: return value == "register" ? token_kind::kw_register : token_kind::identifier;
    case 0x39386e06u: return value == "if" ? token_kind::kw_if : token_kind::identifier;
    case 0x3b0333a9u: return value == "mutable" ? token_kind::kw_mutable : token_kind::identifier;
    case 0x4288e94cu: return value == "catch" ? token_kind::kw_catch : token_kind::identifier;
    case 0x437c3109u: return value == "bitand" ? token_kind::ampersand : token_kind::identifier;
    case 0x44e4e5f2u: return value == "const_cast" ? token_kind::kw_const_cast : token_kind::identifier;
    case 0x48b5725fu: return value == "void" ? token_kind::kw_void : token_kind::identifier;
    case 0x4db211e5u: return value == "true" ? token_kind::kw_true : token_kind::identifier;
    case 0x4f06d367u: return value == "or_eq" ? token_kind::pipe_assign : token_kind::identifier;
    case 0x5807e43cu: return value == "char8_t" ? token_kind::kw_char8_t : token_kind::identifier;
    case 0x5aa35603u: return value == "constexpr" ? token_kind::kw_constexpr : token_kind::identifier;
    case 0x5d342984u: return value == "or" ? token_kind::logical_or : token_kind::identifier;
    case 0x5d967ebcu: return value == "virtual" ? token_kind::kw_virtual : token_kind::identifier;
    case 0x605fdc91u: return value == "and_eq" ? token_kind::ampersand_assign : token_kind::identifier;
    case 0x61338257u: return value == "noexcept" ? token_kind::kw_noexcept : token_kind::identifier;
    case 0x621cd814u: return value == "do" ? token_kind::kw_do : token_kind::identifier;
    case 0x62cb0d0cu: return value == "private" ? token_kind::kw_private : token_kind::identifier;
    case 0x664fd1d4u: return value == "const" ? token_kind::kw_const : token_kind::identifier;
    case 0x676a80dcu: return value == "dynamic_cast" ? token_kind::kw_dynamic_cast : token_kind::identifier;
    case 0x67c2444au: return value == "delete" ? token_kind::kw_delete : token_kind::identifier;
    case 0x68e79149u: return value == "explicit" ? token_kind::kw_explicit : token_kind::identifier;
    case 0x694aaa0bu: return value == "template" ? token_kind::kw_template : token_kind::identifier;
    case 0x69ce1407u: return value == "using" ? token_kind::kw_using : token_kind::identifier;
    case 0x6ee13afdu: return value == "sizeof" ? token_kind::kw_sizeof : token_kind::identifier;
    case 0x7a78762fu: return value == "throw" ? token_kind::kw_throw : token_kind::identifier;
    case 0x801a266du: return value == "char16_t" ? token_kind::kw_char16_t : token_kind::identifier;
    case 0x816cb000u: return value == "enum" ? token_kind::kw_enum : token_kind::identifier;
    case 0x85ee37bfu: return value == "return" ? token_kind::kw_return : token_kind::identifier;
    case 0x8d911aa9u: return value == "xor_eq" ? token_kind::caret_assign : token_kind::identifier;
    case 0x9087ddb7u: return value == "extern" ? token_kind::kw_extern : token_kind::identifier;
    case 0x923fa396u: return value == "auto" ? token_kind::kw_auto : token_kind::identifier;
    case 0x92c2be20u: return value == "struct" ? token_kind::kw_struct : token_kind::identifier;
    case 0x933b5bdeu: return value == "default" ? token_kind::kw_default : token_kind::identifier;
    case 0x93e05f71u: return value == "switch" ? token_kind::kw_switch : token_kind::identifier;
    case 0x94e1036du: return value == "volatile" ? token_kind::kw_volatile : token_kind::identifier;
    case 0x95e97e5eu: return value == "int" ? token_kind::kw_int : token_kind::identifier;
    case 0x9b2538b1u: return value == "case" ? token_kind::kw_case : token_kind::identifier;
    case 0x9b8caa55u: return value == "requires" ? token_kind::kw_requires : token_kind::identifier;
    case 0xa0eb0f08u: return value == "double" ? token_kind::kw_double : token_kind::identifier;
    case 0xa3383d13u: return value == "concept" ? token_kind::kw_concept : token_kind::identifier;
    case 0xa6c45d85u: return value == "float" ? token_kind::kw_float : token_kind::identifier;
    case 0xa7226423u: return value == "static_cast" ? token_kind::kw_static_cast : token_kind::identifier;
    case 0xa846fc93u: return value == "char32_t" ? token_kind::kw_char32_t : token_kind::identifier;
    case 0xa84c031du: return value == "char" ? token_kind::kw_char : token_kind::identifier;
    case 0xa8953bd8u: return value == "typeid" ? token_kind::kw_typeid : token_kind::identifier;
    case 0xab3e0bffu: return value == "class" ? token_kind::kw_class : token_kind::identifier;
    case 0xac1db00eu: return value == "try" ? token_kind::kw_try : token_kind::identifier;
    case 0xacf38390u: return value == "for" ? token_kind::kw_for : token_kind::identifier;
    case 0xb1727e44u: return value == "continue" ? token_kind::kw_continue : token_kind::identifier;
    case 0xb3d6d26du: return value == "bitor" ? token_kind::pipe : token_kind::identifier;
    case 0xb5712015u: return value == "signed" ? token_kind::kw_signed : token_kind::identifier;
    case 0xb7b4afbdu: return value == "not_eq" ? token_kind::not_equal : token_kind::identifier;
    case 0xba226bd5u: return value == "short" ? token_kind::kw_short : token_kind::identifier;
    case 0xbdbf5bf0u: return value == "else" ? token_kind::kw_else : token_kind::identifier;
    case 0xbeedb7f2u: return value == "compl" ? token_kind::tilde : token_kind::identifier;
    case 0xbef43ea5u: return value == "decltype" ? token_kind::kw_decltype : token_kind::identifier;
    case 0xc2cb5034u: return value == "inline" ? token_kind::kw_inline : token_kind::identifier;
    case 0xc2ecdf53u: return value == "long" ? token_kind::kw_long : token_kind::identifier;
    case 0xc523b9f1u: return value == "wchar_t" ? token_kind::kw_wchar_t : token_kind::identifier;
    case 0xc894953du: return value == "bool" ? token_kind::kw_bool : token_kind::identifier;
    case 0xc9101b72u: return value == "consteval" ? token_kind::kw_consteval : token_kind::identifier;
    case 0xc919731fu: return value == "alignof" ? token_kind::kw_alignof : token_kind::identifier;
    case 0xc9648178u: return value == "break" ? token_kind::kw_break : token_kind::identifier;
    case 0xcace7aa0u: return value == "namespace" ? token_kind::kw_namespace : token_kind::identifier;
    case 0xcba09f8du: return value == "friend" ? token_kind::kw_friend : token_kind::identifier;
    case 0xcc6bdb7eu: return value == "xor" ? token_kind::caret : token_kind::identifier;
    case 0xcc909380u: return value == "public" ? token_kind::kw_public : token_kind::identifier;
    case 0xcd3c1aa1u: return value == "thread_local" ? token_kind::kw_thread_local : token_kind::identifier;
    case 0xd27d73deu: return value == "co_return" ? token_kind::kw_co_return : token_kind::identifier;
    case 0xd290c23bu: return value == "static" ? token_kind::kw_static : token_kind::identifier;
    case 0xd34fd592u: return value == "co_await" ? token_kind::kw_co_await : token_kind::identifier;
    case 0xda2bd281u: return value == "this" ? token_kind::kw_this : token_kind::identifier;
    case 0xdbded6f4u: return value == "union" ? token_kind::kw_union : token_kind::identifier;
    case 0xec3c3c7au: return value == "alignas" ? token_kind::kw_alignas : token_kind::identifier;
    case 0xf5a30fe6u: return value == "goto" ? token_kind::kw_goto : token_kind::identifier;
    case 0xf6522276u: return value == "constinit" ? token_kind::kw_constinit : token_kind::identifier;
    case 0xf874ca49u: return value == "co_yield" ? token_kind::kw_co_yield : token_kind::identifier;
    case 0xfb080cb3u: return value == "export" ? token_kind::kw_export : token_kind::identifier;
    case 0xfb9673deu: return value == "static_assert" ? token_kind::kw_static_assert : token_kind::identifier;
    case 0xfbd4eefdu: return value == "operator" ? token_kind::kw_operator : token_kind::identifier;
    default: return token_kind::identifier;
    }
}

[[nodiscard]] token_kind directive_kind(
    std::string_view value) noexcept {

    if (value == "include") return token_kind::pp_include;
    if (value == "define") return token_kind::pp_define;
    if (value == "undef") return token_kind::pp_undef;
    if (value == "if") return token_kind::pp_if;
    if (value == "ifdef") return token_kind::pp_ifdef;
    if (value == "ifndef") return token_kind::pp_ifndef;
    if (value == "elif") return token_kind::pp_elif;
    if (value == "else") return token_kind::pp_else;
    if (value == "endif") return token_kind::pp_endif;
    if (value == "line") return token_kind::pp_line;
    if (value == "error") return token_kind::pp_error;
    if (value == "pragma") return token_kind::pp_pragma;
    return token_kind::pp_unknown;
}

[[nodiscard]] server_status emit(
    lexical_stream& output,
    token_kind kind,
    std::size_t offset,
    std::size_t length) noexcept {

    return output.append(
        kind,
        static_cast<std::uint32_t>(offset),
        static_cast<std::uint32_t>(length));
}

[[nodiscard]] bool match(
    std::string_view source,
    std::size_t position,
    std::string_view value) noexcept {

    return position <= source.size() &&
           value.size() <= source.size() - position &&
           source.substr(position, value.size()) == value;
}

[[nodiscard]] server_status scan_quoted(
    std::string_view source,
    std::size_t start,
    std::size_t quote_position,
    std::size_t& end) noexcept {

    const auto quote = source[quote_position];
    auto position = quote_position + 1;

    while (position < source.size()) {
        std::size_t newline_length = 0;
        if (newline_at(source, position, newline_length)) {
            return server_status::project_configuration_invalid;
        }

        if (source[position] == '\\') {
            if (position + 1 >= source.size()) {
                return server_status::project_configuration_invalid;
            }

            std::size_t escaped_newline = 0;
            if (newline_at(source, position + 1, escaped_newline)) {
                return server_status::unsupported;
            }

            position += 2;
            continue;
        }

        if (source[position] == quote) {
            end = position + 1;
            return server_status::success;
        }

        ++position;
    }

    (void)start;
    return server_status::project_configuration_invalid;
}

[[nodiscard]] server_status scan_raw_string(
    std::string_view source,
    std::size_t raw_r_position,
    std::size_t& end) noexcept {

    const auto quote_position = raw_r_position + 1;
    if (quote_position >= source.size() ||
        source[quote_position] != '"') {
        return server_status::project_configuration_invalid;
    }

    const auto delimiter_begin = quote_position + 1;
    auto open = delimiter_begin;

    while (open < source.size() &&
           source[open] != '(') {

        const auto value = source[open];
        if (open - delimiter_begin >= 16 ||
            value == ')' ||
            value == '\\' ||
            horizontal_space(value)) {
            return server_status::project_configuration_invalid;
        }

        std::size_t newline_length = 0;
        if (newline_at(source, open, newline_length)) {
            return server_status::project_configuration_invalid;
        }

        ++open;
    }

    if (open >= source.size()) {
        return server_status::project_configuration_invalid;
    }

    const auto delimiter =
        source.substr(
            delimiter_begin,
            open - delimiter_begin);

    auto position = open + 1;
    while (position < source.size()) {
        if (source[position] == ')') {
            const auto suffix = position + 1;
            if (suffix <= source.size() &&
                delimiter.size() <= source.size() - suffix &&
                source.substr(suffix, delimiter.size()) == delimiter) {

                const auto quote = suffix + delimiter.size();
                if (quote < source.size() &&
                    source[quote] == '"') {
                    end = quote + 1;
                    return server_status::success;
                }
            }
        }

        ++position;
    }

    return server_status::project_configuration_invalid;
}

struct literal_start final {
    token_kind kind = token_kind::invalid;
    std::size_t quote_position = 0;
    std::size_t raw_r_position = 0;
};

[[nodiscard]] literal_start detect_literal(
    std::string_view source,
    std::size_t position) noexcept {

    if (position >= source.size()) {
        return {};
    }

    if (source[position] == '"') {
        return {token_kind::string_literal, position, 0};
    }
    if (source[position] == '\'') {
        return {token_kind::character_literal, position, 0};
    }

    if (match(source, position, "R\"")) {
        return {token_kind::raw_string_literal, position + 1, position};
    }
    if (match(source, position, "u8R\"")) {
        return {token_kind::raw_string_literal, position + 3, position + 2};
    }
    if (match(source, position, "uR\"") ||
        match(source, position, "UR\"") ||
        match(source, position, "LR\"")) {
        return {token_kind::raw_string_literal, position + 2, position + 1};
    }

    if (match(source, position, "u8\"") ||
        match(source, position, "u8\'")) {
        return {
            source[position + 2] == '"'
                ? token_kind::string_literal
                : token_kind::character_literal,
            position + 2,
            0,
        };
    }

    if ((source[position] == 'u' ||
         source[position] == 'U' ||
         source[position] == 'L') &&
        position + 1 < source.size() &&
        (source[position + 1] == '"' ||
         source[position + 1] == '\'')) {

        return {
            source[position + 1] == '"'
                ? token_kind::string_literal
                : token_kind::character_literal,
            position + 1,
            0,
        };
    }

    return {};
}

[[nodiscard]] std::size_t scan_pp_number(
    std::string_view source,
    std::size_t position) noexcept {

    auto current = position + 1;
    char previous = source[position];

    while (current < source.size()) {
        const auto value = source[current];

        if (identifier_next(value) ||
            value == '.' ||
            value == '\'') {
            previous = value;
            ++current;
            continue;
        }

        if ((value == '+' || value == '-') &&
            (previous == 'e' || previous == 'E' ||
             previous == 'p' || previous == 'P')) {
            previous = value;
            ++current;
            continue;
        }

        break;
    }

    return current;
}

[[nodiscard]] bool punctuation(
    std::string_view source,
    std::size_t position,
    token_kind& kind,
    std::size_t& length) noexcept {

    kind = token_kind::invalid;
    length = 1;

    const auto c = source[position];
    const auto one = [&](token_kind value) {
        kind = value;
        length = 1;
        return true;
    };
    const auto two = [&](char expected, token_kind value) {
        if (position + 1 < source.size() &&
            source[position + 1] == expected) {
            kind = value;
            length = 2;
            return true;
        }
        return false;
    };

    switch (c) {
    case '{': return one(token_kind::l_brace);
    case '}': return one(token_kind::r_brace);
    case '[': return one(token_kind::l_bracket);
    case ']': return one(token_kind::r_bracket);
    case '(': return one(token_kind::l_paren);
    case ')': return one(token_kind::r_paren);
    case ';': return one(token_kind::semicolon);
    case ',': return one(token_kind::comma);
    case '?': return one(token_kind::question);
    case '~': return one(token_kind::tilde);
    case ':':
        return two(':', token_kind::scope) || one(token_kind::colon);
    case '.':
        if (match(source, position, "...")) {
            kind = token_kind::ellipsis;
            length = 3;
            return true;
        }
        return two('*', token_kind::dot_star) || one(token_kind::dot);
    case '#':
        return two('#', token_kind::hash_hash) || one(token_kind::hash);
    case '+':
        return two('+', token_kind::plus_plus) ||
               two('=', token_kind::plus_assign) ||
               one(token_kind::plus);
    case '-':
        if (match(source, position, "->*")) {
            kind = token_kind::arrow_star;
            length = 3;
            return true;
        }
        return two('-', token_kind::minus_minus) ||
               two('>', token_kind::arrow) ||
               two('=', token_kind::minus_assign) ||
               one(token_kind::minus);
    case '*':
        return two('=', token_kind::star_assign) || one(token_kind::star);
    case '/':
        return two('=', token_kind::slash_assign) || one(token_kind::slash);
    case '%':
        return two('=', token_kind::percent_assign) || one(token_kind::percent);
    case '^':
        return two('=', token_kind::caret_assign) || one(token_kind::caret);
    case '&':
        return two('&', token_kind::logical_and) ||
               two('=', token_kind::ampersand_assign) ||
               one(token_kind::ampersand);
    case '|':
        return two('|', token_kind::logical_or) ||
               two('=', token_kind::pipe_assign) ||
               one(token_kind::pipe);
    case '!':
        return two('=', token_kind::not_equal) || one(token_kind::exclamation);
    case '=':
        return two('=', token_kind::equal) || one(token_kind::assign);
    case '<':
        if (match(source, position, "<=>")) {
            kind = token_kind::spaceship;
            length = 3;
            return true;
        }
        if (match(source, position, "<<=")) {
            kind = token_kind::shift_left_assign;
            length = 3;
            return true;
        }
        if (two('<', token_kind::shift_left) ||
            two('=', token_kind::less_equal)) {
            return true;
        }
        return one(token_kind::less);
    case '>':
        if (match(source, position, ">>=")) {
            kind = token_kind::shift_right_assign;
            length = 3;
            return true;
        }
        if (two('>', token_kind::shift_right) ||
            two('=', token_kind::greater_equal)) {
            return true;
        }
        return one(token_kind::greater);
    default:
        return false;
    }
}

}

server_status lexer::tokenize(
    file_id file,
    std::string_view source,
    lexical_stream& output,
    lexical_error* error) noexcept {

    if (error != nullptr) {
        *error = {};
    }

    const auto fail =
        [&](server_status status,
            std::size_t offset,
            std::size_t length,
            lexical_error_reason reason) noexcept {

            if (error != nullptr) {
                error->offset =
                    static_cast<std::uint32_t>(offset);
                error->length =
                    static_cast<std::uint32_t>(length);
                error->reason = reason;
            }

            return status;
        };

    const auto reset =
        output.reset(file, source.size());

    if (!succeeded(reset)) {
        return reset;
    }

    std::size_t position = 0;
    bool line_start = true;
    bool in_directive = false;
    bool include_header_pending = false;

    while (position < source.size()) {
        const auto value = source[position];

        if (static_cast<unsigned char>(value) >= 0x80U) {
            return fail(
                server_status::unsupported,
                position,
                1,
                lexical_error_reason::unsupported_non_ascii);
        }

        std::size_t newline_length = 0;
        if (newline_at(source, position, newline_length)) {
            if (in_directive) {
                const auto status =
                    emit(output, token_kind::pp_end, position, 0);
                if (!succeeded(status)) {
                    return status;
                }
            }

            in_directive = false;
            include_header_pending = false;
            line_start = true;
            position += newline_length;
            continue;
        }

        if (horizontal_space(value)) {
            ++position;
            continue;
        }

        if (value == '\\' &&
            position + 1 < source.size()) {
            std::size_t spliced_newline = 0;
            if (newline_at(source, position + 1, spliced_newline)) {
                return fail(
                    server_status::unsupported,
                    position,
                    1 + spliced_newline,
                    lexical_error_reason::unsupported_line_splice);
            }
        }

        if (match(source, position, "//")) {
            position += 2;
            while (position < source.size()) {
                if (source[position] == '\\' &&
                    position + 1 < source.size()) {

                    std::size_t spliced_newline = 0;

                    if (newline_at(
                            source,
                            position + 1,
                            spliced_newline)) {

                        return fail(
                            server_status::unsupported,
                            position,
                            1 + spliced_newline,
                            lexical_error_reason::unsupported_line_splice);
                    }
                }

                std::size_t comment_newline = 0;
                if (newline_at(source, position, comment_newline)) {
                    break;
                }
                ++position;
            }
            continue;
        }

        if (match(source, position, "/*")) {
            position += 2;
            bool closed = false;

            while (position < source.size()) {
                if (match(source, position, "*/")) {
                    position += 2;
                    closed = true;
                    break;
                }

                std::size_t comment_newline = 0;
                if (newline_at(source, position, comment_newline)) {
                    if (in_directive) {
                        const auto status =
                            emit(output, token_kind::pp_end, position, 0);
                        if (!succeeded(status)) {
                            return status;
                        }
                    }

                    in_directive = false;
                    include_header_pending = false;
                    line_start = true;
                    position += comment_newline;
                    continue;
                }

                ++position;
            }

            if (!closed) {
                return fail(
                    server_status::project_configuration_invalid,
                    position == 0 ? 0 : position - 1,
                    1,
                    lexical_error_reason::unterminated_block_comment);
            }

            continue;
        }

        if (line_start && value == '#') {
            const auto directive_offset = position;
            ++position;

            while (position < source.size() &&
                   horizontal_space(source[position])) {
                ++position;
            }

            const auto name_begin = position;
            if (position < source.size() &&
                identifier_first(source[position])) {
                ++position;
                while (position < source.size() &&
                       identifier_next(source[position])) {
                    ++position;
                }
            }

            const auto name =
                source.substr(
                    name_begin,
                    position - name_begin);

            const auto kind =
                name.empty()
                    ? token_kind::pp_unknown
                    : directive_kind(name);

            const auto status =
                emit(output, kind, directive_offset, position - directive_offset);
            if (!succeeded(status)) {
                return status;
            }

            line_start = false;
            in_directive = true;
            include_header_pending =
                kind == token_kind::pp_include;
            continue;
        }

        if (include_header_pending) {
            if (value == '"') {
                const auto start = position;
                ++position;
                while (position < source.size() &&
                       source[position] != '"') {
                    std::size_t header_newline = 0;
                    if (newline_at(source, position, header_newline)) {
                        return server_status::project_configuration_invalid;
                    }
                    ++position;
                }
                if (position >= source.size()) {
                    return fail(
                        server_status::project_configuration_invalid,
                        start,
                        source.size() - start,
                        lexical_error_reason::unterminated_header_name);
                }
                ++position;

                const auto status =
                    emit(output, token_kind::header_name_quoted, start, position - start);
                if (!succeeded(status)) {
                    return status;
                }

                include_header_pending = false;
                continue;
            }

            if (value == '<') {
                const auto start = position;
                ++position;
                while (position < source.size() &&
                       source[position] != '>') {
                    std::size_t header_newline = 0;
                    if (newline_at(source, position, header_newline)) {
                        return server_status::project_configuration_invalid;
                    }
                    ++position;
                }
                if (position >= source.size()) {
                    return fail(
                        server_status::project_configuration_invalid,
                        start,
                        source.size() - start,
                        lexical_error_reason::unterminated_header_name);
                }
                ++position;

                const auto status =
                    emit(output, token_kind::header_name_angled, start, position - start);
                if (!succeeded(status)) {
                    return status;
                }

                include_header_pending = false;
                continue;
            }

            include_header_pending = false;
        }

        const auto literal =
            detect_literal(source, position);

        if (literal.kind != token_kind::invalid) {
            const auto start = position;
            std::size_t end = 0;

            const auto status =
                literal.kind == token_kind::raw_string_literal
                    ? scan_raw_string(
                        source,
                        literal.raw_r_position,
                        end)
                    : scan_quoted(
                        source,
                        start,
                        literal.quote_position,
                        end);

            if (!succeeded(status)) {
                return fail(
                    status,
                    start,
                    source.size() - start,
                    status == server_status::unsupported
                        ? lexical_error_reason::unsupported_line_splice
                        : lexical_error_reason::unterminated_literal);
            }

            const auto emitted =
                emit(output, literal.kind, start, end - start);
            if (!succeeded(emitted)) {
                return emitted;
            }

            position = end;
            line_start = false;
            continue;
        }

        if (identifier_first(value)) {
            const auto start = position++;
            while (position < source.size() &&
                   identifier_next(source[position])) {
                ++position;
            }

            const auto spelling =
                source.substr(start, position - start);

            auto kind =
                keyword_kind(spelling);

            if (in_directive &&
                spelling == "defined") {
                kind = token_kind::pp_defined;
            }

            const auto status =
                emit(output, kind, start, position - start);
            if (!succeeded(status)) {
                return status;
            }

            line_start = false;
            continue;
        }

        if (ascii_digit(value) ||
            (value == '.' &&
             position + 1 < source.size() &&
             ascii_digit(source[position + 1]))) {

            const auto start = position;
            position = scan_pp_number(source, position);

            const auto status =
                emit(output, token_kind::pp_number, start, position - start);
            if (!succeeded(status)) {
                return status;
            }

            line_start = false;
            continue;
        }

        token_kind kind{};
        std::size_t punctuation_length = 0;
        if (punctuation(
                source,
                position,
                kind,
                punctuation_length)) {

            const auto status =
                emit(output, kind, position, punctuation_length);
            if (!succeeded(status)) {
                return status;
            }

            position += punctuation_length;
            line_start = false;
            continue;
        }

        return fail(
            server_status::project_configuration_invalid,
            position,
            1,
            lexical_error_reason::invalid_character);
    }

    if (in_directive) {
        const auto status =
            emit(output, token_kind::pp_end, source.size(), 0);
        if (!succeeded(status)) {
            return status;
        }
    }

    return server_status::success;
}

}
