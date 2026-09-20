#include "directive_decoder.hpp"

#include <limits>

namespace cw::server {
namespace {

[[nodiscard]] constexpr directive_kind decode_kind(
    token_kind kind) noexcept {

    switch (kind) {
    case token_kind::pp_include:
        return directive_kind::include;
    case token_kind::pp_define:
        return directive_kind::define;
    case token_kind::pp_undef:
        return directive_kind::undef;
    case token_kind::pp_if:
        return directive_kind::if_;
    case token_kind::pp_ifdef:
        return directive_kind::ifdef;
    case token_kind::pp_ifndef:
        return directive_kind::ifndef;
    case token_kind::pp_elif:
        return directive_kind::elif;
    case token_kind::pp_else:
        return directive_kind::else_;
    case token_kind::pp_endif:
        return directive_kind::endif;
    case token_kind::pp_line:
        return directive_kind::line;
    case token_kind::pp_error:
        return directive_kind::error;
    case token_kind::pp_pragma:
        return directive_kind::pragma;
    case token_kind::pp_unknown:
        return directive_kind::unknown;
    default:
        return directive_kind::invalid;
    }
}

[[nodiscard]] constexpr include_form decode_include_form(
    token_kind kind) noexcept {

    switch (kind) {
    case token_kind::header_name_quoted:
        return include_form::quoted;
    case token_kind::header_name_angled:
        return include_form::angled;
    default:
        return include_form::none;
    }
}

}

server_status directive_decoder::decode(
    frontend_input& input,
    preprocessing_directive& output) noexcept {

    output = {};

    if (input.empty() ||
        input.finished()) {

        return server_status::project_configuration_invalid;
    }

    const auto file =
        input.current_file();

    const auto word_begin =
        input.word_offset();

    frontend_token first;

    const auto first_status =
        input.next(first);

    if (!succeeded(first_status)) {
        return first_status;
    }

    const auto kind =
        decode_kind(first.kind);

    if (kind == directive_kind::invalid ||
        first.file != file) {

        return server_status::project_configuration_invalid;
    }

    include_directive include;
    bool include_payload_seen = false;

    frontend_token current = first;

    for (;;) {
        const auto before =
            input.word_offset();

        const auto next_status =
            input.next(current);

        if (!succeeded(next_status)) {
            return next_status;
        }

        if (current.file != file) {
            return server_status::project_configuration_invalid;
        }

        if (current.kind == token_kind::pp_end) {
            const auto word_end =
                input.word_offset();

            if (word_end < word_begin ||
                current.source_offset < first.source_offset) {

                return server_status::project_configuration_invalid;
            }

            const auto word_count =
                word_end - word_begin;

            if (word_count == 0) {
                return server_status::project_configuration_invalid;
            }

            output.kind = kind;
            output.range = {
                file,
                word_begin,
                word_count,
                {
                    first.source_offset,
                    current.source_offset -
                        first.source_offset,
                },
            };
            output.include = include;

            return server_status::success;
        }

        if (kind != directive_kind::include) {
            continue;
        }

        const auto form =
            decode_include_form(
                current.kind);

        if (form == include_form::none) {
            continue;
        }

        if (include_payload_seen) {
            return server_status::project_configuration_invalid;
        }

        include_payload_seen = true;
        include.form = form;
        include.locator = {
            current.source_offset,
            current.source_length,
        };

        const auto after =
            input.word_offset();

        if (after <= before) {
            return server_status::project_configuration_invalid;
        }
    }
}

}
