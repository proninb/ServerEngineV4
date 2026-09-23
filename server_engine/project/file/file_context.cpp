#include "file_context.hpp"

#include "../persistence/source_save.hpp"

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

file_dependency_view file_dependency_view::from_native(
    std::span<const file_id> values) noexcept {

    file_dependency_view output;
    output.native = values;
    output.count = values.size();
    return output;
}

file_dependency_view file_dependency_view::from_encoded(
    std::span<const std::byte> values) noexcept {

    if (values.size() %
        sizeof(std::uint32_t) != 0) {

        return {};
    }

    file_dependency_view output;
    output.encoded = values;
    output.count =
        values.size() /
        sizeof(std::uint32_t);

    return output;
}

file_id file_dependency_view::operator[](
    std::size_t index) const noexcept {

    if (index >= count) {
        return {};
    }

    if (!native.empty()) {
        return native[index];
    }

    const auto offset =
        index *
        sizeof(std::uint32_t);

    if (offset > encoded.size() ||
        encoded.size() - offset <
            sizeof(std::uint32_t)) {

        return {};
    }

    std::uint32_t value = 0;

    for (std::size_t byte = 0;
         byte < sizeof(std::uint32_t);
         ++byte) {

        value |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    encoded[offset + byte]))
            << (byte * 8);
    }

    return file_id{value};
}

server_status file_context::bind_baseline(
    const source_save_view& source) noexcept {

    if (!source.valid() ||
        source.file_count() == 0 ||
        baseline != nullptr ||
        !files.empty() ||
        !path_chars.empty() ||
        !path_index.empty() ||
        !physical_files.empty() ||
        !content_files.empty() ||
        !content_bytes.empty() ||
        !dependency_files.empty() ||
        !forward_edges.empty() ||
        !reverse_edges.empty() ||
        !dependency_edges.empty() ||
        topology_finalized ||
        !baseline_overlays.empty() ||
        !baseline_overlay_index.empty() ||
        !baseline_path_chars.empty()) {

        return server_status::
            project_artifact_invalid;
    }

    baseline = &source;
    baseline_file_count =
        source.file_count();

    return server_status::success;
}

bool file_context::local_index(
    file_id file,
    std::size_t& output) const noexcept {

    output = 0;

    if (!file ||
        file.value() <=
            baseline_file_count) {

        return false;
    }

    const auto index =
        static_cast<std::size_t>(
            file.value() -
            baseline_file_count -
            1);

    if (index >= files.size() ||
        physical_files.size() != files.size() ||
        content_files.size() != files.size() ||
        dependency_files.size() != files.size()) {

        return false;
    }

    output = index;
    return true;
}

bool file_context::find_baseline_overlay(
    file_id file,
    std::size_t& output) const noexcept {

    output = 0;

    if (baseline == nullptr ||
        !file ||
        file.value() >
            baseline_file_count ||
        baseline_overlay_index.empty()) {

        return false;
    }

    const auto mask =
        baseline_overlay_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            file.value() *
                2654435761u) &
        mask;

    for (std::size_t probe = 0;
         probe <
            baseline_overlay_index.size();
         ++probe) {

        const auto& slot =
            baseline_overlay_index[
                position];

        if (!slot.file) {
            return false;
        }

        if (slot.file == file) {
            if (slot.record == 0 ||
                slot.record >
                    baseline_overlays.size()) {

                return false;
            }

            output =
                static_cast<std::size_t>(
                    slot.record - 1);

            return baseline_overlays[
                    output].file ==
                file;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

void file_context::insert_baseline_overlay(
    std::vector<baseline_overlay_slot>& index,
    file_id file,
    std::uint32_t record) const noexcept {

    const auto mask =
        index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            file.value() *
                2654435761u) &
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

server_status file_context::ensure_baseline_overlay_capacity(
    std::size_t additional) const noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            baseline_overlays.size()) {

        return server_status::io_error;
    }

    const auto required =
        baseline_overlays.size() +
        additional;

    if (!baseline_overlay_index.empty() &&
        required <=
            baseline_overlay_index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(
            required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<baseline_overlay_slot>
            candidate(capacity);

        for (std::size_t index = 0;
             index <
                baseline_overlays.size();
             ++index) {

            insert_baseline_overlay(
                candidate,
                baseline_overlays[index].file,
                static_cast<std::uint32_t>(
                    index + 1));
        }

        baseline_overlay_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status file_context::ensure_baseline_overlay(
    file_id file,
    std::size_t& output) const noexcept {

    output = 0;

    if (baseline == nullptr ||
        !file ||
        file.value() >
            baseline_file_count ||
        !baseline->contains(file)) {

        return server_status::
            project_configuration_invalid;
    }

    if (find_baseline_overlay(
            file,
            output)) {

        return server_status::success;
    }

    source_save_file_view state;

    if (!baseline->file(
            file,
            state)) {

        return server_status::
            project_artifact_invalid;
    }

    std::filesystem::path decoded;

    if (filesystem_path_from_utf8(
            state.path_utf8,
            decoded) !=
        filesystem_path_result::success) {

        return server_status::
            project_artifact_invalid;
    }

    const auto& native =
        decoded.native();

    const auto max_u32 =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    if (baseline_path_chars.size() >
            max_u32 ||
        native.size() >
            max_u32 -
                baseline_path_chars.size() -
                1 ||
        baseline_overlays.size() >=
            max_u32) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_baseline_overlay_capacity(
            1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto old_path_size =
        baseline_path_chars.size();

    try {
        const auto offset =
            static_cast<std::uint32_t>(
                old_path_size);

        baseline_path_chars.insert(
            baseline_path_chars.end(),
            native.begin(),
            native.end());

        baseline_path_chars.push_back(
            file_path_char{});

        baseline_overlay_record overlay;
        overlay.file = file;
        overlay.path_offset = offset;
        overlay.path_length =
            static_cast<std::uint32_t>(
                native.size());
        overlay.physical =
            state.physical;

        baseline_overlays.push_back(
            overlay);

        output =
            baseline_overlays.size() - 1;

        insert_baseline_overlay(
            baseline_overlay_index,
            file,
            static_cast<std::uint32_t>(
                output + 1));

        return server_status::success;
    }
    catch (...) {
        baseline_path_chars.resize(
            old_path_size);

        return server_status::io_error;
    }
}

std::uint32_t file_context::fingerprint(
    const filesystem_path_key& key) noexcept {

    const auto value =
        static_cast<std::uint64_t>(
            filesystem_path_key_hash{}(key));

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
    const filesystem_path_key& key,
    bool& output) const noexcept {

    output = false;

    if (!contains(file)) {
        return server_status::project_configuration_invalid;
    }

    try {
        filesystem_path_key stored_key;

        if (make_filesystem_path_key(
                make_path(path(file)),
                stored_key) !=
            filesystem_path_result::success) {

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
    const filesystem_path_key& key,
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
                    baseline_file_count +
                    index +
                    1)};

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

    filesystem_path_key key;

    if (make_filesystem_path_key(
            resolved,
            key) !=
        filesystem_path_result::success) {

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

    if (!output &&
        baseline != nullptr) {

        const auto baseline_found =
            baseline->find_path(
                resolved,
                output);

        if (!succeeded(
                baseline_found)) {

            return baseline_found;
        }
    }

    if (output) {
        return kind(output) == requested_kind
            ? server_status::success
            : server_status::
                project_configuration_invalid;
    }

    if (topology_finalized) {
        return server_status::
            project_configuration_invalid;
    }

    const auto max_u32 =
        static_cast<std::size_t>(
            (std::numeric_limits<
                std::uint32_t>::max)());

    if (size() >= max_u32) {
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
                baseline_file_count +
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

    filesystem_path_key key;

    if (make_filesystem_path_key(
            resolved,
            key) !=
        filesystem_path_result::success) {

        return server_status::io_error;
    }

    const auto local_found =
        find_key(
            key,
            fingerprint(key),
            output);

    if (!succeeded(local_found) ||
        output ||
        baseline == nullptr) {

        return local_found;
    }

    return baseline->find_path(
        resolved,
        output);
}

server_status file_context::prepare_acquire(
    file_id file,
    file_acquire_job& output) const noexcept {

    output = {};

    if (!contains(file)) {
        return server_status::
            project_configuration_invalid;
    }

    const file_physical_record* state = nullptr;

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        std::size_t overlay = 0;

        const auto prepared =
            ensure_baseline_overlay(
                file,
                overlay);

        if (!succeeded(prepared)) {
            return prepared;
        }

        state =
            &baseline_overlays[
                overlay].physical;
    }
    else {
        std::size_t index = 0;

        if (!local_index(
                file,
                index)) {

            return server_status::
                project_configuration_invalid;
        }

        state =
            &physical_files[index];
    }

    output.file = file;
    output.path = path(file);
    output.baseline_present =
        state->present();
    output.baseline_token_available =
        state->has_change_token();

    if (output.path.empty()) {
        return server_status::io_error;
    }

    if (output.baseline_token_available) {
        output.baseline_token =
            state->change_token;
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
        return server_status::
            project_configuration_invalid;
    }

    file_physical_record* state = nullptr;
    file_content_record* content_state = nullptr;

    if (baseline != nullptr &&
        result.file.value() <=
            baseline_file_count) {

        std::size_t overlay = 0;

        const auto prepared =
            ensure_baseline_overlay(
                result.file,
                overlay);

        if (!succeeded(prepared)) {
            return prepared;
        }

        state =
            &baseline_overlays[
                overlay].physical;

        content_state =
            &baseline_overlays[
                overlay].content;
    }
    else {
        std::size_t index = 0;

        if (!local_index(
                result.file,
                index)) {

            return server_status::
                project_configuration_invalid;
        }

        state =
            &physical_files[index];

        content_state =
            &content_files[index];
    }

    const auto baseline_present =
        state->present();

    switch (result.kind) {
    case file_acquire_result_kind::unchanged:
        return baseline_present
            ? server_status::success
            : server_status::
                project_artifact_invalid;

    case file_acquire_result_kind::missing:
        if (content_state->materialized()) {
            return server_status::unsupported;
        }

        content_changed =
            baseline_present;

        *state = {};
        *content_state = {};
        return server_status::success;

    case file_acquire_result_kind::present: {
        const auto changed =
            !baseline_present ||
            !(state->content_hash ==
              result.snapshot.content_hash);

        if (content_state->materialized()) {
            if (changed) {
                return server_status::unsupported;
            }

            content_changed = false;
            return server_status::success;
        }

        const auto max_u32 =
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)());

        if (content_bytes.size() >
                max_u32 ||
            result.snapshot.bytes.size() >
                max_u32 -
                    content_bytes.size()) {

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

        content_state->offset =
            static_cast<std::uint32_t>(
                old_size);

        content_state->size =
            static_cast<std::uint32_t>(
                result.snapshot.bytes.size());

        *state = {};
        state->content_hash =
            result.snapshot.content_hash;
        state->flags =
            file_physical_present;

        if (result.snapshot.change_token_available &&
            result.snapshot.change_token) {

            state->change_token =
                result.snapshot.change_token;
            state->flags |=
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

    if (baseline != nullptr) {
        return server_status::unsupported;
    }

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

    if (baseline != nullptr) {
        return server_status::unsupported;
    }

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

file_dependency_view file_context::dependencies(
    file_id file) const noexcept {

    if (!contains(file)) {
        return {};
    }

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        source_save_file_view state;

        return baseline->file(
                file,
                state)
            ? state.dependencies
            : file_dependency_view{};
    }

    std::size_t index = 0;

    if (!local_index(
            file,
            index)) {

        return {};
    }

    const auto range =
        dependency_files[
            index].dependencies;

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

    return file_dependency_view::from_native(
        std::span<const file_id>{
            forward_edges.data() +
                range.offset,
            range.count});
}

file_dependency_view file_context::dependents(
    file_id file) const noexcept {

    if (!contains(file)) {
        return {};
    }

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        source_save_file_view state;

        return baseline->file(
                file,
                state)
            ? state.dependents
            : file_dependency_view{};
    }

    std::size_t index = 0;

    if (!local_index(
            file,
            index)) {

        return {};
    }

    const auto range =
        dependency_files[
            index].dependents;

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

    return file_dependency_view::from_native(
        std::span<const file_id>{
            reverse_edges.data() +
                range.offset,
            range.count});
}

bool file_context::contains(
    file_id file) const noexcept {

    if (!file) {
        return false;
    }

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        return baseline->contains(
            file);
    }

    std::size_t index = 0;

    return local_index(
        file,
        index);
}

file_path_view file_context::path(
    file_id file) const noexcept {

    assert(contains(file));

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        std::size_t overlay = 0;

        if (!succeeded(
                ensure_baseline_overlay(
                    file,
                    overlay))) {

            return {};
        }

        const auto& record =
            baseline_overlays[
                overlay];

        if (record.path_offset >
                baseline_path_chars.size() ||
            record.path_length >
                baseline_path_chars.size() -
                    record.path_offset) {

            return {};
        }

        return {
            baseline_path_chars.data() +
                record.path_offset,
            record.path_length,
        };
    }

    std::size_t index = 0;

    if (!local_index(
            file,
            index)) {

        return {};
    }

    const auto& record =
        files[index];

    return {
        path_chars.data() +
            record.path_offset,
        record.path_length,
    };
}

file_kind file_context::kind(
    file_id file) const noexcept {

    assert(contains(file));

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        source_save_file_view state;

        return baseline->file(
                file,
                state)
            ? state.kind
            : file_kind::project;
    }

    std::size_t index = 0;

    return local_index(
            file,
            index)
        ? files[index].kind
        : file_kind::project;
}

const file_physical_record* file_context::physical(
    file_id file) const noexcept {

    if (!contains(file)) {
        return nullptr;
    }

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        std::size_t overlay = 0;

        if (!succeeded(
                ensure_baseline_overlay(
                    file,
                    overlay))) {

            return nullptr;
        }

        return &baseline_overlays[
            overlay].physical;
    }

    std::size_t index = 0;

    return local_index(
            file,
            index)
        ? &physical_files[index]
        : nullptr;
}

bool file_context::content_available(
    file_id file) const noexcept {

    if (!contains(file)) {
        return false;
    }

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        std::size_t overlay = 0;

        return find_baseline_overlay(
                file,
                overlay) &&
            baseline_overlays[
                overlay].physical.present() &&
            baseline_overlays[
                overlay].content.materialized();
    }

    std::size_t index = 0;

    return local_index(
            file,
            index) &&
        physical_files[index].present() &&
        content_files[index].materialized();
}

std::string_view file_context::content(
    file_id file) const noexcept {

    assert(content_available(file));

    const file_content_record* record = nullptr;

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        std::size_t overlay = 0;

        if (!find_baseline_overlay(
                file,
                overlay)) {

            return {};
        }

        record =
            &baseline_overlays[
                overlay].content;
    }
    else {
        std::size_t index = 0;

        if (!local_index(
                file,
                index)) {

            return {};
        }

        record =
            &content_files[index];
    }

    if (record->offset >
            content_bytes.size() ||
        record->size >
            content_bytes.size() -
                record->offset) {

        return {};
    }

    return {
        content_bytes.data() +
            record->offset,
        record->size,
    };
}

}
