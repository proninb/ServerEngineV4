/*
 * Typed, non-owning view over one scalar JSON value.
 *
 * String values reference parser-owned decoded storage and are valid only for
 * the duration of the json_event_handler::value() callback.
 *
 * Numeric values retain their original token so get<T>() performs direct,
 * range-checked conversion to the requested C++ type.
 */
#pragma once

#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace cw::server {

enum class json_value_kind : std::uint8_t {
    string,
    integer,
    number,
    boolean,
    null_value,
};

class json_parser_impl;

// Read-only view over one scalar JSON value.
class json_value_view final {
public:
    [[nodiscard]] constexpr json_value_kind kind() const noexcept {
        return kind_value;
    }

    [[nodiscard]] constexpr bool is_null() const noexcept {
        return kind_value == json_value_kind::null_value;
    }

    // Extracts a decoded JSON string without allocation.
    [[nodiscard]] bool get(
        std::string_view& output) const noexcept {

        if (kind_value != json_value_kind::string) {
            return false;
        }

        output = text_value;
        return true;
    }

    // Copies a decoded JSON string.
    [[nodiscard]] bool get(
        std::string& output) const {

        if (kind_value != json_value_kind::string) {
            return false;
        }

        output.assign(
            text_value.data(),
            text_value.size());

        return true;
    }

    // Extracts a JSON boolean.
    [[nodiscard]] bool get(
        bool& output) const noexcept {

        if (kind_value != json_value_kind::boolean) {
            return false;
        }

        output = boolean_value;
        return true;
    }

    // Converts an integral JSON token directly to the requested C++ type.
    template <std::integral T>
    requires (!std::same_as<std::remove_cv_t<T>, bool>)
    [[nodiscard]] bool get(
        T& output) const noexcept {

        if (kind_value != json_value_kind::integer) {
            return false;
        }

        T candidate{};

        const auto parsed =
            std::from_chars(
                text_value.data(),
                text_value.data() +
                    text_value.size(),
                candidate);

        if (parsed.ec != std::errc{} ||
            parsed.ptr !=
                text_value.data() +
                    text_value.size()) {
            return false;
        }

        output = candidate;
        return true;
    }

    // Converts an integer/number token to the requested floating-point type.
    template <std::floating_point T>
    [[nodiscard]] bool get(
        T& output) const noexcept {

        if (kind_value != json_value_kind::integer &&
            kind_value != json_value_kind::number) {
            return false;
        }

        T candidate{};

        const auto parsed =
            std::from_chars(
                text_value.data(),
                text_value.data() +
                    text_value.size(),
                candidate,
                std::chars_format::general);

        if (parsed.ec != std::errc{} ||
            parsed.ptr !=
                text_value.data() +
                    text_value.size() ||
            !std::isfinite(candidate)) {
            return false;
        }

        output = candidate;
        return true;
    }

private:
    friend class json_parser_impl;

    constexpr json_value_view(
        json_value_kind kind,
        std::string_view text,
        bool boolean) noexcept
        : kind_value(kind),
          text_value(text),
          boolean_value(boolean) {
    }

    json_value_kind kind_value =
        json_value_kind::null_value;

    // Decoded string text or original numeric token.
    std::string_view text_value;

    bool boolean_value = false;
};

}
