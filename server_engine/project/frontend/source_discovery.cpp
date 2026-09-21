#include "source_discovery.hpp"

#include "directive_decoder.hpp"
#include "../construction/execution_lanes.hpp"
#include "frontend_input.hpp"
#include "lexer.hpp"
#include "../preprocessor/preprocessor.hpp"
#include "../../filesystem_path.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

namespace cw::server {
namespace {

[[nodiscard]] constexpr bool lexical_kind(
    file_kind kind) noexcept {

    return kind == file_kind::header ||
        kind == file_kind::source;
}

[[nodiscard]] server_status acquisition_status(
    file_acquire_result_kind kind) noexcept {

    switch (kind) {
    case file_acquire_result_kind::present:
        return server_status::success;

    case file_acquire_result_kind::missing:
        return server_status::project_configuration_invalid;

    case file_acquire_result_kind::unchanged:
        return server_status::project_artifact_invalid;

    case file_acquire_result_kind::changed_during_read:
    case file_acquire_result_kind::failed:
    case file_acquire_result_kind::allocation_failed:
        return server_status::io_error;
    }

    return server_status::io_error;
}

struct file_pool final {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct lexical_lane_state final {
    lexical_stream stream;
    server_status status =
        server_status::success;
    source_discovery_failure failure;
};

struct directive_frame final {
    file_id file{};
    std::uint32_t next_directive = 0;
};

class source_closure_builder final {
public:
    source_closure_builder(
        file_context& files,
        lexical_generation& lexical,
        const preprocessor_configuration& configuration,
        string_table& strings,
        source_discovery_failure* failure) noexcept
        : files(files),
          lexical(lexical),
          configuration(configuration),
          strings(strings),
          state(strings),
          executor(strings, state),
          failure(failure) {
    }

    [[nodiscard]] server_status run() noexcept {

        if (failure != nullptr) {
            *failure = {};
        }

        root_count =
            files.size();

        std::size_t lexical_file_count = 0;
        std::uint64_t lexical_weight = 0;

        const auto materialized =
            materialize_range(
                0,
                root_count,
                lexical_file_count,
                lexical_weight);

        if (!succeeded(materialized)) {
            return materialized;
        }

        if (lexical_file_count == 0) {
            return lexical.reset(
                root_count,
                1);
        }

        lane_capacity =
            (std::min)(
                lexical_file_count,
                execution_lane_capacity());

        if (lane_capacity == 0 ||
            lane_capacity >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {

            return server_status::io_error;
        }

        pools.reset(
            new (std::nothrow)
                file_pool[lane_capacity]);

        lanes.reset(
            new (std::nothrow)
                lexical_lane_state[lane_capacity]);

        if (!pools ||
            !lanes) {

            return server_status::io_error;
        }

        const auto reset =
            lexical.reset(
                root_count,
                lane_capacity);

        if (!succeeded(reset)) {
            return reset;
        }

        const auto started =
            workers.start(
                lane_capacity);

        if (!succeeded(started)) {
            return started;
        }

        const auto initial_lex =
            lex_range(
                0,
                root_count,
                lexical_file_count,
                lexical_weight);

        if (!succeeded(initial_lex)) {
            return initial_lex;
        }

        // Identity-producing directive execution is deliberately single-owner
        // and ordered by the initial dense file_id table. CPU count affects only
        // physical lex throughput, never file_id/string_id assignment order.
        for (std::size_t index = 0;
             index < root_count;
             ++index) {

            const file_id root{
                static_cast<std::uint32_t>(
                    index + 1)};

            if (!lexical_kind(
                    files.kind(root))) {

                continue;
            }

            const auto executed =
                execute_root(root);

            if (!succeeded(executed)) {
                return executed;
            }
        }

        return server_status::success;
    }

private:
    static void lexical_lane_entry(
        void* context,
        std::size_t lane) noexcept {

        static_cast<source_closure_builder*>(
            context)
            ->run_lexical_lane(
                lane);
    }

    void set_failure(
        source_discovery_failure_kind kind,
        file_id file,
        source_range source) noexcept {

        if (failure == nullptr) {
            return;
        }

        failure->kind =
            kind;
        failure->file =
            file;
        failure->source =
            source;
    }

    [[nodiscard]] server_status materialize_file(
        file_id file) noexcept {

        if (!files.contains(file)) {
            return server_status::project_configuration_invalid;
        }

        if (files.content_available(file)) {
            return server_status::success;
        }

        file_acquire_job job;

        const auto prepared =
            files.prepare_acquire(
                file,
                job);

        if (!succeeded(prepared)) {
            return prepared;
        }

        file_acquire_result result;

        file_context::execute_acquire(
            job,
            result);

        const auto acquired =
            acquisition_status(
                result.kind);

        if (!succeeded(acquired)) {
            return acquired;
        }

        bool content_changed = false;

        const auto applied =
            files.apply_acquire(
                result,
                content_changed);

        if (!succeeded(applied)) {
            return applied;
        }

        return files.content_available(file)
            ? server_status::success
            : server_status::project_artifact_invalid;
    }

    [[nodiscard]] server_status materialize_range(
        std::size_t begin,
        std::size_t end,
        std::size_t& lexical_file_count,
        std::uint64_t& lexical_weight) noexcept {

        lexical_file_count = 0;
        lexical_weight = 0;

        if (begin > end ||
            end > files.size()) {

            return server_status::project_configuration_invalid;
        }

        for (auto index = begin;
             index < end;
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            if (!lexical_kind(
                    files.kind(file))) {

                continue;
            }

            const auto materialized =
                materialize_file(file);

            if (!succeeded(materialized)) {
                return materialized;
            }

            const auto size =
                files.content(file)
                    .size();

            const auto weight =
                static_cast<std::uint64_t>(
                    size == 0
                        ? 1
                        : size);

            if (lexical_weight >
                (std::numeric_limits<std::uint64_t>::max)() -
                    weight) {

                return server_status::io_error;
            }

            lexical_weight +=
                weight;

            ++lexical_file_count;
        }

        return server_status::success;
    }

    [[nodiscard]] server_status build_pools(
        std::size_t begin,
        std::size_t end,
        std::size_t lexical_file_count,
        std::uint64_t lexical_weight,
        std::size_t active_lanes) noexcept {

        if (begin > end ||
            end > files.size() ||
            active_lanes == 0 ||
            active_lanes > lane_capacity ||
            lexical_file_count < active_lanes ||
            lexical_weight <
                static_cast<std::uint64_t>(
                    lexical_file_count)) {

            return server_status::project_configuration_invalid;
        }

        std::size_t lane = 0;
        std::size_t pool_begin = begin;
        std::size_t remaining_files =
            lexical_file_count;

        std::uint64_t assigned_weight = 0;
        std::uint64_t pool_weight = 0;

        for (auto index = begin;
             index < end;
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            if (!lexical_kind(
                    files.kind(file))) {

                continue;
            }

            const auto size =
                files.content(file)
                    .size();

            const auto weight =
                static_cast<std::uint64_t>(
                    size == 0
                        ? 1
                        : size);

            pool_weight +=
                weight;

            --remaining_files;

            if (lane + 1 >=
                active_lanes) {

                continue;
            }

            const auto remaining_lanes =
                active_lanes - lane;

            const auto remaining_weight =
                lexical_weight -
                assigned_weight;

            const auto target =
                remaining_weight /
                    remaining_lanes +
                static_cast<std::uint64_t>(
                    remaining_weight %
                        remaining_lanes !=
                    0);

            const auto future_lanes =
                active_lanes -
                lane -
                1;

            if (pool_weight < target &&
                remaining_files >
                    future_lanes) {

                continue;
            }

            pools[lane] = {
                pool_begin,
                index + 1,
            };

            ++lane;

            pool_begin =
                index + 1;

            assigned_weight +=
                pool_weight;

            pool_weight = 0;
        }

        if (lane + 1 !=
            active_lanes) {

            return server_status::project_configuration_invalid;
        }

        pools[lane] = {
            pool_begin,
            end,
        };

        return server_status::success;
    }

    [[nodiscard]] server_status lex_file(
        file_id file,
        std::uint32_t arena,
        lexical_stream& stream,
        source_discovery_failure* local_failure) noexcept {

        if (lexical.contains(file)) {
            return server_status::success;
        }

        if (!files.contains(file) ||
            !lexical_kind(
                files.kind(file))) {

            return server_status::project_configuration_invalid;
        }

        if (!files.content_available(file)) {
            return server_status::project_artifact_invalid;
        }

        lexical_error error;

        const auto tokenized =
            lexer::tokenize(
                file,
                files.content(file),
                stream,
                &error);

        if (!succeeded(tokenized)) {
            if (local_failure != nullptr) {
                local_failure->kind =
                    source_discovery_failure_kind::lexical;

                local_failure->file =
                    file;

                local_failure->lexical =
                    error;
            }

            return tokenized;
        }

        return lexical.publish(
            file,
            arena,
            stream);
    }

    void run_lexical_lane(
        std::size_t lane) noexcept {

        auto& lane_state =
            lanes[lane];

        lane_state.status =
            server_status::success;

        lane_state.failure = {};

        const auto pool =
            pools[lane];

        const auto arena =
            static_cast<std::uint32_t>(
                lane);

        for (auto index = pool.begin;
             index < pool.end;
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            if (!lexical_kind(
                    files.kind(file))) {

                continue;
            }

            lane_state.status =
                lex_file(
                    file,
                    arena,
                    lane_state.stream,
                    &lane_state.failure);

            if (!succeeded(
                    lane_state.status)) {

                return;
            }
        }
    }

    [[nodiscard]] server_status lex_range(
        std::size_t begin,
        std::size_t end,
        std::size_t lexical_file_count,
        std::uint64_t lexical_weight) noexcept {

        if (lexical_file_count == 0) {
            return server_status::success;
        }

        const auto active_lanes =
            (std::min)(
                lexical_file_count,
                lane_capacity);

        const auto pooled =
            build_pools(
                begin,
                end,
                lexical_file_count,
                lexical_weight,
                active_lanes);

        if (!succeeded(pooled)) {
            return pooled;
        }

        const auto parallel =
            workers.run(
                active_lanes,
                lexical_lane_entry,
                this);

        if (!succeeded(parallel)) {
            return parallel;
        }

        for (std::size_t lane = 0;
             lane < active_lanes;
             ++lane) {

            if (!succeeded(
                    lanes[lane].status)) {

                if (failure != nullptr) {
                    *failure =
                        lanes[lane].failure;
                }

                return lanes[lane].status;
            }
        }

        return server_status::success;
    }

    [[nodiscard]] server_status lex_discovered(
        file_id file) noexcept {

        const auto materialized =
            materialize_file(
                file);

        if (!succeeded(materialized)) {
            return materialized;
        }

        const auto extended =
            lexical.extend(
                files.size());

        if (!succeeded(extended)) {
            return extended;
        }

        const auto index =
            static_cast<std::size_t>(
                file.value() - 1);

        const auto size =
            files.content(file)
                .size();

        const auto weight =
            static_cast<std::uint64_t>(
                size == 0
                    ? 1
                    : size);

        return lex_range(
            index,
            index + 1,
            1,
            weight);
    }

    [[nodiscard]] server_status resolve_include(
        const include_request& request,
        std::string_view source,
        file_id& output) noexcept {

        output = {};

        if (request.form ==
            include_form::angled) {

            set_failure(
                source_discovery_failure_kind::
                    unsupported_include_form,
                request.source,
                request.locator);

            return server_status::unsupported;
        }

        if (request.form !=
                include_form::quoted ||
            request.locator.length < 2) {

            set_failure(
                source_discovery_failure_kind::
                    invalid_include,
                request.source,
                request.locator);

            return server_status::project_configuration_invalid;
        }

        const auto offset =
            static_cast<std::size_t>(
                request.locator.offset);

        const auto length =
            static_cast<std::size_t>(
                request.locator.length);

        if (offset > source.size() ||
            length >
                source.size() - offset) {

            set_failure(
                source_discovery_failure_kind::
                    invalid_include,
                request.source,
                request.locator);

            return server_status::project_configuration_invalid;
        }

        const auto spelling =
            source.substr(
                offset,
                length);

        if (spelling.size() < 2 ||
            spelling.front() != '"' ||
            spelling.back() != '"') {

            set_failure(
                source_discovery_failure_kind::
                    invalid_include,
                request.source,
                request.locator);

            return server_status::project_configuration_invalid;
        }

        const auto locator_text =
            spelling.substr(
                1,
                spelling.size() - 2);

        if (locator_text.empty()) {
            set_failure(
                source_discovery_failure_kind::
                    invalid_include,
                request.source,
                request.locator);

            return server_status::project_configuration_invalid;
        }

        std::filesystem::path locator;

        if (filesystem_path_from_utf8(
                locator_text,
                locator) !=
            filesystem_path_result::success) {

            set_failure(
                source_discovery_failure_kind::
                    invalid_include,
                request.source,
                request.locator);

            return server_status::project_configuration_invalid;
        }

        try {
            const auto path_view =
                files.path(
                    request.source);

            const std::filesystem::path source_path{
                path_view.begin(),
                path_view.end()};

            const auto candidate =
                source_path.parent_path() /
                locator;

            const auto resolved =
                files.resolve(
                    candidate,
                    file_kind::header,
                    output);

            if (!succeeded(resolved)) {
                set_failure(
                    source_discovery_failure_kind::
                        include_resolution,
                    request.source,
                    request.locator);

                return resolved;
            }
        }
        catch (...) {
            set_failure(
                source_discovery_failure_kind::
                    include_resolution,
                request.source,
                request.locator);

            return server_status::io_error;
        }

        const auto staged =
            files.add_dependency(
                request.source,
                output);

        if (!succeeded(staged)) {
            set_failure(
                source_discovery_failure_kind::
                    include_resolution,
                request.source,
                request.locator);

            return staged;
        }

        return server_status::success;
    }

    [[nodiscard]] server_status execute_root(
        file_id root) noexcept {

        state.reset();
        executor.reset();

        const auto initialized =
            initialize_preprocessor(
                configuration,
                strings,
                state);

        if (!succeeded(initialized)) {
            return initialized;
        }

        frontend_input decoder_input{
            lexical};

        std::array<
            directive_frame,
            frontend_include_depth_limit>
            stack{};

        std::size_t depth = 1;

        stack[0] = {
            root,
            0,
        };

        while (depth != 0) {
            auto& frame =
                stack[
                    depth - 1];

            const auto anchors =
                lexical.directives(
                    frame.file);

            if (static_cast<std::size_t>(
                    frame.next_directive) >=
                anchors.size()) {

                directive_execution_error error;

                const auto finished =
                    executor.finish_file(
                        frame.file,
                        &error);

                if (!succeeded(finished)) {
                    if (failure != nullptr) {
                        failure->kind =
                            source_discovery_failure_kind::directive;
                        failure->file =
                            error.file;
                        failure->source =
                            error.source;
                        failure->directive =
                            error.kind;
                    }

                    return finished;
                }

                --depth;
                continue;
            }

            const auto anchor =
                anchors[
                    frame.next_directive++];

            const auto started =
                decoder_input.start_at(
                    frame.file,
                    anchor.word_offset,
                    anchor.source_base);

            if (!succeeded(started)) {
                return started;
            }

            preprocessing_directive directive;

            const auto decoded =
                directive_decoder::decode(
                    decoder_input,
                    directive);

            if (!succeeded(decoded)) {
                if (failure != nullptr) {
                    failure->kind =
                        source_discovery_failure_kind::directive;
                    failure->file =
                        frame.file;
                    failure->source = {};
                    failure->directive =
                        directive_execution_error_kind::
                            malformed_operand;
                }

                return decoded;
            }

            const auto source =
                files.content(
                    directive.range.file);

            directive_execution_result result;
            directive_execution_error error;

            const auto executed =
                executor.execute(
                    directive,
                    source,
                    result,
                    &error);

            if (!succeeded(executed)) {
                if (failure != nullptr) {
                    failure->kind =
                        source_discovery_failure_kind::directive;
                    failure->file =
                        error.file;
                    failure->source =
                        error.source;
                    failure->directive =
                        error.kind;
                }

                return executed;
            }

            if (result.kind !=
                directive_execution_kind::include) {

                continue;
            }

            if (depth ==
                stack.size()) {

                set_failure(
                    source_discovery_failure_kind::
                        include_depth_exceeded,
                    result.include.source,
                    result.include.locator);

                return server_status::project_configuration_invalid;
            }

            file_id target;

            const auto resolved =
                resolve_include(
                    result.include,
                    source,
                    target);

            if (!succeeded(resolved)) {
                return resolved;
            }

            if (!lexical.contains(target)) {
                const auto lexed =
                    lex_discovered(
                        target);

                if (!succeeded(lexed)) {
                    if (failure != nullptr &&
                        failure->kind ==
                            source_discovery_failure_kind::none) {

                        set_failure(
                            source_discovery_failure_kind::
                                include_resolution,
                            result.include.source,
                            result.include.locator);
                    }

                    return lexed;
                }
            }

            stack[depth++] = {
                target,
                0,
            };
        }

        return server_status::success;
    }

    file_context& files;
    lexical_generation& lexical;
    const preprocessor_configuration& configuration;
    string_table& strings;

    preprocessor state;
    directive_executor executor;

    source_discovery_failure* failure = nullptr;

    std::size_t root_count = 0;
    std::size_t lane_capacity = 0;

    std::unique_ptr<file_pool[]> pools;
    std::unique_ptr<lexical_lane_state[]> lanes;

    execution_lanes workers;
};

}

server_status discover_source_closure(
    file_context& files,
    lexical_generation& lexical,
    const preprocessor_configuration& configuration,
    string_table& strings,
    source_discovery_failure* failure) noexcept {

    source_closure_builder builder{
        files,
        lexical,
        configuration,
        strings,
        failure};

    return builder.run();
}

}
