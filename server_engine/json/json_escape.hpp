/*
 * JSON string escaping shared by compact/formatted writers.
 */
#pragma once

#include "json_buffer.hpp"

#include <string_view>

namespace cw::server {
namespace detail {

inline constexpr char json_hex_upper[] =
    "0123456789ABCDEF";

inline void json_write_u00_escape(
    json_buffer& output,
    unsigned char value) {

    output.push('\\');
    output.push('u');
    output.push('0');
    output.push('0');
    output.push(
        json_hex_upper[
            (value >> 4) & 0x0F]);
    output.push(
        json_hex_upper[
            value & 0x0F]);
}

}

inline void json_write_escaped_string(
    json_buffer& output,
    std::string_view text) {

    output.push('"');

    for (const unsigned char value :
         text) {

        switch (value) {
        case '"':
            output.push("\\\"", 2);
            break;
        case '\\':
            output.push("\\\\", 2);
            break;
        case '\b':
            output.push("\\b", 2);
            break;
        case '\f':
            output.push("\\f", 2);
            break;
        case '\n':
            output.push("\\n", 2);
            break;
        case '\r':
            output.push("\\r", 2);
            break;
        case '\t':
            output.push("\\t", 2);
            break;
        default:
            if (value < 0x20u) {
                detail::json_write_u00_escape(
                    output,
                    value);
            } else {
                output.push(
                    static_cast<char>(value));
            }
            break;
        }
    }

    output.push('"');
}

}
