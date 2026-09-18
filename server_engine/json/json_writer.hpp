/*
 * State-checked compact JSON writer.
 *
 * Invalid call ordering fails closed by returning false. The writer never
 * silently emits structurally invalid JSON when its API is used correctly.
 */
#pragma once

#include "json_buffer.hpp"
#include "json_escape.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cw::server {

class json_writer final {
public:
    explicit json_writer(
        std::size_t reserve = 4096)
        : output(reserve) {

        stack.reserve(16);
    }

    void clear() noexcept {
        output.clear();
        stack.clear();
        root_written = false;
        valid_state = true;
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_state;
    }

    [[nodiscard]] bool complete() const noexcept {
        return valid_state &&
               root_written &&
               stack.empty();
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return output.view();
    }

    [[nodiscard]] std::string str() && {
        return std::move(output).str();
    }

    [[nodiscard]] bool begin_object() {
        if (!begin_value()) {
            return false;
        }

        output.push('{');

        stack.push_back({
            container_kind::object,
            false,
            true,
        });

        return true;
    }

    [[nodiscard]] bool end_object() {
        if (stack.empty() ||
            stack.back().kind !=
                container_kind::object ||
            !stack.back().expect_key) {

            return invalidate();
        }

        output.push('}');
        stack.pop_back();
        finish_value();
        return true;
    }

    [[nodiscard]] bool begin_array() {
        if (!begin_value()) {
            return false;
        }

        output.push('[');

        stack.push_back({
            container_kind::array,
            false,
            false,
        });

        return true;
    }

    [[nodiscard]] bool end_array() {
        if (stack.empty() ||
            stack.back().kind !=
                container_kind::array) {

            return invalidate();
        }

        output.push(']');
        stack.pop_back();
        finish_value();
        return true;
    }

    [[nodiscard]] bool key(
        std::string_view value) {

        if (stack.empty()) {
            return invalidate();
        }

        auto& frame =
            stack.back();

        if (frame.kind !=
                container_kind::object ||
            !frame.expect_key) {

            return invalidate();
        }

        if (frame.has_content) {
            output.push(',');
        }

        json_write_escaped_string(
            output,
            value);

        output.push(':');

        frame.expect_key = false;
        return true;
    }

    [[nodiscard]] bool null() {
        return primitive("null");
    }

    [[nodiscard]] bool boolean(
        bool value) {

        return primitive(
            value
                ? "true"
                : "false");
    }

    template <typename T>
    requires std::integral<T>
    [[nodiscard]] bool number(
        T value) {

        if (!begin_value()) {
            return false;
        }

        if (!output.push_integer(value)) {
            return invalidate();
        }

        finish_value();
        return true;
    }

    [[nodiscard]] bool number(
        double value) {

        if (!std::isfinite(value) ||
            !begin_value()) {
            return invalidate();
        }

        char text[64];

        const auto [end, error] =
            std::to_chars(
                text,
                text + sizeof(text),
                value,
                std::chars_format::general);

        if (error != std::errc{}) {
            return invalidate();
        }

        output.push(
            text,
            static_cast<std::size_t>(
                end - text));

        finish_value();
        return true;
    }

    [[nodiscard]] bool string(
        std::string_view value) {

        if (!begin_value()) {
            return false;
        }

        json_write_escaped_string(
            output,
            value);

        finish_value();
        return true;
    }

private:
    enum class container_kind : std::uint8_t {
        object,
        array,
    };

    struct frame {
        container_kind kind;
        bool has_content = false;

        // Object only: true means key() must be called next.
        bool expect_key = false;
    };

    [[nodiscard]] bool invalidate() noexcept {
        valid_state = false;
        return false;
    }

    [[nodiscard]] bool begin_value() {
        if (!valid_state) {
            return false;
        }

        if (stack.empty()) {
            if (root_written) {
                return invalidate();
            }

            root_written = true;
            return true;
        }

        auto& frame =
            stack.back();

        if (frame.kind ==
            container_kind::object) {

            if (frame.expect_key) {
                return invalidate();
            }

            return true;
        }

        if (frame.has_content) {
            output.push(',');
        }

        return true;
    }

    void finish_value() noexcept {
        if (stack.empty()) {
            return;
        }

        auto& frame =
            stack.back();

        frame.has_content = true;

        if (frame.kind ==
            container_kind::object) {
            frame.expect_key = true;
        }
    }

    [[nodiscard]] bool primitive(
        std::string_view value) {

        if (!begin_value()) {
            return false;
        }

        output.push(value);
        finish_value();
        return true;
    }

    json_buffer output;
    std::vector<frame> stack;
    bool root_written = false;
    bool valid_state = true;
};

}
