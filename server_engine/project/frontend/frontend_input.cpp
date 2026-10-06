#include "frontend_input.hpp"

#include <limits>

namespace cw::server {

server_status frontend_input::start(
    file_id root) noexcept {

    stack_size = 0;
    return enter(root);
}

server_status frontend_input::start_at(
    file_id file,
    std::uint32_t word_offset_value,
    std::uint32_t source_base) noexcept {

    stack_size = 0;

    if (!lexical.contains(file)) {
        return server_status::project_configuration_invalid;
    }

    const auto words =
        lexical.words(file);

    if (static_cast<std::size_t>(
            word_offset_value) >=
        words.size()) {

        return server_status::project_configuration_invalid;
    }

    stack[0] = {
        file,
        word_offset_value,
        source_base,
        words,
    };

    stack_size = 1;

    return server_status::success;
}

server_status frontend_input::enter(
    file_id file) noexcept {

    if (!lexical.contains(file) ||
        stack_size == stack.size()) {

        return server_status::project_configuration_invalid;
    }

    const auto words =
        lexical.words(file);

    stack[stack_size++] = {
        file,
        0,
        0,
        words,
    };

    return server_status::success;
}

server_status frontend_input::next(
    frontend_token& output) noexcept {

    output = {};

    if (stack_size == 0) {
        return server_status::project_configuration_invalid;
    }

    auto& current =
        stack[stack_size - 1];

    const auto& words =
        current.words;

    return decode_frontend_token(words, current.file, current.word_offset,
        current.source_offset, output);
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

std::uint32_t frontend_input::word_offset() const noexcept {

    return stack_size == 0
        ? 0
        : stack[stack_size - 1].word_offset;
}

std::uint32_t frontend_input::source_offset() const noexcept {

    return stack_size == 0
        ? 0
        : stack[stack_size - 1].source_offset;
}

bool frontend_input::finished() const noexcept {

    if (stack_size == 0) {
        return false;
    }

    const auto& current =
        stack[stack_size - 1];

    return static_cast<std::size_t>(
               current.word_offset) ==
        current.words.size();
}

}
