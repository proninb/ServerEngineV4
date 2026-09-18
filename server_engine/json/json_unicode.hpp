/*
 * JSON Unicode/UTF-8 helpers.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cw::server {

[[nodiscard]] constexpr bool json_is_high_surrogate(
    std::uint32_t value) noexcept {

    return value >= 0xD800u && value <= 0xDBFFu;
}

[[nodiscard]] constexpr bool json_is_low_surrogate(
    std::uint32_t value) noexcept {

    return value >= 0xDC00u && value <= 0xDFFFu;
}

[[nodiscard]] constexpr bool json_is_surrogate(
    std::uint32_t value) noexcept {

    return value >= 0xD800u && value <= 0xDFFFu;
}

[[nodiscard]] constexpr bool json_is_unicode_scalar(
    std::uint32_t value) noexcept {

    return value <= 0x10FFFFu &&
           !json_is_surrogate(value);
}

inline void json_append_utf8(
    std::uint32_t codepoint,
    std::string& output) {

    if (codepoint <= 0x7Fu) {
        output.push_back(
            static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        output.push_back(
            static_cast<char>(
                0xC0u |
                ((codepoint >> 6u) & 0x1Fu)));
        output.push_back(
            static_cast<char>(
                0x80u |
                (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        output.push_back(
            static_cast<char>(
                0xE0u |
                ((codepoint >> 12u) & 0x0Fu)));
        output.push_back(
            static_cast<char>(
                0x80u |
                ((codepoint >> 6u) & 0x3Fu)));
        output.push_back(
            static_cast<char>(
                0x80u |
                (codepoint & 0x3Fu)));
    } else {
        output.push_back(
            static_cast<char>(
                0xF0u |
                ((codepoint >> 18u) & 0x07u)));
        output.push_back(
            static_cast<char>(
                0x80u |
                ((codepoint >> 12u) & 0x3Fu)));
        output.push_back(
            static_cast<char>(
                0x80u |
                ((codepoint >> 6u) & 0x3Fu)));
        output.push_back(
            static_cast<char>(
                0x80u |
                (codepoint & 0x3Fu)));
    }
}

enum class json_utf8_status : std::uint8_t {
    ok,
    truncated,
    invalid_leading_byte,
    invalid_continuation_byte,
    overlong_sequence,
    invalid_codepoint,
};

struct json_utf8_result {
    json_utf8_status status = json_utf8_status::ok;
    std::uint32_t codepoint = 0;
    std::size_t length = 0;
};

[[nodiscard]] constexpr bool json_is_utf8_continuation(
    unsigned char value) noexcept {

    return (value & 0xC0u) == 0x80u;
}

[[nodiscard]] inline json_utf8_result json_decode_utf8_one(
    std::string_view text,
    std::size_t offset) noexcept {

    if (offset >= text.size()) {
        return {
            json_utf8_status::truncated,
            0,
            0,
        };
    }

    const auto first =
        static_cast<unsigned char>(
            text[offset]);

    if (first <= 0x7Fu) {
        return {
            json_utf8_status::ok,
            first,
            1,
        };
    }

    std::size_t length = 0;
    std::uint32_t codepoint = 0;
    std::uint32_t minimum = 0;

    if (first >= 0xC2u &&
        first <= 0xDFu) {
        length = 2;
        codepoint = first & 0x1Fu;
        minimum = 0x80u;
    } else if (
        first >= 0xE0u &&
        first <= 0xEFu) {
        length = 3;
        codepoint = first & 0x0Fu;
        minimum = 0x800u;
    } else if (
        first >= 0xF0u &&
        first <= 0xF4u) {
        length = 4;
        codepoint = first & 0x07u;
        minimum = 0x10000u;
    } else {
        return {
            json_utf8_status::invalid_leading_byte,
            0,
            1,
        };
    }

    if (offset + length > text.size()) {
        return {
            json_utf8_status::truncated,
            0,
            text.size() - offset,
        };
    }

    for (std::size_t i = 1;
         i < length;
         ++i) {

        const auto next =
            static_cast<unsigned char>(
                text[offset + i]);

        if (!json_is_utf8_continuation(next)) {
            return {
                json_utf8_status::invalid_continuation_byte,
                0,
                i,
            };
        }

        codepoint =
            (codepoint << 6u) |
            static_cast<std::uint32_t>(
                next & 0x3Fu);
    }

    if (codepoint < minimum) {
        return {
            json_utf8_status::overlong_sequence,
            codepoint,
            length,
        };
    }

    if (!json_is_unicode_scalar(codepoint)) {
        return {
            json_utf8_status::invalid_codepoint,
            codepoint,
            length,
        };
    }

    return {
        json_utf8_status::ok,
        codepoint,
        length,
    };
}

}
