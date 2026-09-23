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

[[nodiscard]] std::uint64_t topology_edge_key(
    file_id owner,
    file_id value) noexcept {

    return
        (static_cast<std::uint64_t>(
            owner.value()) << 32) |
        static_cast<std::uint64_t>(
            value.value());
}

[[nodiscard]] std::size_t topology_hash(
    std::uint64_t value) noexcept {

    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;

    return static_cast<std::size_t>(
        value);
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
    output.base_count = values.size();
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
    output.base_count =
        values.size() /
        sizeof(std::uint32_t);
    output.count =
        output.base_count;

    return output;
}

file_dependency_view file_dependency_view::from_overlay(
    const file_dependency_view& base,
    std::span<const file_id> added_values,
    std::size_t final_count,
    const void* context,
    file_id owner,
    filter_function filter_value) noexcept {

    if (!base.added.empty() ||
        base.filter != nullptr ||
        final_count >
            base.base_count +
                added_values.size()) {

        return {};
    }

    file_dependency_view output;
    output.native = base.native;
    output.encoded = base.encoded;
    output.added = added_values;
    output.base_count =
        base.base_count;
    output.count =
        final_count;
    output.filter_context =
        context;
    output.filter_owner =
        owner;
    output.filter =
        filter_value;

    return output;
}

file_id file_dependency_view::base_value(
    std::size_t index) const noexcept {

    if (index >= base_count) {
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

bool file_dependency_view::filtered(
    file_id value) const noexcept {

    return filter != nullptr &&
        filter(
            filter_context,
            filter_owner,
            value);
}

void file_dependency_view::seek(
    iterator& value) const noexcept {

    while (value.base_index <
            base_count) {

        const auto current =
            base_value(
                value.base_index);

        if (!current ||
            filtered(current)) {

            ++value.base_index;
            continue;
        }

        value.additions = false;
        return;
    }

    value.additions = true;
    value.addition_index = 0;
}

file_dependency_view::iterator::iterator(
    const file_dependency_view* value,
    bool end) noexcept
    : owner(value) {

    if (owner == nullptr) {
        additions = true;
        return;
    }

    if (end) {
        base_index =
            owner->base_count;
        addition_index =
            owner->added.size();
        additions = true;
        return;
    }

    owner->seek(*this);
}

file_id file_dependency_view::iterator::operator*() const noexcept {

    if (owner == nullptr) {
        return {};
    }

    return additions
        ? addition_index <
                owner->added.size()
            ? owner->added[
                addition_index]
            : file_id{}
        : owner->base_value(
            base_index);
}

file_dependency_view::iterator&
file_dependency_view::iterator::operator++() noexcept {

    if (owner == nullptr) {
        return *this;
    }

    if (additions) {
        if (addition_index <
            owner->added.size()) {

            ++addition_index;
        }

        return *this;
    }

    ++base_index;
    owner->seek(*this);
    return *this;
}

file_id file_dependency_view::operator[](
    std::size_t index) const noexcept {

    if (index >= count) {
        return {};
    }

    auto current =
        begin();

    const auto last =
        end();

    for (std::size_t position = 0;
         current != last;
         ++current, ++position) {

        if (position == index) {
            return *current;
        }
    }

    return {};
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
        !topology_sources.empty() ||
        !topology_source_index.empty() ||
        !topology_forward_edges.empty() ||
        !topology_targets.empty() ||
        !topology_target_index.empty() ||
        !topology_reverse_additions.empty() ||
        !topology_reverse_removals.empty() ||
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

server_status file_context::bind_content_baseline(
    file_content_baseline_view source) noexcept {

    if (baseline == nullptr ||
        baseline_file_count == 0 ||
        content_baseline.valid() ||
        !source.valid() ||
        source.file_count() !=
            baseline_file_count) {

        return server_status::
            project_artifact_invalid;
    }

    content_baseline = source;
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

bool file_context::find_topology_source(
    file_id source,
    std::size_t& output) const noexcept {

    output = 0;

    if (!source ||
        topology_source_index.empty()) {

        return false;
    }

    const auto mask =
        topology_source_index.size() - 1;

    auto position =
        topology_hash(
            source.value()) &
        mask;

    for (std::size_t probe = 0;
         probe <
            topology_source_index.size();
         ++probe) {

        const auto& slot =
            topology_source_index[
                position];

        if (!slot.source) {
            return false;
        }

        if (slot.source == source) {
            if (slot.record == 0 ||
                slot.record >
                    topology_sources.size()) {

                return false;
            }

            output =
                static_cast<std::size_t>(
                    slot.record - 1);

            return topology_sources[
                    output].source ==
                source;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

void file_context::insert_topology_source(
    std::vector<topology_source_slot>& index,
    file_id source,
    std::uint32_t record) const noexcept {

    const auto mask =
        index.size() - 1;

    auto position =
        topology_hash(
            source.value()) &
        mask;

    while (index[position].source) {
        position =
            (position + 1) &
            mask;
    }

    index[position] = {
        source,
        record,
    };
}

server_status file_context::ensure_topology_source_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            topology_sources.size()) {

        return server_status::io_error;
    }

    const auto required =
        topology_sources.size() +
        additional;

    if (!topology_source_index.empty() &&
        required <=
            topology_source_index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(
            required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<topology_source_slot>
            candidate(capacity);

        for (std::size_t index = 0;
             index <
                topology_sources.size();
             ++index) {

            insert_topology_source(
                candidate,
                topology_sources[index].source,
                static_cast<std::uint32_t>(
                    index + 1));
        }

        topology_source_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

bool file_context::find_topology_target(
    file_id target,
    std::size_t& output) const noexcept {

    output = 0;

    if (!target ||
        topology_target_index.empty()) {

        return false;
    }

    const auto mask =
        topology_target_index.size() - 1;

    auto position =
        topology_hash(
            target.value()) &
        mask;

    for (std::size_t probe = 0;
         probe <
            topology_target_index.size();
         ++probe) {

        const auto& slot =
            topology_target_index[
                position];

        if (!slot.target) {
            return false;
        }

        if (slot.target == target) {
            if (slot.record == 0 ||
                slot.record >
                    topology_targets.size()) {

                return false;
            }

            output =
                static_cast<std::size_t>(
                    slot.record - 1);

            return topology_targets[
                    output].target ==
                target;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

bool file_context::reverse_dependency_removed(
    file_id target,
    file_id source) const noexcept {

    if (!target ||
        !source ||
        topology_reverse_removals.empty()) {

        return false;
    }

    const auto key =
        topology_edge_key(
            target,
            source);

    const auto mask =
        topology_reverse_removals.size() - 1;

    auto position =
        topology_hash(
            key) &
        mask;

    for (std::size_t probe = 0;
         probe <
            topology_reverse_removals.size();
         ++probe) {

        const auto stored =
            topology_reverse_removals[
                position];

        if (stored == 0) {
            return false;
        }

        if (stored == key) {
            return true;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

bool file_context::reverse_dependency_filter(
    const void* context,
    file_id target,
    file_id source) noexcept {

    return context != nullptr &&
        static_cast<const file_context*>(
            context)
            ->reverse_dependency_removed(
                target,
                source);
}

server_status file_context::begin_dependency_replacement(
    file_id source) noexcept {

    if (baseline == nullptr ||
        topology_finalized ||
        !contains(source)) {

        return server_status::
            project_configuration_invalid;
    }

    std::size_t existing = 0;

    if (find_topology_source(
            source,
            existing)) {

        return server_status::success;
    }

    if (topology_sources.size() >=
        static_cast<std::size_t>(
            (std::numeric_limits<
                std::uint32_t>::max)())) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_topology_source_capacity(
            1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    try {
        topology_source_record record;
        record.source = source;

        topology_sources.push_back(
            record);

        insert_topology_source(
            topology_source_index,
            source,
            static_cast<std::uint32_t>(
                topology_sources.size()));

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status file_context::finalize_baseline_dependency_topology() noexcept {

    if (baseline == nullptr) {
        return server_status::
            project_configuration_invalid;
    }

    if (topology_finalized) {
        return server_status::success;
    }

    struct edge_slot final {
        std::uint64_t key = 0;
    };

    struct delta_event final {
        file_id target{};
        file_id source{};
        bool addition = false;
    };

    auto contains_edge =
        [](
            const std::vector<edge_slot>& index,
            std::uint64_t key) noexcept {

            if (index.empty()) {
                return false;
            }

            const auto mask =
                index.size() - 1;

            auto position =
                topology_hash(
                    key) &
                mask;

            for (std::size_t probe = 0;
                 probe < index.size();
                 ++probe) {

                const auto stored =
                    index[position].key;

                if (stored == 0) {
                    return false;
                }

                if (stored == key) {
                    return true;
                }

                position =
                    (position + 1) &
                    mask;
            }

            return false;
        };

    auto insert_edge =
        [](
            std::vector<edge_slot>& index,
            std::uint64_t key) noexcept {

            const auto mask =
                index.size() - 1;

            auto position =
                topology_hash(
                    key) &
                mask;

            while (index[position].key != 0) {
                if (index[position].key ==
                    key) {

                    return false;
                }

                position =
                    (position + 1) &
                    mask;
            }

            index[position].key =
                key;

            return true;
        };

    try {
        auto candidate_sources =
            topology_sources;

        for (auto& record :
             candidate_sources) {

            record.dependencies = {};
        }

        std::vector<edge_slot> new_index;

        if (!dependency_edges.empty()) {
            const auto capacity =
                next_index_capacity(
                    dependency_edges.size());

            if (capacity == 0) {
                return server_status::io_error;
            }

            new_index.resize(
                capacity);
        }

        std::vector<file_dependency_edge>
            unique_edges;

        unique_edges.reserve(
            dependency_edges.size());

        for (const auto& edge :
             dependency_edges) {

            if (!contains(edge.source) ||
                !contains(edge.target)) {

                return server_status::
                    project_configuration_invalid;
            }

            std::size_t source_index = 0;

            if (!find_topology_source(
                    edge.source,
                    source_index)) {

                return server_status::
                    project_configuration_invalid;
            }

            const auto key =
                topology_edge_key(
                    edge.source,
                    edge.target);

            if (!insert_edge(
                    new_index,
                    key)) {

                continue;
            }

            auto& count =
                candidate_sources[
                    source_index]
                    .dependencies.count;

            if (count ==
                (std::numeric_limits<
                    std::uint32_t>::max)()) {

                return server_status::io_error;
            }

            ++count;

            unique_edges.push_back(
                edge);
        }

        std::uint32_t forward_count = 0;

        for (auto& record :
             candidate_sources) {

            record.dependencies.offset =
                forward_count;

            if (record.dependencies.count >
                (std::numeric_limits<
                    std::uint32_t>::max)() -
                    forward_count) {

                return server_status::io_error;
            }

            forward_count +=
                record.dependencies.count;
        }

        if (static_cast<std::size_t>(
                forward_count) !=
            unique_edges.size()) {

            return server_status::io_error;
        }

        std::vector<file_id>
            candidate_forward(
                forward_count);

        std::vector<std::uint32_t>
            source_cursor(
                candidate_sources.size());

        for (std::size_t index = 0;
             index <
                candidate_sources.size();
             ++index) {

            source_cursor[index] =
                candidate_sources[
                    index]
                    .dependencies.offset;
        }

        for (const auto& edge :
             unique_edges) {

            std::size_t source_index = 0;

            if (!find_topology_source(
                    edge.source,
                    source_index)) {

                return server_status::io_error;
            }

            candidate_forward[
                source_cursor[
                    source_index]++] =
                edge.target;
        }

        std::size_t old_edge_count = 0;

        for (const auto& record :
             candidate_sources) {

            if (record.source.value() >
                baseline_file_count) {

                continue;
            }

            source_save_file_view state;

            if (!baseline->file(
                    record.source,
                    state)) {

                return server_status::
                    project_artifact_invalid;
            }

            if (state.dependencies.size() >
                (std::numeric_limits<
                    std::size_t>::max)() -
                    old_edge_count) {

                return server_status::io_error;
            }

            old_edge_count +=
                state.dependencies.size();
        }

        std::vector<edge_slot> old_index;

        if (old_edge_count != 0) {
            const auto capacity =
                next_index_capacity(
                    old_edge_count);

            if (capacity == 0) {
                return server_status::io_error;
            }

            old_index.resize(
                capacity);
        }

        std::vector<delta_event>
            delta_events;

        if (old_edge_count >
            (std::numeric_limits<
                std::size_t>::max)() -
                unique_edges.size()) {

            return server_status::io_error;
        }

        delta_events.reserve(
            old_edge_count +
            unique_edges.size());

        for (const auto& record :
             candidate_sources) {

            if (record.source.value() >
                baseline_file_count) {

                continue;
            }

            source_save_file_view state;

            if (!baseline->file(
                    record.source,
                    state)) {

                return server_status::
                    project_artifact_invalid;
            }

            for (const auto target :
                 state.dependencies) {

                if (!target ||
                    !contains(target)) {

                    return server_status::
                        project_artifact_invalid;
                }

                const auto key =
                    topology_edge_key(
                        record.source,
                        target);

                if (!insert_edge(
                        old_index,
                        key)) {

                    return server_status::
                        project_artifact_invalid;
                }

                if (!contains_edge(
                        new_index,
                        key)) {

                    delta_events.push_back({
                        target,
                        record.source,
                        false,
                    });
                }
            }
        }

        for (const auto& edge :
             unique_edges) {

            const auto key =
                topology_edge_key(
                    edge.source,
                    edge.target);

            if (!contains_edge(
                    old_index,
                    key)) {

                delta_events.push_back({
                    edge.target,
                    edge.source,
                    true,
                });
            }
        }

        std::vector<topology_target_record>
            candidate_targets;

        std::vector<topology_target_slot>
            candidate_target_index;

        if (!delta_events.empty()) {
            const auto capacity =
                next_index_capacity(
                    delta_events.size());

            if (capacity == 0) {
                return server_status::io_error;
            }

            candidate_target_index.resize(
                capacity);
        }

        auto find_candidate_target =
            [&](
                file_id target,
                std::size_t& output) noexcept {

                output = 0;

                if (candidate_target_index.empty()) {
                    return false;
                }

                const auto mask =
                    candidate_target_index.size() - 1;

                auto position =
                    topology_hash(
                        target.value()) &
                    mask;

                for (std::size_t probe = 0;
                     probe <
                        candidate_target_index.size();
                     ++probe) {

                    const auto& slot =
                        candidate_target_index[
                            position];

                    if (!slot.target) {
                        return false;
                    }

                    if (slot.target == target) {
                        if (slot.record == 0 ||
                            slot.record >
                                candidate_targets.size()) {

                            return false;
                        }

                        output =
                            static_cast<std::size_t>(
                                slot.record - 1);

                        return true;
                    }

                    position =
                        (position + 1) &
                        mask;
                }

                return false;
            };

        auto insert_candidate_target =
            [&](
                file_id target,
                std::uint32_t record) noexcept {

                const auto mask =
                    candidate_target_index.size() - 1;

                auto position =
                    topology_hash(
                        target.value()) &
                    mask;

                while (
                    candidate_target_index[
                        position].target) {

                    position =
                        (position + 1) &
                        mask;
                }

                candidate_target_index[
                    position] = {
                        target,
                        record,
                    };
            };

        for (const auto& event :
             delta_events) {

            std::size_t target_index = 0;

            if (!find_candidate_target(
                    event.target,
                    target_index)) {

                if (candidate_targets.size() >=
                    static_cast<std::size_t>(
                        (std::numeric_limits<
                            std::uint32_t>::max)())) {

                    return server_status::io_error;
                }

                topology_target_record record;
                record.target =
                    event.target;

                candidate_targets.push_back(
                    record);

                target_index =
                    candidate_targets.size() - 1;

                insert_candidate_target(
                    event.target,
                    static_cast<std::uint32_t>(
                        target_index + 1));
            }

            auto& record =
                candidate_targets[
                    target_index];

            if (event.addition) {
                if (record.additions.count ==
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {

                    return server_status::io_error;
                }

                ++record.additions.count;
            }
            else {
                if (record.removals ==
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {

                    return server_status::io_error;
                }

                ++record.removals;
            }
        }

        std::uint32_t addition_count = 0;
        std::size_t removal_count = 0;

        for (auto& record :
             candidate_targets) {

            record.additions.offset =
                addition_count;

            if (record.additions.count >
                (std::numeric_limits<
                    std::uint32_t>::max)() -
                    addition_count) {

                return server_status::io_error;
            }

            addition_count +=
                record.additions.count;

            if (record.removals >
                (std::numeric_limits<
                    std::size_t>::max)() -
                    removal_count) {

                return server_status::io_error;
            }

            removal_count +=
                record.removals;
        }

        std::vector<file_id>
            candidate_additions(
                addition_count);

        std::vector<std::uint32_t>
            target_cursor(
                candidate_targets.size());

        for (std::size_t index = 0;
             index <
                candidate_targets.size();
             ++index) {

            target_cursor[index] =
                candidate_targets[
                    index]
                    .additions.offset;
        }

        std::vector<std::uint64_t>
            candidate_removals;

        if (removal_count != 0) {
            const auto capacity =
                next_index_capacity(
                    removal_count);

            if (capacity == 0) {
                return server_status::io_error;
            }

            candidate_removals.resize(
                capacity);
        }

        for (const auto& event :
             delta_events) {

            std::size_t target_index = 0;

            if (!find_candidate_target(
                    event.target,
                    target_index)) {

                return server_status::io_error;
            }

            if (event.addition) {
                candidate_additions[
                    target_cursor[
                        target_index]++] =
                    event.source;

                continue;
            }

            const auto key =
                topology_edge_key(
                    event.target,
                    event.source);

            const auto mask =
                candidate_removals.size() - 1;

            auto position =
                topology_hash(
                    key) &
                mask;

            while (candidate_removals[
                       position] != 0) {

                if (candidate_removals[
                        position] == key) {

                    return server_status::io_error;
                }

                position =
                    (position + 1) &
                    mask;
            }

            candidate_removals[
                position] =
                key;
        }

        topology_sources =
            std::move(candidate_sources);

        topology_forward_edges =
            std::move(candidate_forward);

        topology_targets =
            std::move(candidate_targets);

        topology_target_index =
            std::move(candidate_target_index);

        topology_reverse_additions =
            std::move(candidate_additions);

        topology_reverse_removals =
            std::move(candidate_removals);

        topology_finalized = true;

        std::vector<file_dependency_edge>{}.swap(
            dependency_edges);

        return server_status::success;
    }
    catch (...) {
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

    if (topology_finalized ||
        !contains(source) ||
        !contains(target)) {

        return server_status::
            project_configuration_invalid;
    }

    if (baseline != nullptr) {
        std::size_t replacement = 0;

        if (!find_topology_source(
                source,
                replacement)) {

            if (source.value() <=
                baseline_file_count) {

                return server_status::
                    project_configuration_invalid;
            }

            const auto begun =
                begin_dependency_replacement(
                    source);

            if (!succeeded(begun)) {
                return begun;
            }
        }
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
        return finalize_baseline_dependency_topology();
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

    if (baseline != nullptr) {
        if (topology_finalized) {
            std::size_t replacement = 0;

            if (find_topology_source(
                    file,
                    replacement)) {

                const auto range =
                    topology_sources[
                        replacement]
                        .dependencies;

                if (range.count == 0) {
                    return {};
                }

                if (range.offset >
                        topology_forward_edges.size() ||
                    range.count >
                        topology_forward_edges.size() -
                            range.offset) {

                    return {};
                }

                return file_dependency_view::from_native(
                    std::span<const file_id>{
                        topology_forward_edges.data() +
                            range.offset,
                        range.count});
            }
        }

        if (file.value() <=
            baseline_file_count) {

            source_save_file_view state;

            return baseline->file(
                    file,
                    state)
                ? state.dependencies
                : file_dependency_view{};
        }

        return {};
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

    if (baseline != nullptr) {
        file_dependency_view base;

        if (file.value() <=
            baseline_file_count) {

            source_save_file_view state;

            if (!baseline->file(
                    file,
                    state)) {

                return {};
            }

            base =
                state.dependents;
        }

        if (!topology_finalized) {
            return base;
        }

        std::size_t delta = 0;

        if (!find_topology_target(
                file,
                delta)) {

            return base;
        }

        const auto& record =
            topology_targets[
                delta];

        if (record.removals >
                base.size() ||
            record.additions.offset >
                topology_reverse_additions.size() ||
            record.additions.count >
                topology_reverse_additions.size() -
                    record.additions.offset) {

            return {};
        }

        const auto final_count =
            base.size() -
            record.removals +
            record.additions.count;

        const std::span<const file_id>
            additions{
                topology_reverse_additions.data() +
                    record.additions.offset,
                record.additions.count};

        return file_dependency_view::from_overlay(
            base,
            additions,
            final_count,
            this,
            file,
            reverse_dependency_filter);
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

        if (find_baseline_overlay(
                file,
                overlay)) {

            const auto& state =
                baseline_overlays[
                    overlay];

            if (!state.physical.present()) {
                return false;
            }

            if (state.content.materialized()) {
                return true;
            }
        }

        std::string_view persisted;

        return content_baseline.read(
            file,
            persisted);
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

    if (baseline != nullptr &&
        file.value() <=
            baseline_file_count) {

        std::size_t overlay = 0;

        if (find_baseline_overlay(
                file,
                overlay)) {

            const auto& state =
                baseline_overlays[
                    overlay];

            if (!state.physical.present()) {
                return {};
            }

            if (state.content.materialized()) {
                const auto& record =
                    state.content;

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

        std::string_view persisted;

        return content_baseline.read(
                file,
                persisted)
            ? persisted
            : std::string_view{};
    }

    std::size_t index = 0;

    if (!local_index(
            file,
            index)) {

        return {};
    }

    const auto& record =
        content_files[index];

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
