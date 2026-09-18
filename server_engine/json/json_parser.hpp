/*
 * Streaming JSON-with-comments parser.
 *
 * The parser:
 * - builds no DOM;
 * - throws no exceptions across its public boundary;
 * - supports // and C-style block comments as trivia;
 * - validates UTF-8 and JSON Unicode escapes;
 * - returns exact zero-based byte offset/length on syntax failure;
 * - delivers all scalar values through json_value_view::get<T>().
 */
#pragma once

#include "json_value.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cw::server {

enum class json_error_code : std::uint8_t {
    none = 0,
    unexpected_end,
    unexpected_token,
    invalid_string,
    invalid_escape,
    invalid_unicode,
    invalid_utf8,
    invalid_number,
    unterminated_comment,
    nesting_too_deep,
    internal_failure,
};

struct json_parse_result {
    json_error_code code = json_error_code::none;

    // Zero-based byte offset into the original JSON source.
    std::size_t offset = 0;

    // Byte extent associated with the error; normally at least one.
    std::size_t length = 0;

    [[nodiscard]] constexpr bool ok() const noexcept {
        return code == json_error_code::none;
    }
};

[[nodiscard]] constexpr std::string_view json_error_message(
    json_error_code code) noexcept {

    switch (code) {
    case json_error_code::none:
        return "No JSON error";
    case json_error_code::unexpected_end:
        return "Unexpected end of JSON input";
    case json_error_code::unexpected_token:
        return "Unexpected JSON token";
    case json_error_code::invalid_string:
        return "Invalid JSON string";
    case json_error_code::invalid_escape:
        return "Invalid JSON escape sequence";
    case json_error_code::invalid_unicode:
        return "Invalid JSON Unicode escape";
    case json_error_code::invalid_utf8:
        return "Invalid UTF-8 in JSON string";
    case json_error_code::invalid_number:
        return "Invalid JSON number";
    case json_error_code::unterminated_comment:
        return "Unterminated JSON block comment";
    case json_error_code::nesting_too_deep:
        return "JSON nesting is too deep";
    case json_error_code::internal_failure:
        return "JSON parser internal failure";
    }

    return "Unknown JSON error";
}

// Receives structural/scalar JSON events directly from the parser.
//
// Callbacks may allocate/throw internally. parse_json() catches all exceptions
// and reports internal_failure instead of allowing exceptions across the JSON
// subsystem boundary.
class json_event_handler {
public:
    virtual ~json_event_handler() = default;

    // Location of the event immediately following this callback.
    virtual void location(
        std::size_t offset,
        std::size_t length) {
        static_cast<void>(offset);
        static_cast<void>(length);
    }

    virtual void object_begin() = 0;
    virtual void object_end() = 0;
    virtual void array_begin() = 0;
    virtual void array_end() = 0;

    // key is decoded UTF-8, never the raw escaped token.
    virtual void key(
        std::string_view key) = 0;

    // Scalar value. String storage is valid only during this callback.
    virtual void value(
        json_value_view value) = 0;
};

[[nodiscard]] json_parse_result parse_json(
    std::string_view text,
    json_event_handler& handler) noexcept;

}
