#include "semantic_input.hpp"

#include "../../filesystem_path.hpp"

#include <filesystem>
#include <chrono>
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
    string_table& strings_value,
    semantic_input_telemetry* telemetry_value) noexcept
    : files(files_value),
      lexical(lexical_value),
      configuration(configuration_value),
      strings(strings_value),
      input(lexical),
      directive_input(lexical),
      preprocessing(strings),
      executor(strings, preprocessing),
      telemetry(telemetry_value) {
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
    source_cache_file = {};
    source_cache = {};
    header_passthrough = false;
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

    if (files.baseline_bound()) {
        const auto replacement =
            files.begin_dependency_replacement(
                root);

        if (!succeeded(replacement)) {
            return fail(
                root,
                {},
                "Semantic root dependency replacement could not begin",
                replacement);
        }
    }

    preprocessing.reset();
    executor.reset();
    once_files.clear();

    if (mode == semantic_input_mode::source) {
        source_root = root;
        source_words = lexical.words(root);
        source_native_words = source_words.native_words();
        source_word_offset = 0;
        source_offset = 0;
        started = true;
        return server_status::success;
    }

    const auto opened =
        input.start(root);

    if (!succeeded(opened)) {
        return fail(
            root,
            {},
            "Semantic lexical input could not start",
            opened);
    }

    prepare_includes(root);

    if (configuration.predefines.empty() &&
        lexical.directives(root).empty()) {

        // A directive-free Header with no configured predefines has no
        // preprocessing state to execute. Keep it on the direct token path.
        header_passthrough = true;
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

    // Materialization may grow the source byte arena. Reacquire cached source
    // storage before slicing another identifier.
    source_cache_file = {};
    source_cache = {};

    if (!files.contains(file) ||
        files.kind(file) != file_kind::header) {

        return fail(
            file,
            {},
            "Included Header identity is invalid",
            server_status::project_configuration_invalid);
    }

    if (!files.content_available(file)) {
        try {
            const auto path_view = files.path(file);
            const std::filesystem::path path{path_view.begin(), path_view.end()};
            const auto cached_physical = prepared_includes.find(path);
            if (cached_physical != prepared_includes.end()) {
                auto physical = std::move(cached_physical->second);
                prepared_includes.erase(cached_physical);
                physical.acquired.file = file;
                bool changed = false;
                auto status = files.apply_acquire(physical.acquired, changed);
                if (!succeeded(status)) return status;
                status = lexical.extend(files.size());
                if (!succeeded(status)) return status;
                physical.stream.bind_prepared_file(file);
                status = lexical.publish(file, 0, physical.stream);
                if (!succeeded(status)) return status;
                if (telemetry != nullptr) ++telemetry->prepared_include_count;
                prepare_includes(file);
                return server_status::success;
            }
        }
        catch (...) { return server_status::io_error; }
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
        prepare_includes(file);
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

    prepare_includes(file);
    return server_status::success;
}

void semantic_input::prepare_include_lane(void* context, std::size_t lane) noexcept {
    auto& owner = *static_cast<semantic_input*>(context);
    for (auto i = lane; i < owner.include_frontier.size(); i += owner.include_lane_count) {
        auto& item = owner.include_frontier[i];
        file_acquire_job job;
        job.file = file_id{1}; // Temporary identity, never published by workers.
        job.path = item.path.native();
        file_context::execute_acquire(job, item.acquired);
        if (item.acquired.kind != file_acquire_result_kind::present) continue;
        item.ready = succeeded(lexer::tokenize(job.file, item.acquired.snapshot.bytes, item.stream));
    }
}

void semantic_input::prepare_includes(file_id file) noexcept {
    // BUILD retains its exact replacement/observation protocol. Speculation is
    // limited to full construction and never executes preprocessing directives.
    if (files.baseline_bound()) return;
    try {
        if (!preparation_scanned.insert(file.value()).second) return;
        const auto anchors = lexical.directives(file);
        if (anchors.size() < 8) return;
        const auto source = files.content(file);
        const auto path_view = files.path(file);
        const std::filesystem::path parent =
            std::filesystem::path{path_view.begin(), path_view.end()}.parent_path();
        include_frontier.clear();
        for (const auto anchor : anchors) {
            if (include_frontier.size() + prepared_includes.size() >= 4096) break;
            if (!succeeded(directive_input.start_at(file, anchor.word_offset, anchor.source_base))) continue;
            preprocessing_directive directive;
            if (!succeeded(directive_decoder::decode(directive_input, directive)) ||
                directive.kind != directive_kind::include) continue;
            const auto range = directive.include.locator;
            if (range.length < 3 || range.offset > source.size() ||
                range.length > source.size() - range.offset) continue;
            std::filesystem::path locator;
            if (filesystem_path_from_utf8(source.substr(range.offset + 1, range.length - 2), locator)
                != filesystem_path_result::success) continue;
            auto candidate = parent / locator;
            std::error_code error;
            bool found = locator.is_absolute() || directive.include.form == include_form::quoted;
            found = found && std::filesystem::is_regular_file(candidate, error);
            if (error && error != std::errc::no_such_file_or_directory) continue;
            if (!found && !locator.is_absolute()) {
                for (const auto& configured : configuration.include_directories) {
                    std::filesystem::path directory;
                    if (filesystem_path_from_utf8(configured, directory) != filesystem_path_result::success) continue;
                    const auto searched = configuration.root_directory / directory / locator;
                    error.clear();
                    if (std::filesystem::is_regular_file(searched, error)) {
                        candidate = searched;
                        found = true;
                        break;
                    }
                    if (error && error != std::errc::no_such_file_or_directory) break;
                }
            }
            if (!found) {
                error.clear();
                found = std::filesystem::is_regular_file(candidate, error);
            }
            if (!found || error) continue;
            candidate = candidate.lexically_normal();
            if (!preparation_attempted.insert(candidate).second) continue;
            prepared_include item;
            item.path = std::move(candidate);
            include_frontier.push_back(std::move(item));
        }
        if (include_frontier.size() < 8) { include_frontier.clear(); return; }
        if (include_workers.size() == 0 &&
            !succeeded(include_workers.start(execution_lane_capacity()))) {
            include_frontier.clear(); return;
        }
        include_lane_count = (std::min)(include_frontier.size(), include_workers.size());
        if (!succeeded(include_workers.run(include_lane_count, prepare_include_lane, this))) {
            include_frontier.clear(); return;
        }
        for (auto& item : include_frontier) {
            if (item.ready) {
                auto key = item.path;
                prepared_includes.emplace(std::move(key), std::move(item));
            }
        }
        include_frontier.clear();
    }
    catch (...) {
        // Speculation cannot fail an inactive include. Active replay remains
        // authoritative and falls back to ordinary acquisition/tokenization.
        include_frontier.clear();
    }
}

server_status semantic_input::resolve_include(
    const include_request& request,
    file_id& output) noexcept {

    output = {};

    if ((request.form != include_form::quoted &&
         request.form != include_form::angled) ||
        request.locator.length < 2) {

        return fail(
            request.source,
            request.locator,
            "Only direct quoted or angled includes are supported by semantic preprocessing",
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
        spelling.front() != (request.form == include_form::quoted ? '"' : '<') ||
        spelling.back() != (request.form == include_form::quoted ? '"' : '>')) {

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

        auto candidate =
            source_path.parent_path() /
            locator;

        auto& resolutions = request.form == include_form::quoted
            ? quoted_includes : angled_includes;
        const auto resolution_key = candidate;
        const auto cached = resolutions.find(resolution_key);
        if (cached != resolutions.end()) {
            output = cached->second;
        }
        else {

            if (!locator.is_absolute() && !configuration.include_directories.empty()) {
                // Quoted includes prefer the including file. Angled includes prefer
                // the root Project's ordered search directories, then local fallback.
                std::error_code error;
                bool found = request.form == include_form::quoted &&
                    std::filesystem::is_regular_file(candidate, error);
                if (error && error != std::errc::no_such_file_or_directory) {
                    return fail(request.source, request.locator,
                        "Cannot inspect local include path", server_status::io_error);
                }
                for (const auto& configured : configuration.include_directories) {
                    if (found) {
                        break;
                    }
                    std::filesystem::path directory;
                    if (filesystem_path_from_utf8(configured, directory) != filesystem_path_result::success) {
                        return fail(request.source, request.locator,
                            "Invalid configured include directory", server_status::project_configuration_invalid);
                    }
                    const auto searched = configuration.root_directory / directory / locator;
                    error.clear();
                    if (std::filesystem::is_regular_file(searched, error)) {
                        candidate = searched;
                        found = true;
                    }
                    else if (error && error != std::errc::no_such_file_or_directory) {
                        return fail(request.source, request.locator,
                            "Cannot inspect configured include path", server_status::io_error);
                    }
                }
            }

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
            resolutions.emplace(resolution_key, output);
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

    if (files.baseline_bound()) {
        const auto replacement =
            files.begin_dependency_replacement(
                output);

        if (!succeeded(replacement)) {
            output = {};

            return fail(
                request.source,
                request.locator,
                "Included Header dependency replacement could not begin",
                replacement);
        }
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

    const auto lexical_started = telemetry != nullptr
        ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto materialized = materialize_and_lex(output);
    if (telemetry != nullptr) {
        telemetry->include_lexical_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - lexical_started).count());
    }
    return materialized;
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

    if (result.kind == directive_execution_kind::pragma_once) {
        try {
            once_files.insert(file.value());
        }
        catch (...) {
            return fail(file, directive.range.source,
                "Pragma once state could not be recorded", server_status::io_error);
        }
        return server_status::success;
    }

    if (result.kind !=
        directive_execution_kind::include) {

        return server_status::success;
    }

    file_id target;

    const auto include_started = telemetry != nullptr
        ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto resolved =
        resolve_include(
            result.include,
            target);

    if (telemetry != nullptr) {
        telemetry->include_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - include_started).count());
        ++telemetry->include_count;
    }

    if (!succeeded(resolved)) {
        return resolved;
    }

    // Resolve and stage the dependency even when replay of the body is skipped.
    if (once_files.contains(target.value())) {
        return server_status::success;
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

    if (source_cache_file != token.file) {
        if (!files.content_available(
                token.file)) {

            return fail(
                token.file,
                {
                    token.source_offset,
                    token.source_length,
                },
                "Identifier source spelling is unavailable",
                server_status::project_configuration_invalid);
        }

        source_cache =
            files.content(
                token.file);

        source_cache_file =
            token.file;
    }

    const auto begin =
        static_cast<std::size_t>(
            token.source_offset);

    const auto count =
        static_cast<std::size_t>(
            token.source_length);

    if (begin > source_cache.size() ||
        count >
            source_cache.size() -
                begin) {

        return fail(
            token.file,
            {
                token.source_offset,
                token.source_length,
            },
            "Identifier source spelling is unavailable",
            server_status::project_configuration_invalid);
    }

    const auto spelling =
        source_cache.substr(
            begin,
            count);

    if (spelling.empty()) {
        return fail(
            token.file,
            {
                token.source_offset,
                token.source_length,
            },
            "Identifier source spelling is unavailable",
            server_status::project_configuration_invalid);
    }

    using clock_type = std::chrono::steady_clock;
    const bool measure = telemetry != nullptr && telemetry->detailed_source && mode_value == semantic_input_mode::source;
    const auto started_at = measure ? clock_type::now() : clock_type::time_point{};
    const auto status = strings.intern(spelling, output);
    if (measure) telemetry->source_intern_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now() - started_at).count());
    return status;
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

server_status semantic_input::next_source(semantic_token& output) noexcept {
    if (source_word_offset == source_words.size()) {
        finished_value = true;
        return server_status::success;
    }
    using clock_type = std::chrono::steady_clock;
    const bool measure = telemetry != nullptr && telemetry->detailed_source;
    const auto started_at = measure ? clock_type::now() : clock_type::time_point{};
    const auto advanced = !source_native_words.empty()
        ? decode_frontend_token(source_native_words, source_root, source_word_offset, source_offset, output)
        : decode_frontend_token(source_words, source_root, source_word_offset, source_offset, output);
    if (telemetry != nullptr) {
        if (measure) telemetry->source_decode_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now() - started_at).count());
        if (succeeded(advanced)) ++telemetry->source_token_count;
    }
    if (!succeeded(advanced)) return fail(source_root, {},
        "Semantic lexical input could not decode next token", advanced);
    if (directive_start(output.kind)) return fail(output.file,
        {output.source_offset, output.source_length},
        "C++ preprocessing directives are not supported in Source inputs",
        server_status::project_configuration_invalid);
    if (output.kind == token_kind::identifier) {
        if (telemetry != nullptr) ++telemetry->source_identifier_count;
        return physical_identifier({output.file, output.kind, output.source_offset, output.source_length},
            output.identifier);
    }
    return server_status::success;
}

server_status semantic_input::next_passthrough_header(
    semantic_token& output) noexcept {

    if (input.finished()) {
        const auto file = input.current_file();
        const auto left = input.leave();

        if (!succeeded(left) || !input.empty()) {
            return fail(
                file,
                {},
                "Semantic lexical input could not leave completed Header",
                succeeded(left)
                    ? server_status::project_configuration_invalid
                    : left);
        }

        finished_value = true;
        return server_status::success;
    }

    frontend_token physical;
    const auto advanced = input.next(physical);

    if (!succeeded(advanced)) {
        return fail(
            input.current_file(),
            {},
            "Semantic lexical input could not decode next token",
            advanced);
    }

    output.file = physical.file;
    output.kind = physical.kind;
    output.source_offset = physical.source_offset;
    output.source_length = physical.source_length;

    if (physical.kind == token_kind::identifier) {
        return physical_identifier(
            physical,
            output.identifier);
    }

    return server_status::success;
}

server_status semantic_input::next_preprocessed_header(
    semantic_token& output) noexcept {

    for (;;) {
        if (input.finished()) {
            const auto file = input.current_file();
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

            const auto left = input.leave();

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

        const auto word_offset = input.word_offset();
        const auto source_base = input.source_offset();

        frontend_token physical;
        const auto advanced = input.next(physical);

        if (!succeeded(advanced)) {
            return fail(
                input.current_file(),
                {},
                "Semantic lexical input could not decode next token",
                advanced);
        }

        if (directive_start(physical.kind)) {
            const auto consumed =
                consume_directive(
                    word_offset,
                    source_base);

            if (!succeeded(consumed)) {
                return consumed;
            }

            continue;
        }

        if (!executor.active()) {
            continue;
        }

        output.file = physical.file;
        output.kind = physical.kind;
        output.source_offset = physical.source_offset;
        output.source_length = physical.source_length;

        if (physical.kind == token_kind::identifier) {
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

        return server_status::success;
    }
}

server_status semantic_input::next(
    semantic_token& output) noexcept {

    output = {};

    if (!started || finished_value) {
        return server_status::
            project_configuration_invalid;
    }

    if (mode_value == semantic_input_mode::source) {
        return next_source(output);
    }

    return header_passthrough
        ? next_passthrough_header(output)
        : next_preprocessed_header(output);
}


}
