/*
 * Parser V2 semantic cursor over prepared lexical input.
 *
 * Replay performs only compact token decoding plus direct
 * local_symbol_id -> string_id resolution. It owns no source text, filesystem,
 * lexer, string table, spelling hash, or preprocessing state.
 */
#pragma once

#include "prepared_frontend_v2.hpp"
#include "prepared_token.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace cw::server {

class semantic_cursor_v2 final {
public:
    semantic_cursor_v2() noexcept = default;

    [[nodiscard]] server_status start(
        prepared_lexical_input_v2 value) noexcept {

        input = {};
        current_value = {};
        word_offset = 0;
        source_offset = 0;
        identifier_occurrence = 0;
        number_occurrence = 0;
        token_offset = 0;
        started = false;
        current_valid = false;

        if (!value.valid()) {
            return server_status::
                project_configuration_invalid;
        }

        input = value;
        started = true;

        if (input.words.empty()) {
            return input.symbols->identifier_count() == 0 &&
                input.literals->number_count() == 0
                ? server_status::success
                : server_status::
                    project_configuration_invalid;
        }

        return decode_current();
    }

    [[nodiscard]] bool finished() const noexcept {
        return started &&
            !current_valid &&
            word_offset == input.words.size();
    }

    [[nodiscard]] const prepared_token* current() const noexcept {
        return current_valid
            ? &current_value
            : nullptr;
    }

    [[nodiscard]] bool at(
        token_kind kind) const noexcept {

        return current_valid &&
            current_value.kind == kind;
    }

    [[nodiscard]] server_status advance() noexcept {
        if (!started ||
            !current_valid) {

            return server_status::
                project_configuration_invalid;
        }

        current_valid = false;
        current_value = {};
        ++token_offset;

        if (word_offset ==
            input.words.size()) {

            return identifier_occurrence ==
                    input.symbols->identifier_count() &&
                number_occurrence ==
                    input.literals->number_count()
                ? server_status::success
                : server_status::
                    project_configuration_invalid;
        }

        return decode_current();
    }

    [[nodiscard]] std::size_t offset() const noexcept {
        return token_offset;
    }

private:
    [[nodiscard]] server_status decode_current() noexcept {
        auto position =
            word_offset;

        if (position >=
            input.words.size()) {

            return server_status::
                project_configuration_invalid;
        }

        const auto header =
            lexical_token::from_value(
                input.words[position++]);

        if (!header) {
            return server_status::
                project_configuration_invalid;
        }

        auto delta =
            header.delta();

        if (delta ==
            lexical_token::extended_delta) {

            if (position >=
                input.words.size()) {

                return server_status::
                    project_configuration_invalid;
            }

            delta =
                input.words[position++];
        }

        auto length =
            header.length();

        if (length ==
            lexical_token::extended_length) {

            if (position >=
                input.words.size()) {

                return server_status::
                    project_configuration_invalid;
            }

            length =
                input.words[position++];
        }

        const auto first =
            token_offset == 0;

        if (!first &&
            delta >
                (std::numeric_limits<std::uint32_t>::max)() -
                    source_offset) {

            return server_status::
                project_configuration_invalid;
        }

        const auto source =
            first
                ? delta
                : source_offset + delta;

        if (source >
                input.symbols->source_size() ||
            length >
                input.symbols->source_size() -
                    source) {

            return server_status::
                project_configuration_invalid;
        }

        string_id identifier;
        prepared_number_v2 number;

        if (header.kind() ==
            token_kind::identifier) {

            if (identifier_occurrence >=
                input.symbols->identifier_count()) {

                return server_status::
                    project_configuration_invalid;
            }

            identifier =
                input.resolution->
                    resolve_identifier(
                        *input.symbols,
                        identifier_occurrence);

            if (!identifier) {
                return server_status::
                    project_configuration_invalid;
            }

            ++identifier_occurrence;
        }
        else if (header.kind() ==
            token_kind::pp_number) {

            if (number_occurrence >=
                input.literals->
                    number_count()) {

                return server_status::
                    project_configuration_invalid;
            }

            number =
                input.literals->
                    number(
                        number_occurrence++);

            if (!number.valid()) {
                // Invalid/unsupported pp-number spelling is retained as an
                // invalid semantic numeric value; the Parser owns diagnostics.
                number = {};
            }
        }

        word_offset =
            position;

        source_offset =
            source;

        current_value = {
            input.file,
            header.kind(),
            source,
            length,
            identifier,
            number,
        };

        current_valid = true;

        return server_status::success;
    }

    prepared_lexical_input_v2 input;
    prepared_token current_value;

    std::size_t word_offset = 0;
    std::uint32_t source_offset = 0;
    std::size_t identifier_occurrence = 0;
    std::size_t number_occurrence = 0;
    std::size_t token_offset = 0;

    bool started = false;
    bool current_valid = false;
};

}
