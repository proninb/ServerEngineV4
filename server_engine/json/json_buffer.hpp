/*
 * Small owned output buffer used by JSON writers.
 */
#pragma once

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cw::server {

class json_buffer final {
public:
    explicit json_buffer(
        std::size_t reserve = 4096) {

        value.reserve(reserve);
    }

    void clear() noexcept {
        value.clear();
    }

    void reserve(
        std::size_t capacity) {

        value.reserve(capacity);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return value.size();
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return value;
    }

    [[nodiscard]] std::string str() && {
        return std::move(value);
    }

    void push(char ch) {
        value.push_back(ch);
    }

    void push(
        std::string_view text) {

        value.append(text);
    }

    void push(
        const char* text,
        std::size_t size) {

        value.append(
            text,
            size);
    }

    template <typename T>
    [[nodiscard]] bool push_integer(
        T input) {

        char text[32];

        const auto [end, error] =
            std::to_chars(
                text,
                text + sizeof(text),
                input);

        if (error != std::errc{}) {
            return false;
        }

        value.append(
            text,
            end);

        return true;
    }

private:
    std::string value;
};

}
