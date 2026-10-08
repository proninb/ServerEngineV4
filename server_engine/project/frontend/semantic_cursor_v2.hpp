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
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
#include <chrono>
#endif
#include <limits>

namespace cw::server {

#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
// PARSER-V2-CURSOR-PERF-07: only benchmark builds see these counters/probes.
// Every 128th invocation is timed; call counts are exact. Nested probes
// MUST NOT be summed into an exclusive phase breakdown.
struct cursor_v2_perf07_counter final {
    std::uint64_t calls = 0;
    std::uint64_t samples = 0;
    double sample_ms = 0;
    // PARSER-V2-CURSOR-DECODE-09: used by the decode counter only.
    std::uint64_t perf09_inline_tokens = 0;
    std::uint64_t perf09_fallback_tokens = 0;
    std::uint64_t perf09_extended_delta_tokens = 0;
    std::uint64_t perf09_extended_length_tokens = 0;
};

struct cursor_v2_perf07_probe final {
    cursor_v2_perf07_counter* measured = nullptr;
    std::chrono::steady_clock::time_point started{};

    explicit cursor_v2_perf07_probe(cursor_v2_perf07_counter* counter) noexcept {
        if (counter == nullptr) return;
        const auto n = ++counter->calls;
        if ((n & 127u) != 0) return;
        ++counter->samples;
        measured = counter;
        started = std::chrono::steady_clock::now();
    }

    ~cursor_v2_perf07_probe() noexcept {
        if (measured != nullptr) {
            measured->sample_ms += std::chrono::duration<double, std::milli>{
                std::chrono::steady_clock::now() - started}.count();
        }
    }
};
#endif

class semantic_cursor_v2 final {
public:
    semantic_cursor_v2() noexcept = default;

    [[nodiscard]] server_status start(
        prepared_lexical_input_v2 value
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        , cursor_v2_perf07_counter* decode_profile_value = nullptr
        , cursor_v2_perf07_counter* advance_profile_value = nullptr
#endif
        ) noexcept {
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        decode_profile = decode_profile_value;
        advance_profile = advance_profile_value;
#endif
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
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        cursor_v2_perf07_probe perf07_probe(advance_profile);
#endif
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
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        cursor_v2_perf07_probe perf07_probe(decode_profile);
#endif
#if defined(CW_PARSER_V2_DECODE09_REFERENCE)
        // PARSER-V2-CURSOR-AB-DIRECTIVE-10: exact pre-DECODE-09 baseline.
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
#else
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

        auto delta = header.delta();
        auto length = header.length();
        const auto source_size =
            input.symbols->source_size();

        // PARSER-V2-CURSOR-DECODE-09: ordinary tokens have two in-word
        // fields. Their previous source offset was already validated.
        // Validate delta and length via subtractions (without overflow).
        const bool ordinary =
            token_offset != 0 &&
            delta != lexical_token::extended_delta &&
            length != lexical_token::extended_length;

#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
        if (decode_profile != nullptr) {
            if (ordinary) ++decode_profile->perf09_inline_tokens;
            else ++decode_profile->perf09_fallback_tokens;
            if (delta == lexical_token::extended_delta)
                ++decode_profile->perf09_extended_delta_tokens;
            if (length == lexical_token::extended_length)
                ++decode_profile->perf09_extended_length_tokens;
        }
#endif

        std::uint32_t source = 0;
        if (ordinary) {
            // Invariant: every successful earlier token set
            // source_offset <= source_size. Check explicitly anyway so
            // malformed prepared streams fail safely.
            if (source_offset > source_size ||
                delta > source_size - source_offset) {
                return server_status::project_configuration_invalid;
            }
            source = source_offset + delta;
            if (length > source_size - source) {
                return server_status::project_configuration_invalid;
            }
        }
        else {
            // First token, extended delta or extended length: preserve
            // the existing checked decoder and extension-word order.
            if (delta == lexical_token::extended_delta) {
                if (position >= input.words.size()) {
                    return server_status::project_configuration_invalid;
                }
                delta = input.words[position++];
            }
            if (length == lexical_token::extended_length) {
                if (position >= input.words.size()) {
                    return server_status::project_configuration_invalid;
                }
                length = input.words[position++];
            }
            const auto first = token_offset == 0;
            if (!first &&
                delta > (std::numeric_limits<std::uint32_t>::max)() -
                    source_offset) {
                return server_status::project_configuration_invalid;
            }
            source = first ? delta : source_offset + delta;
            if (source > source_size ||
                length > source_size - source) {
                return server_status::project_configuration_invalid;
            }
        }

        string_id identifier;
#endif
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
#ifdef CW_HEADER_V2_SEMANTIC_PROFILE
    cursor_v2_perf07_counter* decode_profile = nullptr;
    cursor_v2_perf07_counter* advance_profile = nullptr;
#endif
};

}
