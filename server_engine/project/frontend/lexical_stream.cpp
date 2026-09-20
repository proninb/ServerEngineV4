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
    source_size = static_cast<std::uint32_t>(source_size_value);
    last_source_offset = 0;
    tokens = 0;

    try {
        const auto estimated =
            source_size_value / 4 + 16;

        if (values.capacity() < estimated) {
            values.reserve(estimated);
        }
    }
    catch (...) {
        source_file = {};
        values.clear();
        source_size = 0;
        last_source_offset = 0;
        tokens = 0;
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
        (tokens != 0 &&
         source_offset_value < last_source_offset)) {

        return server_status::project_configuration_invalid;
    }

    if (tokens ==
        (std::numeric_limits<std::uint32_t>::max)()) {

        return server_status::io_error;
    }

    const auto delta = tokens == 0
        ? source_offset_value
        : source_offset_value - last_source_offset;

    const auto extended_delta =
        delta > lexical_token::inline_delta_max;

    const auto extended_length =
        source_length_value > lexical_token::inline_length_max;

    const auto encoded_delta =
        extended_delta
            ? lexical_token::extended_delta
            : delta;

    const auto encoded_length =
        extended_length
            ? lexical_token::extended_length
            : source_length_value;

    const auto header =
        lexical_token::make(
            kind,
            encoded_delta,
            encoded_length);

    const std::size_t required_words =
        1 +
        static_cast<std::size_t>(extended_delta) +
        static_cast<std::size_t>(extended_length);

    if (!header ||
        values.size() >
            (std::numeric_limits<std::size_t>::max)() - required_words) {

        return server_status::io_error;
    }

    try {
        const auto required_capacity =
            values.size() + required_words;

        if (values.capacity() < required_capacity) {
            const auto doubled =
                values.capacity() <=
                    (std::numeric_limits<std::size_t>::max)() / 2
                    ? values.capacity() * 2
                    : values.capacity();

            values.reserve(
                (std::max)(
                    required_capacity,
                    doubled));
        }

        values.push_back(header.value());

        if (extended_delta) {
            values.push_back(delta);
        }

        if (extended_length) {
            values.push_back(source_length_value);
        }
    }
    catch (...) {
        source_file = {};
        values.clear();
        source_size = 0;
        last_source_offset = 0;
        tokens = 0;
        return server_status::io_error;
    }

    last_source_offset = source_offset_value;
    ++tokens;
    return server_status::success;
}

}
