#include "lexical_stream.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] constexpr bool directive_start(
    token_kind kind) noexcept {

    switch (kind) {
    case token_kind::pp_include:
    case token_kind::pp_define:
    case token_kind::pp_undef:
    case token_kind::pp_if:
    case token_kind::pp_ifdef:
    case token_kind::pp_ifndef:
    case token_kind::pp_elif:
    case token_kind::pp_else:
    case token_kind::pp_endif:
    case token_kind::pp_line:
    case token_kind::pp_error:
    case token_kind::pp_pragma:
    case token_kind::pp_unknown:
        return true;

    default:
        return false;
    }
}

}

server_status lexical_stream::reserve_words(
    std::size_t required) noexcept {

    if (required <= value_capacity) {
        return server_status::success;
    }

    auto capacity =
        value_capacity == 0
            ? std::size_t{16}
            : value_capacity;

    while (capacity < required) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            capacity = required;
            break;
        }

        capacity *= 2;
    }

    std::unique_ptr<std::uint32_t[]> candidate{
        new (std::nothrow)
            std::uint32_t[capacity]};

    if (!candidate) {
        return server_status::io_error;
    }

    if (value_count != 0) {
        std::copy_n(
            values.get(),
            value_count,
            candidate.get());
    }

    values =
        std::move(candidate);

    value_capacity =
        capacity;

    return server_status::success;
}

server_status lexical_stream::reserve_directives(
    std::size_t required) noexcept {

    if (required <= directive_capacity) {
        return server_status::success;
    }

    auto capacity =
        directive_capacity == 0
            ? std::size_t{8}
            : directive_capacity;

    while (capacity < required) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            capacity = required;
            break;
        }

        capacity *= 2;
    }

    std::unique_ptr<lexical_directive_anchor[]> candidate{
        new (std::nothrow)
            lexical_directive_anchor[capacity]};

    if (!candidate) {
        return server_status::io_error;
    }

    if (directive_count_value != 0) {
        std::copy_n(
            directive_values.get(),
            directive_count_value,
            candidate.get());
    }

    directive_values =
        std::move(candidate);

    directive_capacity =
        capacity;

    return server_status::success;
}

server_status lexical_stream::reset(
    file_id file,
    std::size_t source_size_value) noexcept {

    if (!file ||
        source_size_value >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::project_configuration_invalid;
    }

    const auto estimated =
        source_size_value / 4 + 16;

    const auto reserved =
        reserve_words(
            estimated);

    if (!succeeded(reserved)) {
        source_file = {};
        value_count = 0;
        directive_count_value = 0;
        source_size = 0;
        last_source_offset = 0;
        tokens = 0;
        return reserved;
    }

    source_file = file;
    value_count = 0;
    directive_count_value = 0;
    source_size =
        static_cast<std::uint32_t>(
            source_size_value);
    last_source_offset = 0;
    tokens = 0;

    return server_status::success;
}

server_status lexical_stream::append(
    token_kind kind,
    std::uint32_t source_offset_value,
    std::uint32_t source_length_value) noexcept {

    if (!source_file ||
        kind == token_kind::invalid ||
        source_offset_value > source_size ||
        source_length_value >
            source_size - source_offset_value ||
        (tokens != 0 &&
         source_offset_value < last_source_offset)) {

        return server_status::project_configuration_invalid;
    }

    if (tokens ==
        (std::numeric_limits<std::uint32_t>::max)()) {

        return server_status::io_error;
    }

    const auto delta =
        tokens == 0
            ? source_offset_value
            : source_offset_value -
                last_source_offset;

    const auto extended_delta =
        delta >
        lexical_token::inline_delta_max;

    const auto extended_length =
        source_length_value >
        lexical_token::inline_length_max;

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

    const auto required_words =
        std::size_t{1} +
        static_cast<std::size_t>(
            extended_delta) +
        static_cast<std::size_t>(
            extended_length);

    if (!header ||
        value_count >
            (std::numeric_limits<std::size_t>::max)() -
                required_words) {

        return server_status::io_error;
    }

    const auto is_directive =
        directive_start(kind);

    if (is_directive) {
        if (value_count >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)()) ||
            directive_count_value ==
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {

            return server_status::io_error;
        }

        const auto directive_reserved =
            reserve_directives(
                directive_count_value + 1);

        if (!succeeded(directive_reserved)) {
            return directive_reserved;
        }
    }

    const auto word_reserved =
        reserve_words(
            value_count +
            required_words);

    if (!succeeded(word_reserved)) {
        return word_reserved;
    }

    if (is_directive) {
        directive_values[
            directive_count_value++] = {
                static_cast<std::uint32_t>(
                    value_count),
                tokens == 0
                    ? 0u
                    : last_source_offset,
            };
    }

    values[value_count++] =
        header.value();

    if (extended_delta) {
        values[value_count++] =
            delta;
    }

    if (extended_length) {
        values[value_count++] =
            source_length_value;
    }

    last_source_offset =
        source_offset_value;

    ++tokens;

    return server_status::success;
}

}
