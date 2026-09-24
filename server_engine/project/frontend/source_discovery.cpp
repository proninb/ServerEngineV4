#include "source_discovery.hpp"

#include "../construction/execution_lanes.hpp"
#include "lexer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

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

[[nodiscard]] server_status lex_source_file(
    file_context& files,
    lexical_generation& lexical,
    file_id file,
    std::uint32_t arena,
    lexical_stream& stream,
    source_preparation_failure* failure) noexcept {

    if (lexical.contains(file)) {
        return server_status::success;
    }

    if (!files.contains(file) ||
        !lexical_kind(files.kind(file)) ||
        !files.content_available(file)) {
        return server_status::project_configuration_invalid;
    }

    lexical_error error;
    const auto tokenized = lexer::tokenize(file, files.content(file), stream, &error);

    if (!succeeded(tokenized)) {
        if (failure != nullptr) {
            failure->kind = source_preparation_failure_kind::lexical;
            failure->file = file;
            failure->lexical = error;
        }
        return tokenized;
    }

    return lexical.publish(file, arena, stream);
}

struct file_pool final {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct lexical_lane_state final {
    lexical_stream stream;
    server_status status =
        server_status::success;
    source_preparation_failure failure;
};

// Owns only the bounded parallel physical preparation of the initial
// Project-declared Header/Source set.
class source_preparation final {
public:
    source_preparation(
        file_context& files,
        lexical_generation& lexical,
        source_preparation_failure* failure) noexcept
        : files(files),
          lexical(lexical),
          failure(failure) {
    }

    [[nodiscard]] server_status run() noexcept {

        if (failure != nullptr) {
            *failure = {};
        }

        const auto root_count =
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

        return lex_range(
            0,
            root_count,
            lexical_file_count,
            lexical_weight);
    }

private:
    static void lexical_lane_entry(
        void* context,
        std::size_t lane) noexcept {

        static_cast<source_preparation*>(
            context)
            ->run_lexical_lane(
                lane);
    }

    [[nodiscard]] server_status materialize_file(
        file_id file) noexcept {

        if (!files.contains(file)) {
            return server_status::
                project_configuration_invalid;
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

            return server_status::
                project_configuration_invalid;
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
                materialize_file(
                    file);

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

            return server_status::
                project_configuration_invalid;
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

            return server_status::
                project_configuration_invalid;
        }

        pools[lane] = {
            pool_begin,
            end,
        };

        return server_status::success;
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
                lex_source_file(
                    files,
                    lexical,
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

    file_context& files;
    lexical_generation& lexical;
    source_preparation_failure* failure = nullptr;

    std::size_t lane_capacity = 0;

    std::unique_ptr<file_pool[]> pools;
    std::unique_ptr<lexical_lane_state[]> lanes;

    execution_lanes workers;
};


class source_replacement final {
public:
    source_replacement(
        file_context& files,
        lexical_generation& lexical,
        std::span<const file_id> replacement_files,
        source_preparation_failure* failure) noexcept
        : files(files),
          lexical(lexical),
          replacement_files(replacement_files),
          failure(failure) {
    }

    [[nodiscard]] server_status run(
        source_replacement_metrics& metrics) noexcept {

        metrics = {};

        if (failure != nullptr) {
            *failure = {};
        }

        if (!lexical.baseline_bound()) {
            return server_status::
                project_artifact_invalid;
        }

        if (replacement_files.empty()) {
            return server_status::success;
        }

        const auto baseline_file_count =
            lexical.size();

        if (files.size() <
            baseline_file_count) {

            return server_status::
                project_artifact_invalid;
        }

        const auto extended =
            lexical.extend(
                files.size());

        if (!succeeded(extended)) {
            return extended;
        }

        std::uint32_t previous = 0;
        std::size_t lexical_file_count = 0;
        std::uint64_t lexical_weight = 0;

        for (const auto file :
             replacement_files) {

            if (!file ||
                file.value() <= previous ||
                !files.contains(file)) {

                return server_status::
                    project_artifact_invalid;
            }

            previous =
                file.value();

            if (!lexical_kind(
                    files.kind(file))) {

                continue;
            }

            if (file.value() <=
                baseline_file_count) {

                if (!lexical.contains(file)) {
                    return server_status::
                        project_artifact_invalid;
                }

                const auto begun =
                    lexical.begin_replacement(
                        file);

                if (!succeeded(begun) ||
                    lexical.contains(file)) {

                    return succeeded(begun)
                        ? server_status::
                            project_artifact_invalid
                        : begun;
                }

                ++metrics.masked_files;
            } else {
                if (lexical.contains(file)) {
                    return server_status::
                        project_artifact_invalid;
                }

                const auto materialized =
                    materialize_appended(
                        file);

                if (!succeeded(materialized)) {
                    return materialized;
                }
            }

            const auto* physical =
                files.physical(file);

            if (physical == nullptr) {
                return server_status::
                    project_artifact_invalid;
            }

            if (!physical->present()) {
                ++metrics.missing_files;
                continue;
            }

            if (!files.content_available(file)) {
                return server_status::
                    project_artifact_invalid;
            }

            const auto size =
                files.content(file).size();

            const auto weight =
                static_cast<std::uint64_t>(
                    size == 0
                        ? 1
                        : size);

            if (lexical_weight >
                (std::numeric_limits<
                    std::uint64_t>::max)() -
                    weight) {

                return server_status::io_error;
            }

            lexical_weight +=
                weight;

            ++lexical_file_count;
        }

        if (lexical_file_count == 0) {
            return server_status::success;
        }

        lane_capacity =
            (std::min)(
                lexical_file_count,
                execution_lane_capacity());

        if (lane_capacity == 0 ||
            lane_capacity >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)())) {

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

        const auto pooled =
            build_pools(
                lexical_file_count,
                lexical_weight,
                lane_capacity);

        if (!succeeded(pooled)) {
            return pooled;
        }

        const auto started =
            workers.start(
                lane_capacity);

        if (!succeeded(started)) {
            return started;
        }

        const auto parallel =
            workers.run(
                lane_capacity,
                lane_entry,
                this);

        if (!succeeded(parallel)) {
            return parallel;
        }

        for (std::size_t lane = 0;
             lane < lane_capacity;
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

        metrics.retokenized_files =
            lexical_file_count;

        metrics.active_lanes =
            lane_capacity;

        return server_status::success;
    }

private:
    static void lane_entry(
        void* context,
        std::size_t lane) noexcept {

        static_cast<source_replacement*>(
            context)
            ->run_lane(lane);
    }

    [[nodiscard]] server_status materialize_appended(
        file_id file) noexcept {

        if (!files.contains(file)) {
            return server_status::
                project_configuration_invalid;
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
            : server_status::
                project_artifact_invalid;
    }

    [[nodiscard]] bool work_file(
        file_id file) const noexcept {

        if (!lexical_kind(
                files.kind(file))) {

            return false;
        }

        const auto* physical =
            files.physical(file);

        return physical != nullptr &&
            physical->present();
    }

    [[nodiscard]] server_status build_pools(
        std::size_t lexical_file_count,
        std::uint64_t lexical_weight,
        std::size_t active_lanes) noexcept {

        if (active_lanes == 0 ||
            active_lanes > lane_capacity ||
            lexical_file_count <
                active_lanes ||
            lexical_weight <
                static_cast<std::uint64_t>(
                    lexical_file_count)) {

            return server_status::
                project_artifact_invalid;
        }

        std::size_t lane = 0;
        std::size_t pool_begin = 0;
        std::size_t remaining_files =
            lexical_file_count;

        std::uint64_t assigned_weight = 0;
        std::uint64_t pool_weight = 0;

        for (std::size_t position = 0;
             position <
                replacement_files.size();
             ++position) {

            const auto file =
                replacement_files[
                    position];

            if (!work_file(file)) {
                continue;
            }

            const auto size =
                files.content(file).size();

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
                position + 1,
            };

            ++lane;

            pool_begin =
                position + 1;

            assigned_weight +=
                pool_weight;

            pool_weight = 0;
        }

        if (lane + 1 !=
            active_lanes) {

            return server_status::
                project_artifact_invalid;
        }

        pools[lane] = {
            pool_begin,
            replacement_files.size(),
        };

        return server_status::success;
    }

    void run_lane(
        std::size_t lane) noexcept {

        auto& state =
            lanes[lane];

        state.status =
            server_status::success;

        state.failure = {};

        const auto pool =
            pools[lane];

        const auto arena =
            static_cast<std::uint32_t>(
                lane);

        for (auto position =
                 pool.begin;
             position <
                 pool.end;
             ++position) {

            const auto file =
                replacement_files[
                    position];

            if (!work_file(file)) {
                continue;
            }

            state.status =
                lex_source_file(
                    files,
                    lexical,
                    file,
                    arena,
                    state.stream,
                    &state.failure);

            if (!succeeded(
                    state.status)) {

                return;
            }
        }
    }

    file_context& files;
    lexical_generation& lexical;
    std::span<const file_id> replacement_files;
    source_preparation_failure* failure = nullptr;

    std::size_t lane_capacity = 0;

    std::unique_ptr<file_pool[]> pools;
    std::unique_ptr<lexical_lane_state[]> lanes;

    execution_lanes workers;
};

}

server_status prepare_source_lexical_state(
    file_context& files,
    lexical_generation& lexical,
    source_preparation_failure* failure) noexcept {

    source_preparation preparation{
        files,
        lexical,
        failure};

    return preparation.run();
}

server_status replace_source_lexical_state(
    file_context& files,
    lexical_generation& lexical,
    std::span<const file_id> replacement_files,
    source_preparation_failure* failure,
    source_replacement_metrics* metrics) noexcept {

    source_replacement_metrics local;
    source_replacement replacement{
        files,
        lexical,
        replacement_files,
        failure};
    const auto result = replacement.run(local);
    if (metrics != nullptr) *metrics = succeeded(result) ? local : source_replacement_metrics{};
    return result;
}

}
