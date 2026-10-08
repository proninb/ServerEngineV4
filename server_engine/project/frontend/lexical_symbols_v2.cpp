#include "lexical_symbols_v2.hpp"

#include "../string/string_table.hpp"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

struct merge_candidate final {
    std::uint32_t hash = 0;
    std::uint32_t length = 0;
    std::uint32_t source = 0;
    std::uint32_t local = 0;
};

static_assert(sizeof(merge_candidate) == 16);

[[nodiscard]] std::string_view candidate_spelling(
    const merge_candidate& candidate,
    std::span<const lexical_symbol_source_v2> sources) noexcept {

    if (candidate.source >= sources.size()) {
        return {};
    }

    const auto& source =
        sources[candidate.source];

    return source.symbols != nullptr
        ? source.symbols->spelling(
            local_symbol_id_v2{
                candidate.local},
            source.source)
        : std::string_view{};
}

}

server_status merge_lexical_symbols_v2(
    std::span<const lexical_symbol_source_v2> sources,
    string_table& strings,
    std::vector<lexical_symbol_resolution_v2>& output) noexcept {

    std::size_t candidate_count = 0;

    for (const auto& source : sources) {
        if (source.symbols == nullptr ||
            !source.symbols->file() ||
            source.symbols->source_size() !=
                source.source.size()) {

            return server_status::
                project_configuration_invalid;
        }

        if (source.symbols->symbol_count() >
            (std::numeric_limits<std::size_t>::max)() -
                candidate_count) {

            return server_status::io_error;
        }

        candidate_count +=
            source.symbols->symbol_count();
    }

    std::vector<merge_candidate>
        candidates;

    std::vector<lexical_symbol_resolution_v2>
        resolutions;

    std::vector<std::uint32_t>
        file_ids;

    try {
        candidates.reserve(
            candidate_count);

        resolutions.resize(
            sources.size());

        file_ids.reserve(
            sources.size());

        for (std::size_t source_index = 0;
             source_index < sources.size();
             ++source_index) {

            if (source_index >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)())) {

                return server_status::io_error;
            }

            const auto& source =
                sources[source_index];

            file_ids.push_back(
                source.symbols->file().value());

            const auto reset =
                resolutions[source_index].reset(
                    source.symbols->file(),
                    source.symbols->symbol_count());

            if (!succeeded(reset)) {
                return reset;
            }

            const auto records =
                source.symbols->symbols();

            for (std::size_t local = 0;
                 local < records.size();
                 ++local) {

                if (local >=
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)())) {

                    return server_status::io_error;
                }

                const auto& record =
                    records[local];

                const auto spelling =
                    source.symbols->spelling(
                        local_symbol_id_v2{
                            static_cast<std::uint32_t>(
                                local + 1)},
                        source.source);

                if (spelling.empty() ||
                    spelling.size() !=
                        record.source_length) {

                    return server_status::
                        project_configuration_invalid;
                }

                candidates.push_back({
                    record.hash,
                    record.source_length,
                    static_cast<std::uint32_t>(
                        source_index),
                    static_cast<std::uint32_t>(
                        local + 1),
                });
            }
        }
    }
    catch (...) {
        return server_status::io_error;
    }

    std::sort(
        file_ids.begin(),
        file_ids.end());

    for (std::size_t index = 1;
         index < file_ids.size();
         ++index) {

        if (file_ids[index - 1] ==
            file_ids[index]) {

            return server_status::
                project_configuration_invalid;
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [&sources](
            const merge_candidate& left,
            const merge_candidate& right) noexcept {

            if (left.hash != right.hash) {
                return left.hash < right.hash;
            }

            if (left.length != right.length) {
                return left.length < right.length;
            }

            const auto left_spelling =
                candidate_spelling(
                    left,
                    sources);

            const auto right_spelling =
                candidate_spelling(
                    right,
                    sources);

            const auto order =
                left_spelling.compare(
                    right_spelling);

            if (order != 0) {
                return order < 0;
            }

            const auto left_file =
                sources[left.source].
                    symbols->file().value();

            const auto right_file =
                sources[right.source].
                    symbols->file().value();

            return left_file != right_file
                ? left_file < right_file
                : left.local < right.local;
        });

    bool have_previous = false;
    std::uint32_t previous_hash = 0;
    std::string_view previous_spelling;
    string_id current_global;

    for (const auto& candidate :
         candidates) {

        const auto spelling =
            candidate_spelling(
                candidate,
                sources);

        if (spelling.empty()) {
            return server_status::
                project_configuration_invalid;
        }

        const auto same =
            have_previous &&
            previous_hash ==
                candidate.hash &&
            previous_spelling ==
                spelling;

        if (!same) {
            const auto interned =
                strings.intern(
                    spelling,
                    current_global);

            if (!succeeded(interned) ||
                !current_global) {

                return succeeded(interned)
                    ? server_status::
                        project_configuration_invalid
                    : interned;
            }

            have_previous = true;
            previous_hash =
                candidate.hash;
            previous_spelling =
                spelling;
        }

        const auto resolved =
            resolutions[
                candidate.source].set(
                    local_symbol_id_v2{
                        candidate.local},
                    current_global);

        if (!succeeded(resolved)) {
            return resolved;
        }
    }

    for (const auto& resolution :
         resolutions) {

        if (!resolution.complete()) {
            return server_status::
                project_configuration_invalid;
        }
    }

    output =
        std::move(resolutions);

    return server_status::success;
}

}
