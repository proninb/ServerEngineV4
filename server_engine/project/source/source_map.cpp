#include "source_map.hpp"

#include <limits>

namespace cw::server {
namespace {

constexpr auto max_index = (std::numeric_limits<std::uint32_t>::max)();

[[nodiscard]] bool is_type(source_data_ref data) noexcept {
    return data.kind() == source_data_kind::type_declaration ||
        data.kind() == source_data_kind::type_definition;
}

[[nodiscard]] std::uint64_t key(file_id file, std::uint32_t data) noexcept {
    return (std::uint64_t{file.value()} << 32) | data;
}

} // namespace

server_status source_map::reset(std::size_t file_count) noexcept {

    if (file_count > max_index) {
        return server_status::io_error;
    }

    try {
        root_ranges.assign(file_count, {});
        completed_roots.assign(file_count, false);

        file_ranges.clear();
        contributions.clear();
        file_indices.clear();
        type_presence.clear();
        object_presence.clear();
        link_presence.clear();
        root_seen.clear();

        active_root = {};
        active_root_begin = 0;
        finalized_value = false;

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status source_map::begin_root(file_id root) noexcept {

    if (!root ||
        active_root ||
        finalized_value ||
        contributions.size() > max_index) {

        return server_status::project_configuration_invalid;
    }

    try {
        if (root.value() > root_ranges.size()) {
            root_ranges.resize(root.value());
            completed_roots.resize(root.value());
        }

        if (completed_roots[root.value() - 1]) {
            return server_status::project_configuration_invalid;
        }

        root_seen.clear();

        active_root = root;
        active_root_begin =
            static_cast<std::uint32_t>(
                contributions.size());

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status source_map::add(file_id file, source_data_ref data) noexcept {

    if (!active_root ||
        finalized_value ||
        !file ||
        !data) {

        return server_status::project_configuration_invalid;
    }

    try {
        const auto owner_key =
            key(
                file,
                is_type(data)
                    ? data.slot()
                    : data.raw());

        const auto owner =
            root_seen.find(owner_key);

        if (owner != root_seen.end()) {
            auto& existing =
                contributions[owner->second].data;

            if (is_type(data) &&
                existing.kind() == source_data_kind::type_declaration &&
                data.kind() == source_data_kind::type_definition) {

                existing = data;
            }

            return server_status::success;
        }

        if (contributions.size() >= max_index) {
            return server_status::io_error;
        }

        const auto position =
            static_cast<std::uint32_t>(
                contributions.size());

        contributions.push_back({
            file,
            data,
        });

        root_seen.emplace(
            owner_key,
            position);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status source_map::end_root() noexcept {

    if (!active_root ||
        finalized_value) {

        return server_status::project_configuration_invalid;
    }

    root_ranges[
        active_root.value() - 1] = {
            active_root_begin,
            static_cast<std::uint32_t>(
                contributions.size() -
                active_root_begin),
        };

    completed_roots[
        active_root.value() - 1] = true;

    active_root = {};
    root_seen.clear();

    return server_status::success;
}

server_status source_map::finalize(
    std::size_t file_count,
    const identity_space& identities,
    const graph& G) noexcept {

    if (active_root ||
        finalized_value ||
        file_count > max_index ||
        root_ranges.size() > file_count) {

        return server_status::project_configuration_invalid;
    }

    try {
        root_ranges.resize(file_count);
        file_ranges.assign(file_count, {});

        type_presence.assign(
            G.type_count(),
            {});

        object_presence.assign(
            G.object_count(),
            0);

        link_presence.assign(
            G.link_count(),
            0);

        const auto increment =
            [](std::uint32_t& value) noexcept {

                if (value == max_index) {
                    return false;
                }

                ++value;
                return true;
            };

        for (const auto& contribution :
             contributions) {

            if (!contribution.file ||
                contribution.file.value() > file_count) {

                return server_status::project_artifact_invalid;
            }

            auto& file_range =
                file_ranges[
                    contribution.file.value() - 1];

            if (!increment(file_range.count)) {
                return server_status::io_error;
            }

            if (is_type(contribution.data)) {
                const auto identity =
                    identities.at_slot(
                        contribution.data.slot());

                const auto type =
                    identity.kind() == identity_kind::type
                        ? G.find_type(identity)
                        : type_handle{};

                const auto* entry =
                    G.find(type);

                if (!type || entry == nullptr) {
                    return server_status::project_artifact_invalid;
                }

                auto& presence =
                    type_presence[
                        type.value() - 1];

                if (!increment(presence.declarations)) {
                    return server_status::io_error;
                }

                if (contribution.data.kind() ==
                    source_data_kind::type_definition) {

                    if (!entry->defined()) {
                        return server_status::project_artifact_invalid;
                    }

                    if (!increment(presence.definitions)) {
                        return server_status::io_error;
                    }
                }

                continue;
            }

            if (contribution.data.kind() ==
                source_data_kind::object) {

                const auto identity =
                    identities.at_slot(
                        contribution.data.slot());

                const auto object =
                    identity.kind() == identity_kind::object
                        ? G.find_object(identity)
                        : object_handle{};

                if (!object) {
                    return server_status::project_artifact_invalid;
                }

                if (!increment(
                        object_presence[
                            object.value() - 1])) {

                    return server_status::io_error;
                }

                continue;
            }

            if (contribution.data.slot() >
                link_presence.size()) {

                return server_status::project_artifact_invalid;
            }

            if (!increment(
                    link_presence[
                        contribution.data.slot() - 1])) {

                return server_status::io_error;
            }
        }

        std::uint32_t cursor = 0;

        for (auto& range :
             file_ranges) {

            range.begin = cursor;
            cursor += range.count;
        }

        file_indices.resize(
            contributions.size());

        std::vector<std::uint32_t>
            next(file_count);

        for (std::size_t index = 0;
             index < file_count;
             ++index) {

            next[index] =
                file_ranges[index].begin;
        }

        for (std::size_t index = 0;
             index < contributions.size();
             ++index) {

            file_indices[
                next[
                    contributions[index].
                        file.value() - 1]++] =
                static_cast<std::uint32_t>(
                    index);
        }

        root_seen.clear();
        root_seen.rehash(0);
        completed_roots.clear();

        finalized_value = true;

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

std::span<const source_contribution_record>
source_map::root(file_id id) const noexcept {

    if (!finalized_value ||
        !id ||
        id.value() > root_ranges.size()) {

        return {};
    }

    const auto range =
        root_ranges[
            id.value() - 1];

    return std::span<
        const source_contribution_record>{
            contributions}
        .subspan(
            range.begin,
            range.count);
}

bool source_map::file(
    file_id id,
    source_map_file_view& output) const noexcept {

    output = {};

    if (!finalized_value ||
        !id ||
        id.value() > file_ranges.size()) {

        return false;
    }

    const auto range =
        file_ranges[
            id.value() - 1];

    output.contributions =
        contributions;

    output.indices =
        std::span<
            const std::uint32_t>{
                file_indices}
            .subspan(
                range.begin,
                range.count);

    return true;
}

} // namespace cw::server
