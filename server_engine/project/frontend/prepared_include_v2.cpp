#include "prepared_include_v2.hpp"

#include "lexer.hpp"
#include "../construction/execution_lanes.hpp"
#include "../file/file_context.hpp"
#include "../preprocessor_configuration.hpp"
#include "../../filesystem_path.hpp"

#include <algorithm>
#include <bit> // HEADER-V2-REAL-LITERAL-11
#include <charconv>
#include <chrono>
#include <filesystem>
#include <limits>
#include <new>
#include <system_error>
#include <unordered_map> // FRONTEND-PHYSICAL-12
#include <utility>
#include <vector>

namespace cw::server {
namespace {

enum class prepared_file_state_v2 : std::uint8_t {
    unseen = 0,
    queued,
    ready,
    missing,
    io_error,
    lexical_error,
};

struct prepared_file_storage_v2 final {
    file_id file{};
    lexical_stream lexical;
    lexical_symbol_stream_v2 symbols;
    prepared_literal_stream_v2 literals;
    std::vector<prepared_include_record_v2> includes;

    prepared_file_state_v2 state =
        prepared_file_state_v2::unseen;

    server_status lexical_status =
        server_status::success;

    bool root = false;
};

// PARSER-V2-PERF-03: invoked only when an optional profile is supplied.
using prepare_clock_v2 = std::chrono::steady_clock;

[[nodiscard]] double prepared_elapsed_ms_v2(
    prepare_clock_v2::time_point begin) noexcept {
    return std::chrono::duration<double, std::milli>{
        prepare_clock_v2::now() - begin}.count();
}

// FRONTEND-PHYSICAL-12: one temporary memo per physical include closure.
// Both spelling and requested form matter: <...> searches configured include
// directories before the local directory, while "..." tries local first.
// A native local candidate path already encodes the source directory.
struct positive_include_key_v2 final {
    std::filesystem::path local;
    bool quoted = false;

    [[nodiscard]] bool operator==(const positive_include_key_v2& other)
        const noexcept {
        return quoted == other.quoted && local == other.local;
    }
};

struct positive_include_key_hash_v2 final {
    [[nodiscard]] std::size_t operator()(
        const positive_include_key_v2& key) const noexcept {
        const auto hash = std::filesystem::hash_value(key.local);
        // Quoted and angled lookups cannot alias, even on a hash collision:
        // unordered_map additionally compares the entire native path/form.
        return hash ^ (key.quoted ? std::size_t{0x9e3779b9u}
                                   : std::size_t{0x85ebca6bu});
    }
};

struct physical_token_v2 final {
    token_kind kind = token_kind::invalid;
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;
};

[[nodiscard]] constexpr std::uint32_t file_hash_v2(
    std::uint32_t value) noexcept {

    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;

    return value == 0
        ? 1
        : value;
}

[[nodiscard]] server_status decode_physical_token_v2(
    std::span<const std::uint32_t> words,
    std::size_t& word_offset,
    std::uint32_t& source_offset,
    std::size_t token_index,
    physical_token_v2& output) noexcept {

    output = {};

    auto position =
        word_offset;

    if (position >= words.size()) {
        return server_status::
            project_configuration_invalid;
    }

    const auto header =
        lexical_token::from_value(
            words[position++]);

    if (!header) {
        return server_status::
            project_configuration_invalid;
    }

    auto delta =
        header.delta();

    if (delta ==
        lexical_token::extended_delta) {

        if (position >= words.size()) {
            return server_status::
                project_configuration_invalid;
        }

        delta =
            words[position++];
    }

    auto length =
        header.length();

    if (length ==
        lexical_token::extended_length) {

        if (position >= words.size()) {
            return server_status::
                project_configuration_invalid;
        }

        length =
            words[position++];
    }

    if (token_index != 0 &&
        delta >
            (std::numeric_limits<std::uint32_t>::max)() -
                source_offset) {

        return server_status::
            project_configuration_invalid;
    }

    const auto source =
        token_index == 0
            ? delta
            : source_offset + delta;

    word_offset =
        position;

    source_offset =
        source;

    output = {
        header.kind(),
        source,
        length,
    };

    return server_status::success;
}

[[nodiscard]] prepared_number_v2 parse_prepared_number_v2(
    std::string_view spelling) noexcept {

    if (spelling.empty()) {
        return {};
    }

    // HEADER-V2-REAL-LITERAL-11: OLD recognizes decimal real pp-numbers
    // containing '.', 'e' or 'E'. Decode once in physical preparation, never
    // parse source text inside Header semantic execution. from_chars must
    // consume the whole token; suffixes, hex and overflow stay unsupported.
    if (spelling.find_first_of(".eE") != std::string_view::npos) {
        double value = 0;
        const auto parsed = std::from_chars(
            spelling.data(), spelling.data() + spelling.size(), value);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != spelling.data() + spelling.size()) {
            return {};
        }
        return {
            std::bit_cast<std::uint64_t>(value),
            prepared_number_kind_v2::real,
            {},
        };
    }

    std::uint64_t value = 0;

    const auto converted =
        std::from_chars(
            spelling.data(),
            spelling.data() +
                spelling.size(),
            value,
            10);

    if (converted.ec !=
            std::errc{} ||
        converted.ptr !=
            spelling.data() +
                spelling.size()) {

        return {};
    }

    return {
        value,
        prepared_number_kind_v2::
            unsigned_integer,
        {},
    };
}

[[nodiscard]] server_status prepare_literals_v2(
    file_id file,
    std::string_view source,
    std::span<const std::uint32_t> words,
    prepared_literal_stream_v2& output) noexcept {

    auto status =
        output.reset(
            file);

    if (!succeeded(status)) {
        return status;
    }

    std::size_t word_offset = 0;
    std::uint32_t source_offset = 0;
    std::size_t token_index = 0;

    while (word_offset <
           words.size()) {

        physical_token_v2 token;

        status =
            decode_physical_token_v2(
                words,
                word_offset,
                source_offset,
                token_index++,
                token);

        if (!succeeded(status)) {
            return status;
        }

        if (token.kind !=
            token_kind::pp_number) {

            continue;
        }

        const auto begin =
            static_cast<std::size_t>(
                token.source_offset);

        const auto length =
            static_cast<std::size_t>(
                token.source_length);

        if (begin > source.size() ||
            length >
                source.size() - begin) {

            return server_status::
                project_artifact_invalid;
        }

        status =
            output.record(
                parse_prepared_number_v2(
                    source.substr(
                        begin,
                        length)));

        if (!succeeded(status)) {
            return status;
        }
    }

    return server_status::success;
}

[[nodiscard]] prepared_include_state_v2 file_state_to_include_state(
    prepared_file_state_v2 state) noexcept {

    switch (state) {
    case prepared_file_state_v2::ready:
        return prepared_include_state_v2::ready;

    case prepared_file_state_v2::missing:
        return prepared_include_state_v2::missing;

    case prepared_file_state_v2::io_error:
        return prepared_include_state_v2::io_error;

    case prepared_file_state_v2::lexical_error:
        return prepared_include_state_v2::lexical_error;

    case prepared_file_state_v2::unseen:
    case prepared_file_state_v2::queued:
        return prepared_include_state_v2::invalid;
    }

    return prepared_include_state_v2::invalid;
}

[[nodiscard]] bool missing_path_error(
    const std::error_code& error) noexcept {

    if (!error) {
        return false;
    }

    // Filesystem APIs may report native system_category errors on Windows
    // (for example ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND). Compare the
    // portable default condition instead of comparing two error_code objects
    // whose categories may differ.
    const auto condition =
        error.default_error_condition();

    return condition ==
            std::make_error_condition(
                std::errc::no_such_file_or_directory) ||
        condition ==
            std::make_error_condition(
                std::errc::not_a_directory);
}

[[nodiscard]] server_status regular_file_v2(
    const std::filesystem::path& path,
    bool& output) noexcept {

    output = false;

    std::error_code error;

    output =
        std::filesystem::is_regular_file(
            path,
            error);

    if (!error) {
        return server_status::success;
    }

    if (missing_path_error(error)) {
        output = false;
        return server_status::success;
    }

    return server_status::io_error;
}

}

struct prepared_include_closure_v2::state final {
    std::vector<
        std::unique_ptr<
            prepared_file_storage_v2>>
        files;

    std::vector<
        prepared_include_file_slot_v2>
        index;

    std::vector<
        prepared_physical_file_v2>
        views;

    std::size_t ready_file_count = 0;
};

namespace {

class prepared_include_builder_v2 final {
public:
    prepared_include_builder_v2(
        file_context& files_value,
        const preprocessor_configuration& configuration_value,
        prepared_include_closure_v2::state& output_value,
        prepared_include_failure_v2* failure_value,
        prepared_include_profile_v2* profile_value) noexcept
        : files(files_value),
          configuration(configuration_value),
          output(output_value),
          failure(failure_value),
          profile(profile_value) {
    }

    [[nodiscard]] server_status run(
        std::span<const file_id> roots) noexcept {

        const auto total_started = profile != nullptr
            ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
        if (profile != nullptr) *profile = {};

        if (failure != nullptr) {
            *failure = {};
        }

        output.files.clear();
        output.index.clear();
        output.views.clear();
        output.ready_file_count = 0;

        if (roots.empty()) {
            return server_status::
                project_configuration_invalid;
        }

        try {
            frontier.reserve(
                roots.size());
        }
        catch (...) {
            return server_status::io_error;
        }

        for (const auto root : roots) {
            if (!root ||
                !files.contains(root) ||
                files.kind(root) !=
                    file_kind::header) {

                return fail_root(
                    root,
                    prepared_include_state_v2::invalid,
                    server_status::
                        project_configuration_invalid);
            }

            prepared_file_storage_v2* storage = nullptr;

            const auto prepared =
                ensure_file(
                    root,
                    storage);

            if (!succeeded(prepared) ||
                storage == nullptr) {

                return succeeded(prepared)
                    ? server_status::
                        project_artifact_invalid
                    : prepared;
            }

            if (storage->root) {
                return fail_root(
                    root,
                    prepared_include_state_v2::invalid,
                    server_status::
                        project_configuration_invalid);
            }

            storage->root = true;

            if (storage->state ==
                prepared_file_state_v2::unseen) {

                storage->state =
                    prepared_file_state_v2::queued;

                try {
                    frontier.push_back(
                        root);
                }
                catch (...) {
                    return server_status::io_error;
                }
            }
        }

        std::sort(
            frontier.begin(),
            frontier.end(),
            [](file_id left, file_id right) noexcept {
                return left.value() < right.value();
            });

        auto lane_count =
            execution_lane_capacity();

        if (lane_count == 0) {
            lane_count = 1;
        }

        const auto workers_started = profile != nullptr
            ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
        const auto started =
            workers.start(
                lane_count);
        if (profile != nullptr) {
            profile->workers_start_ms += prepared_elapsed_ms_v2(workers_started);
        }

        if (!succeeded(started)) {
            return started;
        }

        lane_capacity =
            lane_count;

        if (profile != nullptr) {
            try { lane_profiles.resize(lane_count); }
            catch (...) { return server_status::io_error; }
        }

        while (!frontier.empty()) {
            if (profile != nullptr) {
                ++profile->frontiers;
                profile->peak_frontier_files = (std::max)(
                    profile->peak_frontier_files, frontier.size());
            }
            const auto materialize_started = profile != nullptr
                ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
            const auto materialized =
                materialize_frontier();
            if (profile != nullptr) {
                profile->materialize_wall_ms +=
                    prepared_elapsed_ms_v2(materialize_started);
            }

            if (!succeeded(materialized)) {
                return materialized;
            }

            const auto lexical_started = profile != nullptr
                ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
            const auto lexed =
                lex_frontier();
            if (profile != nullptr) {
                profile->lexical_wall_ms +=
                    prepared_elapsed_ms_v2(lexical_started);
            }

            if (!succeeded(lexed)) {
                return lexed;
            }

            for (const auto file :
                 frontier) {

                const auto* storage =
                    find_file(
                        file);

                if (storage == nullptr) {
                    return server_status::
                        project_artifact_invalid;
                }

                if (storage->root &&
                    storage->state !=
                        prepared_file_state_v2::ready) {

                    return fail_root(
                        file,
                        file_state_to_include_state(
                            storage->state),
                        succeeded(storage->lexical_status)
                            ? server_status::
                                project_configuration_invalid
                            : storage->lexical_status);
                }
            }

            next_frontier.clear();
            const auto scan_started = profile != nullptr
                ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};

            for (const auto file :
                 frontier) {

                auto* storage =
                    find_file(
                        file);

                if (storage == nullptr) {
                    return server_status::
                        project_artifact_invalid;
                }

                if (storage->state !=
                    prepared_file_state_v2::ready) {

                    continue;
                }

                const auto scanned =
                    scan_includes(
                        *storage);

                if (!succeeded(scanned)) {
                    return scanned;
                }
                if (profile != nullptr) {
                    profile->include_records += storage->includes.size();
                }
            }
            if (profile != nullptr) {
                profile->include_scan_ms += prepared_elapsed_ms_v2(scan_started);
            }
            const auto sort_started = profile != nullptr
                ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};

            std::sort(
                next_frontier.begin(),
                next_frontier.end(),
                [](file_id left, file_id right) noexcept {
                    return left.value() < right.value();
                });

            frontier =
                std::move(next_frontier);

            next_frontier.clear();
            if (profile != nullptr) {
                profile->frontier_sort_ms += prepared_elapsed_ms_v2(sort_started);
            }
        }

        const auto finalize_started = profile != nullptr
            ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
        const auto result = finalize();
        if (profile != nullptr) {
            profile->finalize_ms += prepared_elapsed_ms_v2(finalize_started);
            profile->ready_files = output.ready_file_count;
            for (const auto& lane : lane_profiles) {
                profile->read_task_elapsed_sum_ms += lane.read_ms;
                profile->lexer_task_elapsed_sum_ms += lane.lexer_ms;
                profile->literal_task_elapsed_sum_ms += lane.literal_ms;
                profile->lexed_files += lane.lexed_files;
            }
            profile->total_ms = prepared_elapsed_ms_v2(total_started);
        }
        return result;
    }

private:
    // Per-lane counters have a single writer; collected only after workers.run.
    struct lane_profile final {
        double read_ms = 0;
        double lexer_ms = 0;
        double literal_ms = 0;
        std::size_t lexed_files = 0;
    };

    struct acquisition_task final {
        file_id file{};
        file_acquire_job job;
        file_acquire_result result;
    };

    [[nodiscard]] server_status fail_root(
        file_id file,
        prepared_include_state_v2 state,
        server_status status) noexcept {

        if (failure != nullptr) {
            *failure = {
                file,
                {},
                state,
            };
        }

        return status;
    }

    [[nodiscard]] prepared_file_storage_v2* find_file(
        file_id file) noexcept {

        if (!file ||
            output.index.empty()) {

            return nullptr;
        }

        const auto mask =
            output.index.size() - 1;

        auto position =
            static_cast<std::size_t>(
                file_hash_v2(
                    file.value())) &
            mask;

        for (std::size_t probe = 0;
             probe < output.index.size();
             ++probe) {

            const auto& slot =
                output.index[position];

            if (!slot.file) {
                return nullptr;
            }

            if (slot.file == file) {
                if (slot.record == 0 ||
                    slot.record >
                        output.files.size()) {

                    return nullptr;
                }

                return output.files[
                    slot.record - 1].get();
            }

            position =
                (position + 1) &
                mask;
        }

        return nullptr;
    }

    [[nodiscard]] const prepared_file_storage_v2* find_file(
        file_id file) const noexcept {

        return const_cast<
            prepared_include_builder_v2*>(
                this)
            ->find_file(
                file);
    }

    void insert_file_index(
        std::vector<prepared_include_file_slot_v2>& index,
        file_id file,
        std::uint32_t record) const noexcept {

        const auto mask =
            index.size() - 1;

        auto position =
            static_cast<std::size_t>(
                file_hash_v2(
                    file.value())) &
            mask;

        while (index[position].file) {
            position =
                (position + 1) &
                mask;
        }

        index[position] = {
            file,
            record,
        };
    }

    [[nodiscard]] server_status ensure_index_capacity(
        std::size_t additional) noexcept {

        if (additional >
            (std::numeric_limits<std::size_t>::max)() -
                output.files.size()) {

            return server_status::io_error;
        }

        const auto required =
            output.files.size() +
            additional;

        if (!output.index.empty() &&
            required <=
                output.index.size() / 2) {

            return server_status::success;
        }

        if (required >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            return server_status::io_error;
        }

        const auto minimum =
            required * 2;

        std::size_t capacity = 8;

        while (capacity < minimum) {
            if (capacity >
                (std::numeric_limits<std::size_t>::max)() / 2) {

                return server_status::io_error;
            }

            capacity *= 2;
        }

        try {
            std::vector<
                prepared_include_file_slot_v2>
                candidate(
                    capacity);

            for (std::size_t index = 0;
                 index < output.files.size();
                 ++index) {

                const auto& file =
                    output.files[index];

                if (!file ||
                    !file->file) {

                    return server_status::
                        project_artifact_invalid;
                }

                insert_file_index(
                    candidate,
                    file->file,
                    static_cast<std::uint32_t>(
                        index + 1));
            }

            output.index =
                std::move(candidate);

            return server_status::success;
        }
        catch (...) {
            return server_status::io_error;
        }
    }

    [[nodiscard]] server_status ensure_file(
        file_id file,
        prepared_file_storage_v2*& result) noexcept {

        result =
            find_file(
                file);

        if (result != nullptr) {
            return server_status::success;
        }

        if (!file ||
            output.files.size() >=
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {

            return server_status::io_error;
        }

        const auto prepared =
            ensure_index_capacity(
                1);

        if (!succeeded(prepared)) {
            return prepared;
        }

        try {
            auto storage =
                std::make_unique<
                    prepared_file_storage_v2>();

            storage->file =
                file;

            output.files.push_back(
                std::move(storage));

            const auto record =
                static_cast<std::uint32_t>(
                    output.files.size());

            insert_file_index(
                output.index,
                file,
                record);

            result =
                output.files.back().get();

            return server_status::success;
        }
        catch (...) {
            return server_status::io_error;
        }
    }

    static void acquisition_lane_entry(
        void* context,
        std::size_t lane) noexcept {

        auto& owner =
            *static_cast<
                prepared_include_builder_v2*>(
                    context);

        for (auto index = lane;
             index <
                owner.acquisition_tasks.size();
             index += owner.active_lanes) {

            auto& task =
                owner.acquisition_tasks[
                    index];

            const auto task_started = owner.profile != nullptr
                ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
            file_context::execute_acquire(
                task.job,
                task.result);
            if (owner.profile != nullptr) {
                owner.lane_profiles[lane].read_ms +=
                    prepared_elapsed_ms_v2(task_started);
            }
        }
    }

    [[nodiscard]] server_status materialize_frontier() noexcept {

        const auto preparation_started = profile != nullptr
            ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
        acquisition_tasks.clear();

        try {
            acquisition_tasks.reserve(
                frontier.size());
        }
        catch (...) {
            return server_status::io_error;
        }

        for (const auto file :
             frontier) {

            auto* storage =
                find_file(
                    file);

            if (storage == nullptr) {
                return server_status::
                    project_artifact_invalid;
            }

            if (storage->state !=
                prepared_file_state_v2::queued) {

                continue;
            }

            if (files.content_available(
                    file)) {

                continue;
            }

            file_acquire_job job;

            const auto prepared =
                files.prepare_acquire(
                    file,
                    job);

            if (!succeeded(prepared)) {
                storage->state =
                    prepared_file_state_v2::
                        io_error;

                storage->lexical_status =
                    prepared;

                continue;
            }

            try {
                acquisition_tasks.push_back({
                    file,
                    job,
                    {},
                });
            }
            catch (...) {
                return server_status::io_error;
            }
        }

        if (profile != nullptr) {
            profile->acquisition_prepare_ms +=
                prepared_elapsed_ms_v2(preparation_started);
            profile->read_tasks += acquisition_tasks.size();
        }

        if (!acquisition_tasks.empty()) {
            active_lanes =
                (std::min)(
                    lane_capacity,
                    acquisition_tasks.size());
            const auto read_started = profile != nullptr
                ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};

            const auto executed =
                workers.run(
                    active_lanes,
                    acquisition_lane_entry,
                    this);
            if (profile != nullptr) {
                profile->acquisition_read_wall_ms +=
                    prepared_elapsed_ms_v2(read_started);
            }

            if (!succeeded(executed)) {
                return executed;
            }
        }

        const auto apply_started = profile != nullptr
            ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
        for (auto& task :
             acquisition_tasks) {

            auto* storage =
                find_file(
                    task.file);

            if (storage == nullptr) {
                return server_status::
                    project_artifact_invalid;
            }

            if (task.result.kind ==
                file_acquire_result_kind::
                    missing) {

                storage->state =
                    prepared_file_state_v2::
                        missing;

                storage->lexical_status =
                    server_status::
                        project_configuration_invalid;

                continue;
            }

            if (task.result.kind !=
                file_acquire_result_kind::
                    present) {

                storage->state =
                    prepared_file_state_v2::
                        io_error;

                storage->lexical_status =
                    server_status::io_error;

                continue;
            }

            bool changed = false;

            const auto applied =
                files.apply_acquire(
                    task.result,
                    changed);

            if (!succeeded(applied) ||
                !files.content_available(
                    task.file)) {

                storage->state =
                    prepared_file_state_v2::
                        io_error;

                storage->lexical_status =
                    succeeded(applied)
                        ? server_status::
                            project_artifact_invalid
                        : applied;
            }
        }

        if (profile != nullptr) {
            profile->acquisition_apply_ms += prepared_elapsed_ms_v2(apply_started);
        }
        return server_status::success;
    }

    static void lexical_lane_entry(
        void* context,
        std::size_t lane) noexcept {

        auto& owner =
            *static_cast<
                prepared_include_builder_v2*>(
                    context);

        for (auto index = lane;
             index < owner.frontier.size();
             index += owner.active_lanes) {

            const auto file =
                owner.frontier[index];

            auto* storage =
                owner.find_file(
                    file);

            if (storage == nullptr ||
                storage->state !=
                    prepared_file_state_v2::
                        queued ||
                !owner.files.content_available(
                    file)) {

                continue;
            }

            lexical_error error;
            const auto lexer_started = owner.profile != nullptr
                ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};

            storage->lexical_status =
                lexer::tokenize(
                    file,
                    owner.files.content(
                        file),
                    storage->lexical,
                    &error,
                    &storage->symbols);
            if (owner.profile != nullptr) {
                owner.lane_profiles[lane].lexer_ms +=
                    prepared_elapsed_ms_v2(lexer_started);
                ++owner.lane_profiles[lane].lexed_files;
            }

            if (succeeded(
                    storage->lexical_status)) {
                const auto literal_started = owner.profile != nullptr
                    ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};

                storage->lexical_status =
                    prepare_literals_v2(
                        file,
                        owner.files.content(
                            file),
                        storage->lexical.words(),
                        storage->literals);
                if (owner.profile != nullptr) {
                    owner.lane_profiles[lane].literal_ms +=
                        prepared_elapsed_ms_v2(literal_started);
                }
            }

            storage->state =
                succeeded(
                    storage->lexical_status)
                ? prepared_file_state_v2::
                    ready
                : prepared_file_state_v2::
                    lexical_error;
        }
    }

    [[nodiscard]] server_status lex_frontier() noexcept {

        if (frontier.empty()) {
            return server_status::success;
        }

        active_lanes =
            (std::min)(
                lane_capacity,
                frontier.size());

        return workers.run(
            active_lanes,
            lexical_lane_entry,
            this);
    }

    // FRONTEND-PHYSICAL-12: common publication for cache hits and misses.
    [[nodiscard]] server_status publish_candidate_target(
        file_id target,
        prepared_include_record_v2& record) noexcept {

        prepared_file_storage_v2* target_storage = nullptr;
        const auto ensure_started = profile != nullptr
            ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
        const auto prepared = ensure_file(target, target_storage);
        if (profile != nullptr) {
            ++profile->include_ensure_file_calls;
            profile->include_ensure_file_wall_ms +=
                prepared_elapsed_ms_v2(ensure_started);
        }
        if (!succeeded(prepared) || target_storage == nullptr) {
            return succeeded(prepared)
                ? server_status::project_artifact_invalid : prepared;
        }
        record.target = target;
        record.state = prepared_include_state_v2::ready;
        if (target_storage->state == prepared_file_state_v2::unseen) {
            target_storage->state = prepared_file_state_v2::queued;
            try { next_frontier.push_back(target); }
            catch (...) { return server_status::io_error; }
        }
        return server_status::success;
    }

    [[nodiscard]] server_status resolve_candidate(
        file_id source_file,
        physical_token_v2 token,
        prepared_include_record_v2& record) noexcept {

        record = {
            prepared_include_state_v2::invalid,
            {},
            {
                token.source_offset,
                token.source_length,
            },
        };

        const auto source =
            files.content(
                source_file);

        const auto begin =
            static_cast<std::size_t>(
                token.source_offset);

        const auto length =
            static_cast<std::size_t>(
                token.source_length);

        if (begin > source.size() ||
            length > source.size() - begin ||
            length < 2) {

            return server_status::success;
        }

        const auto spelling =
            source.substr(
                begin,
                length);

        const auto quoted =
            token.kind ==
                token_kind::
                    header_name_quoted;

        const auto angled =
            token.kind ==
                token_kind::
                    header_name_angled;

        if ((!quoted && !angled) ||
            spelling.front() !=
                (quoted ? '"' : '<') ||
            spelling.back() !=
                (quoted ? '"' : '>')) {

            return server_status::success;
        }

        const auto locator_text =
            spelling.substr(
                1,
                spelling.size() - 2);

        if (locator_text.empty()) {
            return server_status::success;
        }

        std::filesystem::path locator;

        if (filesystem_path_from_utf8(
                locator_text,
                locator) !=
            filesystem_path_result::success) {

            return server_status::success;
        }

        try {
            const auto source_path_view =
                files.path(
                    source_file);

            const std::filesystem::path source_path{
                source_path_view.begin(),
                source_path_view.end()};

            const auto local =
                source_path.parent_path() /
                locator;

            file_id target;
            if (profile != nullptr) {
                ++profile->include_cache_lookups;
            }
#ifndef CW_PREPARED_INCLUDE_RESOLUTION_REFERENCE
            const positive_include_key_v2 cache_key{local, quoted};
            const auto cached = positive_includes.find(cache_key);
            const bool cache_hit = cached != positive_includes.end();
            if (cache_hit) {
                target = cached->second;
                if (profile != nullptr) ++profile->include_positive_cache_hits;
            }
            else if (profile != nullptr) {
                ++profile->include_cache_misses;
            }
#else
            if (profile != nullptr) ++profile->include_cache_misses;
#endif
            if (!target) {
            std::filesystem::path selected;
            bool found = false;

            const auto try_path =
                [&](const std::filesystem::path& candidate)
                    -> server_status {

                    bool regular = false;

                    const auto probe_started = profile != nullptr
                        ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
                    const auto checked =
                        regular_file_v2(
                            candidate,
                            regular);
                    if (profile != nullptr) {
                        ++profile->include_filesystem_probe_calls;
                        profile->include_filesystem_probe_wall_ms +=
                            prepared_elapsed_ms_v2(probe_started);
                    }

                    if (!succeeded(checked)) {
                        record.state =
                            prepared_include_state_v2::
                                io_error;

                        return checked;
                    }

                    if (regular && !found) {
                        selected =
                            candidate;

                        found = true;
                    }

                    return server_status::success;
                };

            if (locator.is_absolute()) {
                const auto checked =
                    try_path(
                        locator);

                if (!succeeded(checked)) {
                    return server_status::success;
                }
            }
            else {
                if (quoted) {
                    const auto checked =
                        try_path(
                            local);

                    if (!succeeded(checked)) {
                        return server_status::success;
                    }
                }

                for (const auto& configured :
                     configuration.include_directories) {

                    if (found) {
                        break;
                    }

                    std::filesystem::path directory;

                    if (filesystem_path_from_utf8(
                            configured,
                            directory) !=
                        filesystem_path_result::
                            success) {

                        return server_status::
                            project_configuration_invalid;
                    }

                    const auto checked =
                        try_path(
                            configuration.root_directory /
                            directory /
                            locator);

                    if (!succeeded(checked)) {
                        return server_status::success;
                    }
                }

                if (!found && angled) {
                    const auto checked =
                        try_path(
                            local);

                    if (!succeeded(checked)) {
                        return server_status::success;
                    }
                }
            }

            if (!found) {
                record.state =
                    prepared_include_state_v2::
                        missing;

                return server_status::success;
            }

            const auto file_resolve_started = profile != nullptr
                ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
            const auto resolved =
                files.resolve(
                    selected,
                    file_kind::header,
                    target);
            if (profile != nullptr) {
                ++profile->include_file_resolve_calls;
                profile->include_file_resolve_wall_ms +=
                    prepared_elapsed_ms_v2(file_resolve_started);
            }

            if (!succeeded(resolved) ||
                !target) {

                return succeeded(resolved)
                    ? server_status::
                        project_configuration_invalid
                    : resolved;
            }
            } // Cache misses use the original ordered path/probe resolution.

            const auto published = publish_candidate_target(target, record);
            if (!succeeded(published)) {
                return published;
            }
#ifndef CW_PREPARED_INCLUDE_RESOLUTION_REFERENCE
            if (!cache_hit) {
                // Memoization is optional. An allocation failure must not turn
                // an otherwise valid include into a semantic error.
                try {
                    const auto inserted = positive_includes.emplace(cache_key, target);
                    if (profile != nullptr && inserted.second) {
                        ++profile->include_positive_cache_entries;
                    }
                }
                catch (...) {
                }
            }
#endif
            return server_status::success;
        }
        catch (...) {
            record.state =
                prepared_include_state_v2::
                    io_error;

            return server_status::success;
        }
    }

    [[nodiscard]] server_status scan_includes(
        prepared_file_storage_v2& storage) noexcept {

        storage.includes.clear();

        const auto words =
            storage.lexical.words();

        // FRONTEND-PHYSICAL-11: collect wall time for stream walking separately
        // from candidate resolution; timers are disabled in ordinary V2.
        const auto scan_started = profile != nullptr
            ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
        double candidate_wall_ms = 0.0;

#ifdef CW_PREPARED_INCLUDE_SCAN_REFERENCE
        // The exact previous whole-stream walk, benchmark-reference only.
        std::size_t word_offset = 0;
        std::uint32_t source_offset = 0;
        std::size_t token_index = 0;

        while (word_offset <
               words.size()) {
#else
        // lexical_stream::append() recorded source_base BEFORE each directive
        // and word_offset at its header. No replay of ordinary C++ tokens.
        for (const auto& anchor : storage.lexical.directives()) {
            if (anchor.word_offset >= words.size()) {
                return server_status::project_artifact_invalid;
            }
            std::size_t word_offset = anchor.word_offset;
            std::uint32_t source_offset = anchor.source_base;
            // decode_physical_token_v2 treats only the first token specially.
            // word offset zero implies token ordinal zero in a valid stream.
            std::size_t token_index = anchor.word_offset == 0 ? 0 : 1;
#endif
            physical_token_v2 token;

            const auto decoded =
                decode_physical_token_v2(
                    words,
                    word_offset,
                    source_offset,
                    token_index++,
                    token);

            if (!succeeded(decoded)) {
                return server_status::
                    project_artifact_invalid;
            }

            if (token.kind !=
                token_kind::pp_include) {

                continue;
            }

            prepared_include_record_v2
                record;

            bool have_argument = false;
            physical_token_v2 argument;

            for (;;) {
                if (word_offset >=
                    words.size()) {

                    return server_status::
                        project_artifact_invalid;
                }

                physical_token_v2 current;

                const auto next =
                    decode_physical_token_v2(
                        words,
                        word_offset,
                        source_offset,
                        token_index++,
                        current);

                if (!succeeded(next)) {
                    return server_status::
                        project_artifact_invalid;
                }

                if (current.kind ==
                    token_kind::pp_end) {

                    break;
                }

                if (!have_argument) {
                    argument =
                        current;

                    have_argument =
                        true;
                }
                else {
                    argument.kind =
                        token_kind::invalid;
                }
            }

            if (have_argument &&
                (argument.kind ==
                        token_kind::
                            header_name_quoted ||
                 argument.kind ==
                        token_kind::
                            header_name_angled)) {

                if (profile != nullptr) {
                    ++profile->include_candidate_calls;
                }
                const auto candidate_started = profile != nullptr
                    ? prepare_clock_v2::now() : prepare_clock_v2::time_point{};
                const auto resolved =
                    resolve_candidate(
                        storage.file,
                        argument,
                        record);
                if (profile != nullptr) {
                    candidate_wall_ms +=
                        prepared_elapsed_ms_v2(candidate_started);
                }

                if (!succeeded(resolved)) {
                    return resolved;
                }
            }
            else {
                record.state =
                    prepared_include_state_v2::
                        invalid;

                record.locator = {
                    token.source_offset,
                    token.source_length,
                };
            }

            try {
                storage.includes.push_back(
                    record);
            }
            catch (...) {
                return server_status::io_error;
            }
        }

        if (profile != nullptr) {
            const auto scanned_ms = prepared_elapsed_ms_v2(scan_started);
            profile->include_candidate_wall_ms += candidate_wall_ms;
            profile->include_stream_walk_wall_ms +=
                (std::max)(0.0, scanned_ms - candidate_wall_ms);
        }
        return server_status::success;
    }

    [[nodiscard]] server_status finalize() noexcept {

        for (auto& source_pointer :
             output.files) {

            if (!source_pointer) {
                return server_status::
                    project_artifact_invalid;
            }

            auto& source =
                *source_pointer;

            if (source.state !=
                prepared_file_state_v2::
                    ready) {

                continue;
            }

            for (auto& include :
                 source.includes) {

                if (!include.target) {
                    continue;
                }

                const auto* target =
                    find_file(
                        include.target);

                include.state =
                    target != nullptr
                    ? file_state_to_include_state(
                        target->state)
                    : prepared_include_state_v2::
                        invalid;
            }
        }

        try {
            output.views.assign(
                output.files.size(),
                prepared_physical_file_v2{});
        }
        catch (...) {
            return server_status::io_error;
        }

        output.ready_file_count = 0;

        for (std::size_t index = 0;
             index < output.files.size();
             ++index) {

            auto& source =
                *output.files[index];

            if (source.state !=
                prepared_file_state_v2::
                    ready) {

                continue;
            }

            output.views[index] = {
                source.file,
                source.lexical.words(),
                &source.symbols,
                &source.literals,
                std::span<
                    const prepared_include_record_v2>{
                        source.includes},
            };

            ++output.ready_file_count;
        }

        return server_status::success;
    }

    file_context& files;

    const preprocessor_configuration&
        configuration;

    prepared_include_closure_v2::state&
        output;

    prepared_include_failure_v2*
        failure = nullptr;
    prepared_include_profile_v2* profile = nullptr;

    execution_lanes workers;
    std::vector<lane_profile> lane_profiles;

    std::size_t lane_capacity = 1;
    std::size_t active_lanes = 1;

    std::vector<file_id> frontier;
    std::vector<file_id> next_frontier;

    std::vector<acquisition_task>
        acquisition_tasks;

#ifndef CW_PREPARED_INCLUDE_RESOLUTION_REFERENCE
    // Positive results only, owned by this builder/run. Inactive speculative
    // includes receive NO semantic identity, and negative results are NOT
    // cached. The map dies before the prepared physical view is published.
    std::unordered_map<positive_include_key_v2, file_id,
                       positive_include_key_hash_v2> positive_includes;
#endif
};

}

prepared_include_closure_v2::
prepared_include_closure_v2() noexcept = default;

prepared_include_closure_v2::
~prepared_include_closure_v2() noexcept = default;

prepared_include_view_v2
prepared_include_closure_v2::view() const noexcept {

    return state_value != nullptr
        ? prepared_include_view_v2::
            from_native(
                state_value->views,
                state_value->index)
        : prepared_include_view_v2{};
}

std::size_t
prepared_include_closure_v2::file_count() const noexcept {

    return state_value != nullptr
        ? state_value->ready_file_count
        : 0;
}

server_status prepare_header_include_closure_v2(
    file_context& files,
    std::span<const file_id> roots,
    const preprocessor_configuration& configuration,
    prepared_include_closure_v2& output,
    prepared_include_failure_v2* failure,
    prepared_include_profile_v2* profile) noexcept {

    try {
        output.state_value =
            std::make_unique<
                prepared_include_closure_v2::
                    state>();
    }
    catch (...) {
        return server_status::io_error;
    }

    prepared_include_builder_v2 builder{
        files,
        configuration,
        *output.state_value,
        failure,
        profile};

    const auto status =
        builder.run(
            roots);

    if (!succeeded(status)) {
        output.state_value.reset();
    }

    return status;
}

}
