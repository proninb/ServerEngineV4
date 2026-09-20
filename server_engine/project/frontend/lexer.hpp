/*
 * Parallel per-file construction lexer.
 *
 * lexer reads one immutable physical file and produces one private compact
 * lexical_stream. It has no shared mutable state and performs no textual or
 * semantic interning, so independent files may be lexed concurrently.
 */
#pragma once

#include "lexical_stream.hpp"

#include <string_view>

namespace cw::server {

enum class lexical_error_reason : std::uint8_t {
    none = 0,
    unsupported_non_ascii,
    unsupported_line_splice,
    unterminated_block_comment,
    unterminated_literal,
    unterminated_header_name,
    invalid_character,
};

struct lexical_error final {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    lexical_error_reason reason =
        lexical_error_reason::none;
};

[[nodiscard]] constexpr const char* lexical_error_message(
    lexical_error_reason reason) noexcept {

    switch (reason) {
    case lexical_error_reason::unsupported_non_ascii:
        return "Non-ASCII source text is not supported by the current lexer";
    case lexical_error_reason::unsupported_line_splice:
        return "Backslash-newline source splicing is not supported";
    case lexical_error_reason::unterminated_block_comment:
        return "Block comment is not terminated";
    case lexical_error_reason::unterminated_literal:
        return "String or character literal is not terminated";
    case lexical_error_reason::unterminated_header_name:
        return "Preprocessing include header name is not terminated";
    case lexical_error_reason::invalid_character:
        return "Source contains a character that cannot be tokenized";
    case lexical_error_reason::none:
        return "Lexical analysis failed";
    }

    return "Lexical analysis failed";
}

class lexer final {
public:
    [[nodiscard]] static server_status tokenize(
        file_id file,
        std::string_view source,
        lexical_stream& output,
        lexical_error* error = nullptr) noexcept;
};

}
