/*
 * Parser V2 speculative physical include closure.
 *
 * This layer may resolve/read/lex direct include candidates before semantic
 * execution. It never publishes semantic dependency edges and never assigns
 * global string_id values to speculative include files.
 */
#pragma once

#include "prepared_frontend_v2.hpp"
#include "source_range.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace cw::server {

class file_context;
struct preprocessor_configuration;

enum class prepared_include_state_v2 : std::uint8_t {
    ready = 0,
    missing,
    invalid,
    io_error,
    lexical_error,
};

struct prepared_include_record_v2 final {
    prepared_include_state_v2 state =
        prepared_include_state_v2::invalid;

    file_id target{};
    source_range locator;
};

struct prepared_physical_file_v2 final {
    file_id file{};
    std::span<const std::uint32_t> words;
    const lexical_symbol_stream_v2* symbols = nullptr;
    const prepared_literal_stream_v2* literals = nullptr;
    std::span<const prepared_include_record_v2> includes;

    [[nodiscard]] bool valid() const noexcept {
        return file &&
            symbols != nullptr &&
            literals != nullptr &&
            symbols->file() == file &&
            literals->file() == file;
    }
};

// Sparse file_id -> prepared-record lookup. Physical include closure may be a
// small subset of a very large Project file lineage, so V2 does not allocate a
// second dense per-file table.
struct prepared_include_file_slot_v2 final {
    file_id file{};
    std::uint32_t record = 0;
};

static_assert(sizeof(prepared_include_file_slot_v2) == 8);

class prepared_include_view_v2 final {
public:
    [[nodiscard]] const prepared_physical_file_v2* file(
        file_id id) const noexcept {

        if (!id ||
            index.empty()) {

            return nullptr;
        }

        const auto mask =
            index.size() - 1;

        auto position =
            static_cast<std::size_t>(
                hash_file(
                    id.value())) &
            mask;

        for (std::size_t probe = 0;
             probe < index.size();
             ++probe) {

            const auto& slot =
                index[position];

            if (!slot.file) {
                return nullptr;
            }

            if (slot.file == id) {
                if (slot.record == 0 ||
                    slot.record >
                        files.size()) {

                    return nullptr;
                }

                const auto& value =
                    files[
                        slot.record - 1];

                return value.valid() &&
                    value.file == id
                    ? &value
                    : nullptr;
            }

            position =
                (position + 1) &
                mask;
        }

        return nullptr;
    }

    [[nodiscard]] const prepared_include_record_v2* include(
        file_id source,
        std::size_t occurrence) const noexcept {

        const auto* value =
            file(source);

        return value != nullptr &&
            occurrence < value->includes.size()
            ? &value->includes[occurrence]
            : nullptr;
    }

private:
    [[nodiscard]] static constexpr std::uint32_t hash_file(
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

    [[nodiscard]] static prepared_include_view_v2 from_native(
        std::span<const prepared_physical_file_v2> values,
        std::span<const prepared_include_file_slot_v2> lookup) noexcept {

        prepared_include_view_v2 output;

        output.files =
            values;

        output.index =
            lookup;

        return output;
    }

    std::span<const prepared_physical_file_v2> files;
    std::span<const prepared_include_file_slot_v2> index;

    friend class prepared_include_closure_v2;
};

// PARSER-V2-PERF-03: optional wall-time and lane-task telemetry.
// Main stage times are wall milliseconds; task elapsed sums may exceed wall
// time when worker lanes operate concurrently. Never sum them as wall time.
struct prepared_include_profile_v2 final {
    double total_ms = 0;
    double workers_start_ms = 0;
    double materialize_wall_ms = 0;
    double acquisition_prepare_ms = 0;
    double acquisition_read_wall_ms = 0;
    double acquisition_apply_ms = 0;
    double lexical_wall_ms = 0;
    double include_scan_ms = 0;
    double frontier_sort_ms = 0;
    double finalize_ms = 0;
    double read_task_elapsed_sum_ms = 0;
    double lexer_task_elapsed_sum_ms = 0;
    double literal_task_elapsed_sum_ms = 0;
    std::size_t frontiers = 0;
    std::size_t peak_frontier_files = 0;
    std::size_t read_tasks = 0;
    std::size_t lexed_files = 0;
    std::size_t ready_files = 0;
    std::size_t include_records = 0;

    // FRONTEND-PHYSICAL-11: profile-only, single-owner include discovery.
    // scan includes candidate_wall; probe/resolve/ensure are nested in candidate.
    // Never sum them as independent wall time.
    double include_stream_walk_wall_ms = 0;
    double include_candidate_wall_ms = 0;
    double include_filesystem_probe_wall_ms = 0;
    double include_file_resolve_wall_ms = 0;
    double include_ensure_file_wall_ms = 0;
    std::size_t include_candidate_calls = 0;
    std::size_t include_filesystem_probe_calls = 0;
    std::size_t include_file_resolve_calls = 0;
    std::size_t include_ensure_file_calls = 0;

    // FRONTEND-PHYSICAL-12: exact counts. Only successful path resolutions
    // enter the ephemeral cache. Missing, invalid and I/O results do not.
    std::size_t include_cache_lookups = 0;
    std::size_t include_cache_misses = 0;
    std::size_t include_positive_cache_hits = 0;
    std::size_t include_positive_cache_entries = 0;
};

struct prepared_include_failure_v2 final {
    file_id file{};
    source_range source;

    prepared_include_state_v2 state =
        prepared_include_state_v2::invalid;
};

class prepared_include_closure_v2 final {
public:
    struct state;

    prepared_include_closure_v2() noexcept;
    ~prepared_include_closure_v2() noexcept;

    prepared_include_closure_v2(
        const prepared_include_closure_v2&) = delete;

    prepared_include_closure_v2& operator=(
        const prepared_include_closure_v2&) = delete;

    [[nodiscard]] prepared_include_view_v2 view() const noexcept;

    [[nodiscard]] std::size_t file_count() const noexcept;

private:
    std::unique_ptr<state> state_value;

    friend server_status prepare_header_include_closure_v2(
        file_context& files,
        std::span<const file_id> roots,
        const preprocessor_configuration& configuration,
        prepared_include_closure_v2& output,
        prepared_include_failure_v2* failure,
        prepared_include_profile_v2* profile) noexcept;
};

// Builds a speculative physical closure. Direct includes are discovered in
// deterministic file/token order; each frontier is read/lexed in parallel.
// Missing/invalid include candidates are recorded, not failed, because semantic
// preprocessing may later prove them inactive.
[[nodiscard]] server_status prepare_header_include_closure_v2(
    file_context& files,
    std::span<const file_id> roots,
    const preprocessor_configuration& configuration,
    prepared_include_closure_v2& output,
    prepared_include_failure_v2* failure = nullptr,
    prepared_include_profile_v2* profile = nullptr) noexcept;

}
