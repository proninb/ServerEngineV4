#include "semantic_input.hpp"

#include "../../filesystem_path.hpp"

#include <filesystem>
#include <string_view>

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

[[nodiscard]] bool source_view(
    const file_context& files,
    file_id file,
    std::uint32_t offset,
    std::uint32_t length,
    std::string_view& output) noexcept {

    output = {};

    if (!files.contains(file) ||
        !files.content_available(file)) {

        return false;
    }

    const auto source =
        files.content(file);

    const auto begin =
        static_cast<std::size_t>(
            offset);

    const auto count =
        static_cast<std::size_t>(
            length);

    if (begin > source.size() ||
        count > source.size() - begin) {

        return false;
    }

    output =
        source.substr(
            begin,
            count);

    return true;
}

}

semantic_input::semantic_input(
    file_context& files_value,
    lexical_generation& lexical_value,
    const preprocessor_configuration& configuration_value,
    string_table& strings_value) noexcept
    : files(files_value),
      lexical(lexical_value),
      configuration(configuration_value),
      strings(strings_value),
      input(lexical),
      directive_input(lexical),
      preprocessing(strings),
      executor(strings, preprocessing) {
}

server_status semantic_input::fail(
    file_id file,
    source_range source,
    std::string_view detail,
    server_status status,
    semantic_input_failure_kind kind) noexcept {

    failure_value = {
        kind,
        file,
        source,
        {},
        detail,
    };

    return status;
}

server_status semantic_input::start(
    file_id root,
    semantic_input_mode mode) noexcept {

    failure_value = {};
    mode_value = mode;
    started = false;
    finished_value = false;

    const auto expected_kind =
        mode == semantic_input_mode::header
        ? file_kind::header
        : file_kind::source;

    if (!root ||
        !files.contains(root) ||
        files.kind(root) != expected_kind ||
        !lexical.contains(root)) {

        return fail(
            root,
            {},
            "Semantic frontend root does not match its syntax domain",
            server_status::project_configuration_invalid);
    }

    preprocessing.reset();
    executor.reset();

    const auto opened =
        input.start(root);

    if (!succeeded(opened)) {
        return fail(
            root,
            {},
            "Semantic lexical input could not start",
            opened);
    }

    if (mode == semantic_input_mode::source) {
        started = true;
        return server_status::success;
    }

    const auto initialized =
        initialize_preprocessor(
            configuration,
            strings,
            preprocessing);

    if (!succeeded(initialized)) {
        return fail(
            root,
            {},
            "Root preprocessor configuration could not be initialized",
            initialized);
    }

    const auto execution_entered =
        executor.enter_file(root);

    if (!succeeded(execution_entered)) {
        return fail(
            root,
            {},
            "Preprocessing execution could not enter semantic root",
            execution_entered);
    }

    started = true;
    return server_status::success;
}

server_status semantic_input::materialize_and_lex(
    file_id file) noexcept {

    if (!files.contains(file) ||
        files.kind(file) != file_kind::header) {

        return fail(
            file,
            {},
            "Included Header identity is invalid",
            server_status::project_configuration_invalid);
    }

    if (!files.content_available(file)) {
        file_acquire_job job;

        const auto prepared =
            files.prepare_acquire(
                file,
                job);

        if (!succeeded(prepared)) {
            return fail(
                file,
                {},
                "Included Header acquisition could not be prepared",
                prepared);
        }

        file_acquire_result result;

        file_context::execute_acquire(
            job,
            result);

        server_status acquired =
            server_status::io_error;

        switch (result.kind) {
        case file_acquire_result_kind::present:
            acquired =
                server_status::success;
            break;

        case file_acquire_result_kind::missing:
            acquired =
                server_status::
                    project_configuration_invalid;
            break;

        case file_acquire_result_kind::unchanged:
            acquired =
                server_status::
                    project_artifact_invalid;
            break;

        case file_acquire_result_kind::changed_during_read:
        case file_acquire_result_kind::failed:
        case file_acquire_result_kind::allocation_failed:
            acquired =
                server_status::io_error;
            break;
        }

        if (!succeeded(acquired)) {
            return fail(
                file,
                {},
                "Included Header could not be materialized",
                acquired);
        }

        bool content_changed = false;

        const auto applied =
            files.apply_acquire(
                result,
                content_changed);

        if (!succeeded(applied) ||
            !files.content_available(file)) {

            return fail(
                file,
                {},
                "Included Header materialization could not be published",
                succeeded(applied)
                    ? server_status::
                        project_artifact_invalid
                    : applied);
        }
    }

    if (lexical.contains(file)) {
        return server_status::success;
    }

    const auto extended =
        lexical.extend(
            files.size());

    if (!succeeded(extended)) {
        return fail(
            file,
            {},
            "Lexical storage could not be extended for included Header",
            extended);
    }

    lexical_error error;

    const auto tokenized =
        lexer::tokenize(
            file,
            files.content(file),
            include_stream,
            &error);

    if (!succeeded(tokenized)) {
        failure_value = {
            semantic_input_failure_kind::
                lexical,
            file,
            {
                error.offset,
                error.length,
            },
            error,
            lexical_error_message(
                error.reason),
        };

        return tokenized;
    }

    const auto published =
        lexical.publish(
            file,
            0,
            include_stream);

    if (!succeeded(published)) {
        return fail(
            file,
            {},
            "Included Header lexical state could not be published",
            published);
    }

    return server_status::success;
}

server_status semantic_input::resolve_include(
    const include_request& request,
    file_id& output) noexcept {

    output = {};

    if (request.form != include_form::quoted ||
        request.locator.length < 2) {

        return fail(
            request.source,
            request.locator,
            "Only direct quoted includes are supported by semantic preprocessing",
            server_status::project_configuration_invalid);
    }

    std::string_view spelling;

    if (!source_view(
            files,
            request.source,
            request.locator.offset,
            request.locator.length,
            spelling) ||
        spelling.size() < 2 ||
        spelling.front() != '"' ||
        spelling.back() != '"') {

        return fail(
            request.source,
            request.locator,
            "Included Header spelling is invalid",
            server_status::project_configuration_invalid);
    }

    const auto locator_text =
        spelling.substr(
            1,
            spelling.size() - 2);

    if (locator_text.empty()) {
        return fail(
            request.source,
            request.locator,
            "Included Header path is empty",
            server_status::project_configuration_invalid);
    }

    std::filesystem::path locator;

    if (filesystem_path_from_utf8(
            locator_text,
            locator) !=
        filesystem_path_result::success) {

        return fail(
            request.source,
            request.locator,
            "Included Header path could not be represented",
            server_status::project_configuration_invalid);
    }

    try {
        const auto source_path_view =
            files.path(
                request.source);

        const std::filesystem::path source_path{
            source_path_view.begin(),
            source_path_view.end()};

        const auto candidate =
            source_path.parent_path() /
            locator;

        const auto resolved =
            files.resolve(
                candidate,
                file_kind::header,
                output);

        if (!succeeded(resolved) ||
            !output) {

            output = {};

            return fail(
                request.source,
                request.locator,
                "Included Header could not be resolved",
                succeeded(resolved)
                    ? server_status::
                        project_configuration_invalid
                    : resolved);
        }
    }
    catch (...) {
        output = {};

        return fail(
            request.source,
            request.locator,
            "Included Header path resolution failed",
            server_status::io_error);
    }

    const auto staged =
        files.add_dependency(
            request.source,
            output);

    if (!succeeded(staged)) {
        return fail(
            request.source,
            request.locator,
            "Included Header dependency could not be staged",
            staged);
    }

    return materialize_and_lex(
        output);
}

server_status semantic_input::consume_directive(
    std::uint32_t word_offset,
    std::uint32_t source_base) noexcept {

    const auto file =
        input.current_file();

    const auto decoder_started =
        directive_input.start_at(
            file,
            word_offset,
            source_base);

    if (!succeeded(decoder_started)) {
        return fail(
            file,
            {},
            "Preprocessing directive replay could not start",
            decoder_started);
    }

    preprocessing_directive directive;

    const auto decoded =
        directive_decoder::decode(
            directive_input,
            directive);

    if (!succeeded(decoded)) {
        return fail(
            file,
            {},
            "Preprocessing directive replay could not be decoded",
            decoded);
    }

    frontend_token consumed;

    do {
        const auto advanced =
            input.next(consumed);

        if (!succeeded(advanced)) {
            return fail(
                file,
                directive.range.source,
                "Preprocessing directive replay could not advance lexical input",
                advanced);
        }
    }
    while (consumed.kind !=
        token_kind::pp_end);

    directive_execution_result result;
    directive_execution_error error;

    const auto source =
        files.content(
            directive.range.file);

    const auto executed =
        executor.execute(
            directive,
            source,
            result,
            &error);

    if (!succeeded(executed)) {
        return fail(
            error.file,
            error.source,
            "Preprocessing directive execution failed during semantic replay",
            executed);
    }

    if (result.kind !=
        directive_execution_kind::include) {

        return server_status::success;
    }

    file_id target;

    const auto resolved =
        resolve_include(
            result.include,
            target);

    if (!succeeded(resolved)) {
        return resolved;
    }

    const auto entered =
        input.enter(target);

    if (!succeeded(entered)) {
        return fail(
            result.include.source,
            result.include.locator,
            "Included Header nesting exceeds the supported frontend depth",
            entered);
    }

    const auto execution_entered =
        executor.enter_file(target);

    if (!succeeded(execution_entered)) {
        return fail(
            result.include.source,
            result.include.locator,
            "Preprocessing execution could not enter included Header",
            execution_entered);
    }

    return server_status::success;
}

server_status semantic_input::physical_identifier(
    const frontend_token& token,
    string_id& output) noexcept {

    output = {};

    std::string_view spelling;

    if (!source_view(
            files,
            token.file,
            token.source_offset,
            token.source_length,
            spelling) ||
        spelling.empty()) {

        return fail(
            token.file,
            {
                token.source_offset,
                token.source_length,
            },
            "Identifier source spelling is unavailable",
            server_status::project_configuration_invalid);
    }

    return strings.intern(
        spelling,
        output);
}

server_status semantic_input::effective_identifier(
    const frontend_token& token,
    string_id& output,
    bool& empty) noexcept {

    output = {};
    empty = false;

    string_id physical;

    const auto interned =
        physical_identifier(
            token,
            physical);

    if (!succeeded(interned)) {
        return interned;
    }

    preprocessor_expansion expansion;

    const auto expanded =
        preprocessing.expand(
            physical,
            expansion);

    if (!succeeded(expanded)) {
        return expanded;
    }

    if (expansion.kind ==
        preprocessor_expansion_kind::empty) {

        empty = true;
        return server_status::success;
    }

    output =
        expansion.identifier;

    return output
        ? server_status::success
        : server_status::project_configuration_invalid;
}

server_status semantic_input::next(
    semantic_token& output) noexcept {

    output = {};

    if (!started ||
        finished_value) {

        return server_status::
            project_configuration_invalid;
    }

    for (;;) {
        if (input.finished()) {
            const auto file =
                input.current_file();

            if (mode_value ==
                semantic_input_mode::header) {

                directive_execution_error error;

                const auto finished =
                    executor.finish_file(
                        file,
                        &error);

                if (!succeeded(finished)) {
                    return fail(
                        error.file,
                        error.source,
                        "Conditional preprocessing group is not closed",
                        finished);
                }
            }

            const auto left =
                input.leave();

            if (!succeeded(left)) {
                return fail(
                    file,
                    {},
                    "Semantic lexical input could not leave completed file",
                    left);
            }

            if (input.empty()) {
                finished_value = true;
                return server_status::success;
            }

            continue;
        }

        const auto word_offset =
            input.word_offset();

        const auto source_base =
            input.source_offset();

        frontend_token physical;

        const auto advanced =
            input.next(
                physical);

        if (!succeeded(advanced)) {
            return fail(
                input.current_file(),
                {},
                "Semantic lexical input could not decode next token",
                advanced);
        }

        if (directive_start(
                physical.kind)) {

            if (mode_value ==
                semantic_input_mode::source) {

                return fail(
                    physical.file,
                    {
                        physical.source_offset,
                        physical.source_length,
                    },
                    "C++ preprocessing directives are not supported in Source inputs",
                    server_status::project_configuration_invalid);
            }

            const auto consumed =
                consume_directive(
                    word_offset,
                    source_base);

            if (!succeeded(consumed)) {
                return consumed;
            }

            continue;
        }

        if (mode_value ==
                semantic_input_mode::header &&
            !executor.active()) {

            continue;
        }

        output.file =
            physical.file;

        output.kind =
            physical.kind;

        output.source_offset =
            physical.source_offset;

        output.source_length =
            physical.source_length;

        if (physical.kind ==
            token_kind::identifier) {

            if (mode_value ==
                semantic_input_mode::source) {

                const auto interned =
                    physical_identifier(
                        physical,
                        output.identifier);

                if (!succeeded(interned)) {
                    return interned;
                }
            }
            else {
                bool empty = false;

                const auto effective =
                    effective_identifier(
                        physical,
                        output.identifier,
                        empty);

                if (!succeeded(effective)) {
                    return effective;
                }

                if (empty) {
                    output = {};
                    continue;
                }
            }
        }

        return server_status::success;
    }
}


}
