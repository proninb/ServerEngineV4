#include "lexical_stream.hpp"

#include <algorithm>
#include <limits>

namespace cw::server {

server_status lexical_stream::reset(
    file_id file,
    std::size_t source_size_value) noexcept {

    if (!file ||
        source_size_value >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::project_configuration_invalid;
    }

    source_file = file;
    values.clear();
    checkpoints.clear();
    extended_tokens.clear();
    source_size = static_cast<std::uint32_t>(source_size_value);
    last_source_offset = 0;

    try {
        const auto estimated =
            source_size_value / 4 + 16;

        if (values.capacity() < estimated) {
            values.reserve(estimated);
        }

        const auto checkpoint_estimate =
            estimated / lexical_checkpoint_stride + 1;

        if (checkpoints.capacity() < checkpoint_estimate) {
            checkpoints.reserve(checkpoint_estimate);
        }
    }
    catch (...) {
        source_file = {};
        values.clear();
        checkpoints.clear();
        extended_tokens.clear();
        source_size = 0;
        last_source_offset = 0;
        return server_status::io_error;
    }

    return server_status::success;
}

server_status lexical_stream::append(
    token_kind kind,
    std::uint32_t source_offset_value,
    std::uint32_t source_length_value) noexcept {

    if (!source_file ||
        kind == token_kind::invalid ||
        source_offset_value > source_size ||
        source_length_value > source_size - source_offset_value ||
        (!values.empty() &&
         source_offset_value < last_source_offset)) {

        return server_status::project_configuration_invalid;
    }

    if (values.size() >=
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::io_error;
    }

    const auto token_index =
        static_cast<std::uint32_t>(values.size());

    const auto delta = values.empty()
        ? source_offset_value
        : source_offset_value - last_source_offset;

    const auto encoded_delta =
        delta <= lexical_token::inline_delta_max
            ? delta
            : lexical_token::extended_delta;

    const auto encoded_length =
        source_length_value <= lexical_token::inline_length_max
            ? source_length_value
            : lexical_token::extended_length;

    try {
        if ((token_index % lexical_checkpoint_stride) == 0) {
            checkpoints.push_back({
                token_index,
                source_offset_value,
            });
        }

        if (encoded_delta == lexical_token::extended_delta ||
            encoded_length == lexical_token::extended_length) {

            extended_tokens.push_back({
                token_index,
                delta,
                source_length_value,
            });
        }

        values.push_back(
            lexical_token::make(
                kind,
                encoded_delta,
                encoded_length));
    }
    catch (...) {
        values.clear();
        checkpoints.clear();
        extended_tokens.clear();
        source_file = {};
        source_size = 0;
        last_source_offset = 0;
        return server_status::io_error;
    }

    last_source_offset = source_offset_value;
    return server_status::success;
}

const lexical_extended_token* lexical_stream::extended_at(
    std::size_t token_index) const noexcept {

    const auto key =
        static_cast<std::uint32_t>(token_index);

    const auto position =
        std::lower_bound(
            extended_tokens.begin(),
            extended_tokens.end(),
            key,
            [](const lexical_extended_token& value,
               std::uint32_t expected) {
                return value.token_index < expected;
            });

    return position != extended_tokens.end() &&
           position->token_index == key
        ? &*position
        : nullptr;
}

std::uint32_t lexical_stream::delta_at(
    std::size_t token_index) const noexcept {

    const auto encoded =
        values[token_index].delta();

    if (encoded != lexical_token::extended_delta) {
        return encoded;
    }

    const auto* extended =
        extended_at(token_index);

    return extended != nullptr
        ? extended->delta
        : 0;
}

std::uint32_t lexical_stream::length_at(
    std::size_t token_index) const noexcept {

    const auto encoded =
        values[token_index].length();

    if (encoded != lexical_token::extended_length) {
        return encoded;
    }

    const auto* extended =
        extended_at(token_index);

    return extended != nullptr
        ? extended->length
        : 0;
}

server_status lexical_stream::source_offset(
    std::size_t token_index,
    std::uint32_t& output) const noexcept {

    output = 0;

    if (token_index >= values.size()) {
        return server_status::project_configuration_invalid;
    }

    const auto checkpoint_index =
        token_index / lexical_checkpoint_stride;

    if (checkpoint_index >= checkpoints.size()) {
        return server_status::project_configuration_invalid;
    }

    const auto& checkpoint =
        checkpoints[checkpoint_index];

    auto offset =
        checkpoint.source_offset;

    for (std::size_t index =
             static_cast<std::size_t>(checkpoint.token_index) + 1;
         index <= token_index;
         ++index) {

        offset += delta_at(index);
    }

    output = offset;
    return server_status::success;
}

server_status lexical_stream::source_length(
    std::size_t token_index,
    std::uint32_t& output) const noexcept {

    output = 0;

    if (token_index >= values.size()) {
        return server_status::project_configuration_invalid;
    }

    output = length_at(token_index);
    return server_status::success;
}

}
