/*
 * Streaming JSON-with-comments parser implementation.
 */
#include "json_parser.hpp"

#include "json_ascii.hpp"
#include "json_unicode.hpp"

#include <algorithm>
#include <new>
#include <string>

namespace cw::server {

class json_parser_impl final {
public:
    json_parser_impl(
        std::string_view text,
        json_event_handler& handler) noexcept
        : text(text),
          handler(handler) {
    }

    [[nodiscard]] json_parse_result run() {
        skip_bom();

        if (!skip_trivia()) {
            return result;
        }

        if (position >= text.size()) {
            static_cast<void>(
                fail(
                    json_error_code::unexpected_end,
                    position,
                    0));
            return result;
        }

        if (!parse_value(0)) {
            return result;
        }

        if (!skip_trivia()) {
            return result;
        }

        if (position != text.size()) {
            static_cast<void>(
                fail(
                    json_error_code::unexpected_token,
                    position,
                    1));
        }

        return result;
    }

private:
    static constexpr std::size_t max_depth = 64;

    void skip_bom() noexcept {
        if (text.size() >= 3 &&
            static_cast<unsigned char>(text[0]) == 0xEFu &&
            static_cast<unsigned char>(text[1]) == 0xBBu &&
            static_cast<unsigned char>(text[2]) == 0xBFu) {
            position = 3;
        }
    }

    [[nodiscard]] bool skip_trivia() {
        while (position < text.size()) {
            if (json_is_space(text[position])) {
                ++position;
                continue;
            }

            if (text[position] != '/' ||
                position + 1 >= text.size()) {
                return true;
            }

            const char next =
                text[position + 1];

            if (next == '/') {
                position += 2;

                while (position < text.size() &&
                       text[position] != '\r' &&
                       text[position] != '\n') {
                    ++position;
                }

                continue;
            }

            if (next == '*') {
                const auto begin =
                    position;

                position += 2;

                bool closed = false;

                while (position + 1 < text.size()) {
                    if (text[position] == '*' &&
                        text[position + 1] == '/') {
                        position += 2;
                        closed = true;
                        break;
                    }

                    ++position;
                }

                if (!closed) {
                    return fail(
                        json_error_code::unterminated_comment,
                        begin,
                        text.size() - begin);
                }

                continue;
            }

            return true;
        }

        return true;
    }

    [[nodiscard]] bool parse_value(
        std::size_t depth) {

        if (!skip_trivia()) {
            return false;
        }

        if (position >= text.size()) {
            return fail(
                json_error_code::unexpected_end,
                position,
                0);
        }

        switch (text[position]) {
        case '{':
            return parse_object(depth);

        case '[':
            return parse_array(depth);

        case '"':
            return parse_string_value();

        case 't':
            return parse_literal(
                "true",
                json_value_view(
                    json_value_kind::boolean,
                    {},
                    true));

        case 'f':
            return parse_literal(
                "false",
                json_value_view(
                    json_value_kind::boolean,
                    {},
                    false));

        case 'n':
            return parse_literal(
                "null",
                json_value_view(
                    json_value_kind::null_value,
                    {},
                    false));

        default:
            if (text[position] == '-' ||
                json_is_digit(text[position])) {
                return parse_number();
            }

            return fail(
                json_error_code::unexpected_token,
                position,
                1);
        }
    }

    [[nodiscard]] bool parse_object(
        std::size_t depth) {

        if (depth >= max_depth) {
            return fail(
                json_error_code::nesting_too_deep,
                position,
                1);
        }

        const auto begin =
            position++;

        handler.location(
            begin,
            1);
        handler.object_begin();

        if (!skip_trivia()) {
            return false;
        }

        if (consume('}')) {
            handler.object_end();
            return true;
        }

        while (true) {
            if (!skip_trivia()) {
                return false;
            }

            if (position >= text.size()) {
                return fail(
                    json_error_code::unexpected_end,
                    position,
                    0);
            }

            if (text[position] != '"') {
                return fail(
                    json_error_code::unexpected_token,
                    position,
                    1);
            }

            const auto key_begin =
                position;

            if (!parse_decoded_string()) {
                return false;
            }

            handler.location(
                key_begin,
                position - key_begin);

            handler.key(scratch);

            if (!skip_trivia()) {
                return false;
            }

            if (!consume(':')) {
                return fail(
                    json_error_code::unexpected_token,
                    position,
                    position < text.size() ? 1 : 0);
            }

            if (!parse_value(depth + 1)) {
                return false;
            }

            if (!skip_trivia()) {
                return false;
            }

            if (consume('}')) {
                handler.object_end();
                return true;
            }

            if (!consume(',')) {
                return fail(
                    json_error_code::unexpected_token,
                    position,
                    position < text.size() ? 1 : 0);
            }
        }
    }

    [[nodiscard]] bool parse_array(
        std::size_t depth) {

        if (depth >= max_depth) {
            return fail(
                json_error_code::nesting_too_deep,
                position,
                1);
        }

        const auto begin =
            position++;

        handler.location(
            begin,
            1);
        handler.array_begin();

        if (!skip_trivia()) {
            return false;
        }

        if (consume(']')) {
            handler.array_end();
            return true;
        }

        while (true) {
            if (!parse_value(depth + 1)) {
                return false;
            }

            if (!skip_trivia()) {
                return false;
            }

            if (consume(']')) {
                handler.array_end();
                return true;
            }

            if (!consume(',')) {
                return fail(
                    json_error_code::unexpected_token,
                    position,
                    position < text.size() ? 1 : 0);
            }
        }
    }

    [[nodiscard]] bool parse_string_value() {
        const auto begin =
            position;

        if (!parse_decoded_string()) {
            return false;
        }

        handler.location(
            begin,
            position - begin);

        handler.value(
            json_value_view(
                json_value_kind::string,
                scratch,
                false));

        return true;
    }

    [[nodiscard]] bool parse_decoded_string() {
        if (!consume('"')) {
            return fail(
                json_error_code::unexpected_token,
                position,
                position < text.size() ? 1 : 0);
        }

        scratch.clear();

        while (position < text.size()) {
            const auto byte =
                static_cast<unsigned char>(
                    text[position]);

            if (byte == '"') {
                ++position;
                return true;
            }

            if (byte < 0x20u) {
                return fail(
                    json_error_code::invalid_string,
                    position,
                    1);
            }

            if (byte == '\\') {
                const auto escape_offset =
                    position++;

                if (position >= text.size()) {
                    return fail(
                        json_error_code::unexpected_end,
                        position,
                        0);
                }

                const char escape =
                    text[position++];

                switch (escape) {
                case '"':
                    scratch.push_back('"');
                    break;
                case '\\':
                    scratch.push_back('\\');
                    break;
                case '/':
                    scratch.push_back('/');
                    break;
                case 'b':
                    scratch.push_back('\b');
                    break;
                case 'f':
                    scratch.push_back('\f');
                    break;
                case 'n':
                    scratch.push_back('\n');
                    break;
                case 'r':
                    scratch.push_back('\r');
                    break;
                case 't':
                    scratch.push_back('\t');
                    break;
                case 'u':
                    if (!parse_unicode_escape(
                            escape_offset)) {
                        return false;
                    }
                    break;
                default:
                    return fail(
                        json_error_code::invalid_escape,
                        escape_offset,
                        2);
                }

                continue;
            }

            if (byte <= 0x7Fu) {
                scratch.push_back(
                    static_cast<char>(byte));
                ++position;
                continue;
            }

            const auto decoded =
                json_decode_utf8_one(
                    text,
                    position);

            if (decoded.status !=
                json_utf8_status::ok) {

                return fail(
                    json_error_code::invalid_utf8,
                    position,
                    std::max<std::size_t>(
                        decoded.length,
                        1));
            }

            scratch.append(
                text.data() + position,
                decoded.length);

            position +=
                decoded.length;
        }

        return fail(
            json_error_code::unexpected_end,
            position,
            0);
    }

    [[nodiscard]] bool parse_unicode_escape(
        std::size_t escape_offset) {

        std::uint32_t first = 0;

        if (!parse_hex4(first)) {
            return false;
        }

        std::uint32_t codepoint =
            first;

        if (json_is_high_surrogate(first)) {
            if (position + 2 > text.size() ||
                text[position] != '\\' ||
                text[position + 1] != 'u') {

                return fail(
                    json_error_code::invalid_unicode,
                    escape_offset,
                    position - escape_offset);
            }

            position += 2;

            std::uint32_t second = 0;

            if (!parse_hex4(second)) {
                return false;
            }

            if (!json_is_low_surrogate(second)) {
                return fail(
                    json_error_code::invalid_unicode,
                    escape_offset,
                    position - escape_offset);
            }

            codepoint =
                0x10000u +
                ((first - 0xD800u) << 10u) +
                (second - 0xDC00u);
        } else if (
            json_is_low_surrogate(first)) {

            return fail(
                json_error_code::invalid_unicode,
                escape_offset,
                position - escape_offset);
        }

        json_append_utf8(
            codepoint,
            scratch);

        return true;
    }

    [[nodiscard]] bool parse_hex4(
        std::uint32_t& output) {

        if (position + 4 > text.size()) {
            return fail(
                json_error_code::unexpected_end,
                position,
                text.size() - position);
        }

        output = 0;

        for (int i = 0;
             i < 4;
             ++i) {

            const int digit =
                json_hex_value(
                    text[position]);

            if (digit < 0) {
                return fail(
                    json_error_code::invalid_unicode,
                    position,
                    1);
            }

            ++position;

            output =
                (output << 4u) |
                static_cast<std::uint32_t>(
                    digit);
        }

        return true;
    }

    [[nodiscard]] bool parse_number() {
        const auto begin =
            position;

        if (consume('-') &&
            position >= text.size()) {
            return fail(
                json_error_code::invalid_number,
                begin,
                position - begin);
        }

        if (consume('0')) {
            if (position < text.size() &&
                json_is_digit(text[position])) {
                return fail(
                    json_error_code::invalid_number,
                    begin,
                    position - begin + 1);
            }
        } else {
            if (position >= text.size() ||
                !json_is_digit(text[position])) {
                return fail(
                    json_error_code::invalid_number,
                    begin,
                    std::max<std::size_t>(
                        position - begin,
                        1));
            }

            while (position < text.size() &&
                   json_is_digit(text[position])) {
                ++position;
            }
        }

        bool integer = true;

        if (consume('.')) {
            integer = false;

            if (position >= text.size() ||
                !json_is_digit(text[position])) {
                return fail(
                    json_error_code::invalid_number,
                    begin,
                    position - begin);
            }

            while (position < text.size() &&
                   json_is_digit(text[position])) {
                ++position;
            }
        }

        if (position < text.size() &&
            (text[position] == 'e' ||
             text[position] == 'E')) {

            integer = false;
            ++position;

            if (position < text.size() &&
                (text[position] == '+' ||
                 text[position] == '-')) {
                ++position;
            }

            if (position >= text.size() ||
                !json_is_digit(text[position])) {
                return fail(
                    json_error_code::invalid_number,
                    begin,
                    position - begin);
            }

            while (position < text.size() &&
                   json_is_digit(text[position])) {
                ++position;
            }
        }

        const auto token =
            text.substr(
                begin,
                position - begin);

        handler.location(
            begin,
            token.size());

        handler.value(
            json_value_view(
                integer
                    ? json_value_kind::integer
                    : json_value_kind::number,
                token,
                false));

        return true;
    }

    [[nodiscard]] bool parse_literal(
        std::string_view literal,
        json_value_view value) {

        const auto begin =
            position;

        if (text.substr(
                position,
                literal.size()) != literal) {

            return fail(
                json_error_code::unexpected_token,
                position,
                1);
        }

        position +=
            literal.size();

        handler.location(
            begin,
            literal.size());

        handler.value(value);
        return true;
    }

    [[nodiscard]] bool consume(
        char value) noexcept {

        if (position < text.size() &&
            text[position] == value) {
            ++position;
            return true;
        }

        return false;
    }

    [[nodiscard]] bool fail(
        json_error_code code,
        std::size_t offset,
        std::size_t length) noexcept {

        if (result.code ==
            json_error_code::none) {

            result = {
                code,
                offset,
                length,
            };
        }

        return false;
    }

    std::string_view text;
    json_event_handler& handler;
    std::size_t position = 0;
    json_parse_result result;

    // Decoded key/string storage. Valid through the callback only.
    std::string scratch;
};

json_parse_result parse_json(
    std::string_view text,
    json_event_handler& handler) noexcept {

    try {
        return json_parser_impl(
            text,
            handler).run();
    } catch (const std::bad_alloc&) {
        return {
            json_error_code::internal_failure,
            0,
            0,
        };
    } catch (...) {
        return {
            json_error_code::internal_failure,
            0,
            0,
        };
    }
}

}
