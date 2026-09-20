#include "frontend_input.hpp"

namespace cw::server {

server_status frontend_input::start(
    file_id root) noexcept {

    stack_size = 0;
    return enter(root);
}

server_status frontend_input::enter(
    file_id file) noexcept {

    if (!files.contains(file) ||
        !files.content_available(file) ||
        stack_size == stack.size()) {

        return server_status::project_configuration_invalid;
    }

    stack[stack_size++] = {
        file,
        0,
    };

    return server_status::success;
}

server_status frontend_input::advance(
    std::size_t count) noexcept {

    if (stack_size == 0) {
        return server_status::project_configuration_invalid;
    }

    const auto bytes =
        files.content(stack[stack_size - 1].file);

    const auto current =
        static_cast<std::size_t>(
            stack[stack_size - 1].offset);

    if (current > bytes.size() ||
        count > bytes.size() - current) {

        return server_status::project_configuration_invalid;
    }

    stack[stack_size - 1].offset +=
        static_cast<std::uint32_t>(count);

    return server_status::success;
}

server_status frontend_input::leave() noexcept {

    if (stack_size == 0 ||
        !finished()) {

        return server_status::project_configuration_invalid;
    }

    --stack_size;
    return server_status::success;
}

file_id frontend_input::current_file() const noexcept {

    return stack_size == 0
        ? file_id{}
        : stack[stack_size - 1].file;
}

std::size_t frontend_input::offset() const noexcept {

    return stack_size == 0
        ? 0
        : stack[stack_size - 1].offset;
}

std::string_view frontend_input::remaining() const noexcept {

    if (stack_size == 0) {
        return {};
    }

    const auto& current =
        stack[stack_size - 1];

    const auto bytes =
        files.content(current.file);

    const auto position =
        static_cast<std::size_t>(
            current.offset);

    if (position > bytes.size()) {
        return {};
    }

    return bytes.substr(position);
}

bool frontend_input::finished() const noexcept {

    if (stack_size == 0) {
        return false;
    }

    const auto bytes =
        files.content(stack[stack_size - 1].file);

    return static_cast<std::size_t>(
               stack[stack_size - 1].offset) ==
        bytes.size();
}

}
