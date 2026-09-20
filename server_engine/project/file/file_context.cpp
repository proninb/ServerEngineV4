#include "file_context.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] std::size_t next_index_capacity(
    std::size_t required) noexcept {

    std::size_t capacity = 16;

    while (capacity / 2 < required) {
        if (capacity >
            (std::numeric_limits<std::size_t>::max)() / 2) {

            return 0;
        }

        capacity *= 2;
    }

    return capacity;
}

void append_u64(
    std::string& output,
    std::uint64_t value) {

    for (std::size_t index = 0;
         index < 8;
         ++index) {

        output.push_back(
            static_cast<char>(
                (value >> (index * 8)) &
                0xffU));
    }
}

[[nodiscard]] std::filesystem::path make_path(
    file_path_view value) {

    return std::filesystem::path{
        value.begin(),
        value.end()};
}

}

std::uint32_t file_context::fingerprint(
    const project_path_key& key) noexcept {

    const auto value =
        static_cast<std::uint64_t>(
            project_path_key_hash{}(key));

    auto output =
        static_cast<std::uint32_t>(value) ^
        static_cast<std::uint32_t>(value >> 32);

    if (output == 0) {
        output = 1;
    }

    return output;
}

server_status file_context::same_key(
    file_id file,
    const project_path_key& key,
    bool& output) const noexcept {

    output = false;

    if (!contains(file)) {
        return server_status::project_configuration_invalid;
    }

    try {
        project_path_key stored_key;

        if (make_project_path_key(
                make_path(path(file)),
                stored_key) !=
            project_path_result::success) {

            return server_status::io_error;
        }

        output =
            stored_key == key;

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status file_context::find_key(
    const project_path_key& key,
    std::uint32_t hash,
    file_id& output) const noexcept {

    output = {};

    if (path_index.empty()) {
        return server_status::success;
    }

    const auto mask =
        path_index.size() - 1;

    auto position =
        static_cast<std::size_t>(hash) &
        mask;

    for (std::size_t probe = 0;
         probe < path_index.size();
         ++probe) {

        const auto& slot =
            path_index[position];

        if (!slot.file) {
            return server_status::success;
        }

        if (slot.fingerprint == hash) {
            bool equal = false;

            const auto compared =
                same_key(
                    slot.file,
                    key,
                    equal);

            if (!succeeded(compared)) {
                return compared;
            }

            if (equal) {
                output = slot.file;
                return server_status::success;
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return server_status::io_error;
}

void file_context::insert_index(
    std::vector<path_slot>& index,
    file_id file,
    std::uint32_t hash) const noexcept {

    const auto mask =
        index.size() - 1;

    auto position =
        static_cast<std::size_t>(hash) &
        mask;

    while (index[position].file) {
        position =
            (position + 1) &
            mask;
    }

    index[position] = {
        hash,
        file,
    };
}

server_status file_context::ensure_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            files.size()) {

        return server_status::io_error;
    }

    const auto required =
        files.size() + additional;

    if (!path_index.empty() &&
        required <=
            path_index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<path_slot> candidate(
            capacity);

        for (std::size_t index = 0;
             index < files.size();
             ++index) {

            const file_id file{
                static_cast<std::uint32_t>(
                    index + 1)};

            insert_index(
                candidate,
                file,
                files[index].path_hash);
        }

        path_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status file_context::resolve(
    const std::filesystem::path& value,
    file_kind requested_kind,
    file_id& output) noexcept {

    output = {};

    std::filesystem::path resolved;

    if (resolve_project_path(
            value,
            resolved) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    project_path_key key;

    if (make_project_path_key(
            resolved,
            key) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    const auto hash =
        fingerprint(key);

    const auto found =
        find_key(
            key,
            hash,
            output);

    if (!succeeded(found)) {
        return found;
    }

    if (output) {
        return kind(output) == requested_kind
            ? server_status::success
            : server_status::project_configuration_invalid;
    }

    if (topology_finalized) {
        return server_status::project_configuration_invalid;
    }

    const auto max_u32 =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    if (files.size() >= max_u32) {
        return server_status::io_error;
    }

    const auto& native =
        resolved.native();

    if (path_chars.size() >= max_u32 ||
        native.size() >
            max_u32 -
                path_chars.size() -
                1) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_index_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto old_path_size =
        path_chars.size();

    try {
        const auto offset =
            static_cast<std::uint32_t>(
                path_chars.size());

        path_chars.insert(
            path_chars.end(),
            native.begin(),
            native.end());

        path_chars.push_back(
            file_path_char{});

        const auto old_file_count =
            files.size();

        files.push_back({
            offset,
            static_cast<std::uint32_t>(
                native.size()),
            hash,
            requested_kind,
            {},
        });

        try {
            physical_files.emplace_back();
            content_files.emplace_back();
            dependency_files.emplace_back();
        }
        catch (...) {
            files.resize(
                old_file_count);
            physical_files.resize(
                old_file_count);
            content_files.resize(
                old_file_count);
            dependency_files.resize(
                old_file_count);
            path_chars.resize(
                old_path_size);
            throw;
        }

        output = file_id{
            static_cast<std::uint32_t>(
                files.size())};

        insert_index(
            path_index,
            output,
            hash);

        return server_status::success;
    }
    catch (...) {
        path_chars.resize(
            old_path_size);

        output = {};
        return server_status::io_error;
    }
}

server_status file_context::find(
    const std::filesystem::path& value,
    file_id& output) const noexcept {

    output = {};

    std::filesystem::path resolved;

    if (resolve_project_path(
            value,
            resolved) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    project_path_key key;

    if (make_project_path_key(
            resolved,
            key) !=
        project_path_result::success) {

        return server_status::io_error;
    }

    return find_key(
        key,
        fingerprint(key),
        output);
}

server_status file_context::prepare_acquire(
    file_id file,
    file_acquire_job& output) const noexcept {

    output = {};

    if (!contains(file)) {
        return server_status::project_configuration_invalid;
    }

    const auto& state =
        physical_files[file.value() - 1];

    output.file = file;
    output.path = path(file);
    output.baseline_present =
        state.present();
    output.baseline_token_available =
        state.has_change_token();

    if (output.baseline_token_available) {
        output.baseline_token =
            state.change_token;
    }

    return server_status::success;
}

void file_context::execute_acquire(
    const file_acquire_job& job,
    file_acquire_result& output) noexcept {

    output = {};
    output.file = job.file;

    try {
        const auto file_path =
            make_path(job.path);

        if (job.baseline_present &&
            job.baseline_token_available) {

            bool unchanged = false;

            const auto proof =
                prove_file_unchanged(
                    file_path,
                    job.baseline_token,
                    unchanged);

            if (proof ==
                    file_token_result::available &&
                unchanged) {

                output.kind =
                    file_acquire_result_kind::unchanged;
                return;
            }

            if (proof ==
                file_token_result::missing) {

                output.kind =
                    file_acquire_result_kind::missing;
                return;
            }

            if (proof ==
                file_token_result::failed) {

                output.kind =
                    file_acquire_result_kind::failed;
                return;
            }
        }

        const auto acquired =
            acquire_file_content(
                file_path,
                output.snapshot);

        switch (acquired) {
        case file_content_result::acquired:
            output.kind =
                file_acquire_result_kind::present;
            return;

        case file_content_result::missing:
            output.kind =
                file_acquire_result_kind::missing;
            return;

        case file_content_result::changed_during_read:
            output.kind =
                file_acquire_result_kind::changed_during_read;
            return;

        case file_content_result::allocation_failed:
            output.kind =
                file_acquire_result_kind::allocation_failed;
            return;

        case file_content_result::failed:
            output.kind =
                file_acquire_result_kind::failed;
            return;
        }
    }
    catch (const std::bad_alloc&) {
        output.kind =
            file_acquire_result_kind::allocation_failed;
    }
    catch (const std::length_error&) {
        output.kind =
            file_acquire_result_kind::allocation_failed;
    }
    catch (...) {
        output.kind =
            file_acquire_result_kind::failed;
    }
}

server_status file_context::apply_acquire(
    const file_acquire_result& result,
    bool& content_changed) noexcept {

    content_changed = false;

    if (!contains(result.file)) {
        return server_status::project_configuration_invalid;
    }

    const auto index =
        static_cast<std::size_t>(
            result.file.value() - 1);

    auto& state =
        physical_files[index];

    auto& content_state =
        content_files[index];

    const auto baseline_present =
        state.present();

    switch (result.kind) {
    case file_acquire_result_kind::unchanged:
        return baseline_present
            ? server_status::success
            : server_status::project_artifact_invalid;

    case file_acquire_result_kind::missing:
        if (content_state.materialized()) {
            // Replacing an already materialized image would leave unreachable
            // bytes in the construction arena. BUILD replacement policy is a
            // separate slice and must not be smuggled into this representation.
            return server_status::unsupported;
        }

        content_changed =
            baseline_present;
        state = {};
        content_state = {};
        return server_status::success;

    case file_acquire_result_kind::present: {
        const auto changed =
            !baseline_present ||
            !(state.content_hash ==
              result.snapshot.content_hash);

        if (content_state.materialized()) {
            if (changed) {
                // Current arena is optimized for one initial materialization per
                // file. Sparse BUILD replacement needs its own no-garbage policy.
                return server_status::unsupported;
            }

            content_changed = false;
            return server_status::success;
        }

        const auto max_u32 =
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)());

        if (content_bytes.size() > max_u32 ||
            result.snapshot.bytes.size() >
                max_u32 - content_bytes.size()) {

            return server_status::io_error;
        }

        const auto old_size =
            content_bytes.size();

        try {
            content_bytes.insert(
                content_bytes.end(),
                result.snapshot.bytes.begin(),
                result.snapshot.bytes.end());
        }
        catch (...) {
            return server_status::io_error;
        }

        content_state.offset =
            static_cast<std::uint32_t>(
                old_size);

        content_state.size =
            static_cast<std::uint32_t>(
                result.snapshot.bytes.size());

        state = {};
        state.content_hash =
            result.snapshot.content_hash;
        state.flags =
            file_physical_present;

        if (result.snapshot.change_token_available &&
            result.snapshot.change_token) {

            state.change_token =
                result.snapshot.change_token;
            state.flags |=
                file_physical_change_token;
        }

        content_changed =
            changed;

        return server_status::success;
    }

    case file_acquire_result_kind::changed_during_read:
    case file_acquire_result_kind::failed:
    case file_acquire_result_kind::allocation_failed:
        return server_status::io_error;
    }

    return server_status::io_error;
}

server_status file_context::calculate_content_hash(
    std::span<const file_id> ordered_files,
    construction_content_hash& output) const noexcept {

    output = {};

    try {
        if (ordered_files.size() >
            ((std::numeric_limits<std::size_t>::max)() - 16) /
                32) {

            return server_status::io_error;
        }

        std::string canonical;
        canonical.reserve(
            16 +
            ordered_files.size() * 32);

        canonical.append(
            "CWFCNT01",
            8);

        append_u64(
            canonical,
            static_cast<std::uint64_t>(
                ordered_files.size()));

        for (const auto file :
             ordered_files) {

            const auto* state =
                physical(file);

            if (state == nullptr ||
                !state->present()) {

                output = {};
                return server_status::
                    project_configuration_invalid;
            }

            canonical.append(
                reinterpret_cast<const char*>(
                    state->content_hash.bytes.data()),
                state->content_hash.bytes.size());
        }

        const auto digest =
            hash_file_content(
                canonical);

        output.bytes =
            digest.bytes;

        return server_status::success;
    }
    catch (...) {
        output = {};
        return server_status::io_error;
    }
}


server_status file_context::add_dependency(
    file_id source,
    file_id target) noexcept {

    if (topology_finalized ||
        !contains(source) ||
        !contains(target)) {

        return server_status::
            project_configuration_invalid;
    }

    try {
        dependency_edges.push_back({
            source,
            target,
        });

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status file_context::finalize_dependency_topology() noexcept {

    if (topology_finalized) {
        return server_status::success;
    }

    const std::span<const file_dependency_edge> edges{
        dependency_edges};

    const auto max_u32 =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    if (dependency_files.size() !=
            files.size() ||
        edges.size() > max_u32) {

        return server_status::io_error;
    }

    try {
        std::vector<std::uint32_t> source_counts(
            files.size());

        for (const auto& edge : edges) {
            if (!contains(edge.source) ||
                !contains(edge.target)) {

                return server_status::
                    project_configuration_invalid;
            }

            auto& count =
                source_counts[
                    edge.source.value() - 1];

            if (count ==
                (std::numeric_limits<std::uint32_t>::max)()) {

                return server_status::io_error;
            }

            ++count;
        }

        std::vector<std::uint32_t> source_offsets(
            files.size());

        std::uint32_t staged_count = 0;

        for (std::size_t index = 0;
             index < source_counts.size();
             ++index) {

            source_offsets[index] =
                staged_count;

            if (source_counts[index] >
                (std::numeric_limits<std::uint32_t>::max)() -
                    staged_count) {

                return server_status::io_error;
            }

            staged_count +=
                source_counts[index];
        }

        if (static_cast<std::size_t>(
                staged_count) !=
            edges.size()) {

            return server_status::io_error;
        }

        std::vector<file_id> staged_targets(
            edges.size());

        std::vector<std::uint32_t> cursor =
            source_offsets;

        for (const auto& edge : edges) {
            staged_targets[
                cursor[
                    edge.source.value() - 1]++] =
                edge.target;
        }

        std::vector<file_dependency_record> records(
            files.size());

        std::vector<std::uint32_t> seen_target(
            files.size());

        for (std::size_t source_index = 0;
             source_index < files.size();
             ++source_index) {

            const auto source_value =
                static_cast<std::uint32_t>(
                    source_index + 1);

            const auto begin =
                source_offsets[source_index];

            const auto count =
                source_counts[source_index];

            for (std::uint32_t index = 0;
                 index < count;
                 ++index) {

                const auto target =
                    staged_targets[
                        begin + index];

                auto& marker =
                    seen_target[
                        target.value() - 1];

                if (marker ==
                    source_value) {

                    continue;
                }

                marker =
                    source_value;

                auto& forward_count =
                    records[source_index]
                        .dependencies.count;

                auto& reverse_count =
                    records[
                        target.value() - 1]
                        .dependents.count;

                if (forward_count ==
                        (std::numeric_limits<std::uint32_t>::max)() ||
                    reverse_count ==
                        (std::numeric_limits<std::uint32_t>::max)()) {

                    return server_status::io_error;
                }

                ++forward_count;
                ++reverse_count;
            }
        }

        std::uint32_t forward_offset = 0;
        std::uint32_t reverse_offset = 0;

        for (auto& record : records) {
            const auto forward_count =
                record.dependencies.count;

            const auto reverse_count =
                record.dependents.count;

            record.dependencies.offset =
                forward_offset;

            record.dependents.offset =
                reverse_offset;

            if (forward_count >
                    (std::numeric_limits<std::uint32_t>::max)() -
                        forward_offset ||
                reverse_count >
                    (std::numeric_limits<std::uint32_t>::max)() -
                        reverse_offset) {

                return server_status::io_error;
            }

            forward_offset +=
                forward_count;

            reverse_offset +=
                reverse_count;
        }

        if (forward_offset !=
            reverse_offset) {

            return server_status::io_error;
        }

        std::vector<file_id> forward(
            forward_offset);

        std::vector<file_id> reverse(
            reverse_offset);

        cursor.resize(
            records.size());

        for (std::size_t index = 0;
             index < records.size();
             ++index) {

            cursor[index] =
                records[index]
                    .dependencies.offset;
        }

        std::fill(
            seen_target.begin(),
            seen_target.end(),
            0);

        for (std::size_t source_index = 0;
             source_index < files.size();
             ++source_index) {

            const auto source =
                file_id{
                    static_cast<std::uint32_t>(
                        source_index + 1)};

            const auto begin =
                source_offsets[source_index];

            const auto count =
                source_counts[source_index];

            for (std::uint32_t index = 0;
                 index < count;
                 ++index) {

                const auto target =
                    staged_targets[
                        begin + index];

                auto& marker =
                    seen_target[
                        target.value() - 1];

                if (marker ==
                    source.value()) {

                    continue;
                }

                marker =
                    source.value();

                forward[
                    cursor[source_index]++] =
                    target;
            }
        }

        for (std::size_t index = 0;
             index < records.size();
             ++index) {

            cursor[index] =
                records[index]
                    .dependents.offset;
        }

        for (std::size_t source_index = 0;
             source_index < files.size();
             ++source_index) {

            const auto source =
                file_id{
                    static_cast<std::uint32_t>(
                        source_index + 1)};

            const auto range =
                records[source_index]
                    .dependencies;

            for (std::uint32_t index = 0;
                 index < range.count;
                 ++index) {

                const auto target =
                    forward[
                        range.offset + index];

                reverse[
                    cursor[
                        target.value() - 1]++] =
                    source;
            }
        }

        dependency_files =
            std::move(records);

        forward_edges =
            std::move(forward);

        reverse_edges =
            std::move(reverse);

        topology_finalized = true;

        std::vector<file_dependency_edge>{}.swap(
            dependency_edges);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

std::span<const file_id> file_context::dependencies(
    file_id file) const noexcept {

    if (!contains(file)) {
        return {};
    }

    const auto range =
        dependency_files[
            file.value() - 1]
            .dependencies;

    if (range.count == 0) {
        return {};
    }

    if (range.offset >
            forward_edges.size() ||
        range.count >
            forward_edges.size() -
                range.offset) {

        return {};
    }

    return {
        forward_edges.data() +
            range.offset,
        range.count,
    };
}

std::span<const file_id> file_context::dependents(
    file_id file) const noexcept {

    if (!contains(file)) {
        return {};
    }

    const auto range =
        dependency_files[
            file.value() - 1]
            .dependents;

    if (range.count == 0) {
        return {};
    }

    if (range.offset >
            reverse_edges.size() ||
        range.count >
            reverse_edges.size() -
                range.offset) {

        return {};
    }

    return {
        reverse_edges.data() +
            range.offset,
        range.count,
    };
}

bool file_context::contains(
    file_id file) const noexcept {

    return file &&
        static_cast<std::size_t>(
            file.value()) <=
            files.size() &&
        physical_files.size() ==
            files.size() &&
        content_files.size() ==
            files.size() &&
        dependency_files.size() ==
            files.size();
}

file_path_view file_context::path(
    file_id file) const noexcept {

    assert(contains(file));

    const auto& record =
        files[file.value() - 1];

    return {
        path_chars.data() +
            record.path_offset,
        record.path_length,
    };
}

file_kind file_context::kind(
    file_id file) const noexcept {

    assert(contains(file));
    return files[file.value() - 1].kind;
}

const file_physical_record* file_context::physical(
    file_id file) const noexcept {

    return contains(file)
        ? &physical_files[
            file.value() - 1]
        : nullptr;
}

bool file_context::content_available(
    file_id file) const noexcept {

    if (!contains(file)) {
        return false;
    }

    const auto index =
        static_cast<std::size_t>(
            file.value() - 1);

    return physical_files[index].present() &&
        content_files[index].materialized();
}

std::string_view file_context::content(
    file_id file) const noexcept {

    assert(content_available(file));

    const auto& record =
        content_files[
            file.value() - 1];

    if (record.offset >
            content_bytes.size() ||
        record.size >
            content_bytes.size() -
                record.offset) {

        return {};
    }

    return {
        content_bytes.data() +
            record.offset,
        record.size,
    };
}

}
