#include "source_map.hpp"

#include <limits>

namespace cw::server {
namespace {

constexpr auto max_index =
    (std::numeric_limits<std::uint32_t>::max)();

[[nodiscard]] bool is_type(
    source_data_ref data) noexcept {

    return data.kind() ==
            source_data_kind::type_declaration ||
        data.kind() ==
            source_data_kind::type_definition;
}

[[nodiscard]] std::uint64_t key(
    file_id file,
    std::uint32_t data) noexcept {

    return
        (std::uint64_t{file.value()} << 32) |
        data;
}

}

server_status source_map::reset(
    std::size_t file_count) noexcept {

    if (file_count > max_index) {
        return server_status::io_error;
    }

    try {
        root_ranges.assign(
            file_count,
            {});

        root_dependency_ranges.assign(
            file_count,
            {});

        completed_roots.assign(
            file_count,
            false);

        file_ranges.clear();
        contributions.clear();
        file_indices.clear();

        dependencies.clear();
        dependency_index.clear();
        dependent_roots.clear();

        type_presence.clear();
        object_presence.clear();
        link_presence.clear();

        root_seen.clear();
        root_dependency_seen.clear();

        active_root = {};
        active_root_begin = 0;
        active_dependency_begin = 0;
        finalized_value = false;

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status source_map::begin_root(
    file_id root) noexcept {

    if (!root ||
        active_root ||
        finalized_value ||
        contributions.size() > max_index ||
        dependencies.size() > max_index) {

        return server_status::
            project_configuration_invalid;
    }

    try {
        if (root.value() >
            root_ranges.size()) {

            root_ranges.resize(
                root.value());

            root_dependency_ranges.resize(
                root.value());

            completed_roots.resize(
                root.value());
        }

        if (completed_roots[
                root.value() - 1]) {

            return server_status::
                project_configuration_invalid;
        }

        root_seen.clear();
        root_dependency_seen.clear();

        active_root = root;

        active_root_begin =
            static_cast<std::uint32_t>(
                contributions.size());

        active_dependency_begin =
            static_cast<std::uint32_t>(
                dependencies.size());

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status source_map::add(
    file_id file,
    source_data_ref data) noexcept {

    if (!active_root ||
        finalized_value ||
        !file ||
        !data) {

        return server_status::
            project_configuration_invalid;
    }

    try {
        const auto owner_key =
            key(
                file,
                is_type(data)
                    ? data.slot()
                    : data.raw());

        const auto owner =
            root_seen.find(
                owner_key);

        if (owner !=
            root_seen.end()) {

            auto& existing =
                contributions[
                    owner->second].data;

            if (is_type(data) &&
                existing.kind() ==
                    source_data_kind::
                        type_declaration &&
                data.kind() ==
                    source_data_kind::
                        type_definition) {

                existing = data;
            }

            return server_status::success;
        }

        if (contributions.size() >=
            max_index) {

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

server_status source_map::add_dependency(
    type_handle type) noexcept {

    return add_dependency(
        source_dependency_ref::type(
            type));
}

server_status source_map::add_dependency(
    object_handle object) noexcept {

    return add_dependency(
        source_dependency_ref::object(
            object));
}

server_status source_map::add_dependency(
    source_dependency_ref dependency) noexcept {

    if (!active_root ||
        finalized_value ||
        !dependency) {

        return server_status::
            project_configuration_invalid;
    }

    try {
        const auto inserted =
            root_dependency_seen.insert(
                dependency.raw());

        if (!inserted.second) {
            return server_status::success;
        }

        if (dependencies.size() >=
            max_index) {

            root_dependency_seen.erase(
                dependency.raw());

            return server_status::io_error;
        }

        dependencies.push_back(
            dependency);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status source_map::end_root() noexcept {

    if (!active_root ||
        finalized_value) {

        return server_status::
            project_configuration_invalid;
    }

    root_ranges[
        active_root.value() - 1] = {
            active_root_begin,
            static_cast<std::uint32_t>(
                contributions.size() -
                active_root_begin),
        };

    root_dependency_ranges[
        active_root.value() - 1] = {
            active_dependency_begin,
            static_cast<std::uint32_t>(
                dependencies.size() -
                active_dependency_begin),
        };

    completed_roots[
        active_root.value() - 1] =
            true;

    active_root = {};
    root_seen.clear();
    root_dependency_seen.clear();

    return server_status::success;
}

server_status source_map::finalize(
    std::size_t file_count,
    const identity_space& identities,
    const graph& G) noexcept {

    if (active_root ||
        finalized_value ||
        file_count > max_index ||
        root_ranges.size() > file_count ||
        root_dependency_ranges.size() >
            file_count ||
        dependencies.size() > max_index) {

        return server_status::
            project_configuration_invalid;
    }

    try {
        root_ranges.resize(
            file_count);

        root_dependency_ranges.resize(
            file_count);

        file_ranges.assign(
            file_count,
            {});

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

        std::size_t unique_dependency_count = 0;

        const auto grow_dependency_index =
            [&]() -> bool {

                std::size_t capacity = 8;

                if (!dependency_index.empty()) {
                    if (dependency_index.size() >
                        static_cast<std::size_t>(
                            max_index) / 2) {

                        return false;
                    }

                    capacity =
                        dependency_index.size() * 2;
                }

                if (capacity < 8 ||
                    capacity <=
                        dependency_index.size() ||
                    capacity >
                        static_cast<std::size_t>(
                            max_index)) {

                    return false;
                }

                std::vector<
                    source_dependency_index_slot>
                    replacement(
                        capacity);

                const auto mask =
                    capacity - 1;

                for (const auto& existing :
                     dependency_index) {

                    if (!existing.dependency) {
                        continue;
                    }

                    auto position =
                        static_cast<std::size_t>(
                            source_dependency_hash(
                                existing.dependency)) &
                        mask;

                    bool inserted = false;

                    for (std::size_t probe = 0;
                         probe < capacity;
                         ++probe) {

                        auto& slot =
                            replacement[
                                position];

                        if (!slot.dependency) {
                            slot =
                                existing;
                            inserted = true;
                            break;
                        }

                        position =
                            (position + 1) &
                            mask;
                    }

                    if (!inserted) {
                        return false;
                    }
                }

                dependency_index.swap(
                    replacement);

                return true;
            };

        const auto find_or_insert_slot =
            [&](source_dependency_ref dependency)
            -> source_dependency_index_slot* {

                if (!dependency) {
                    return nullptr;
                }

                for (;;) {
                    if (dependency_index.empty() ||
                        unique_dependency_count + 1 >
                            dependency_index.size() / 2) {

                        if (!grow_dependency_index()) {
                            return nullptr;
                        }
                    }

                    const auto mask =
                        dependency_index.size() - 1;

                    auto position =
                        static_cast<std::size_t>(
                            source_dependency_hash(
                                dependency)) &
                        mask;

                    for (std::size_t probe = 0;
                         probe <
                            dependency_index.size();
                         ++probe) {

                        auto& slot =
                            dependency_index[
                                position];

                        if (!slot.dependency) {
                            slot.dependency =
                                dependency;

                            ++unique_dependency_count;
                            return &slot;
                        }

                        if (slot.dependency ==
                            dependency) {

                            return &slot;
                        }

                        position =
                            (position + 1) &
                            mask;
                    }

                    if (!grow_dependency_index()) {
                        return nullptr;
                    }
                }
            };

        for (const auto dependency :
             dependencies) {

            bool target_valid = false;

            if (dependency.kind() ==
                source_dependency_kind::type) {

                target_valid =
                    G.contains(
                        type_handle{
                            dependency.slot()});
            }
            else if (dependency.kind() ==
                source_dependency_kind::object) {

                target_valid =
                    G.contains(
                        object_handle{
                            dependency.slot()});
            }

            auto* slot =
                target_valid
                ? find_or_insert_slot(
                    dependency)
                : nullptr;

            if (slot == nullptr ||
                !increment(
                    slot->roots.count)) {

                return slot == nullptr
                    ? server_status::
                        project_artifact_invalid
                    : server_status::io_error;
            }
        }

        if ((dependencies.empty() &&
             (!dependency_index.empty() ||
              unique_dependency_count != 0)) ||
            (!dependencies.empty() &&
             (dependency_index.empty() ||
              unique_dependency_count == 0 ||
              unique_dependency_count >
                  dependency_index.size() / 2))) {

            return server_status::
                project_artifact_invalid;
        }

        std::uint32_t dependent_cursor = 0;

        for (auto& slot :
             dependency_index) {

            if (!slot.dependency) {
                if (slot.roots.begin != 0 ||
                    slot.roots.count != 0) {

                    return server_status::
                        project_artifact_invalid;
                }

                continue;
            }

            slot.roots.begin =
                dependent_cursor;

            if (slot.roots.count >
                max_index -
                    dependent_cursor) {

                return server_status::io_error;
            }

            dependent_cursor +=
                slot.roots.count;
        }

        if (dependent_cursor !=
            dependencies.size()) {

            return server_status::
                project_artifact_invalid;
        }

        dependent_roots.resize(
            dependencies.size());

        std::vector<std::uint32_t>
            next(
                dependency_index.size());

        for (std::size_t index = 0;
             index <
                dependency_index.size();
             ++index) {

            next[index] =
                dependency_index[
                    index].roots.begin;
        }

        const auto find_position =
            [&](source_dependency_ref dependency)
            -> std::size_t {

                if (!dependency ||
                    dependency_index.empty()) {

                    return dependency_index.size();
                }

                const auto mask =
                    dependency_index.size() - 1;

                auto position =
                    static_cast<std::size_t>(
                        source_dependency_hash(
                            dependency)) &
                    mask;

                for (std::size_t probe = 0;
                     probe <
                        dependency_index.size();
                     ++probe) {

                    const auto& slot =
                        dependency_index[
                            position];

                    if (!slot.dependency) {
                        return dependency_index.size();
                    }

                    if (slot.dependency ==
                        dependency) {

                        return position;
                    }

                    position =
                        (position + 1) &
                        mask;
                }

                return dependency_index.size();
            };

        for (std::size_t root_index = 0;
             root_index <
                root_dependency_ranges.size();
             ++root_index) {

            const auto range =
                root_dependency_ranges[
                    root_index];

            if (range.begin >
                    dependencies.size() ||
                range.count >
                    dependencies.size() -
                        range.begin) {

                return server_status::
                    project_artifact_invalid;
            }

            const file_id root{
                static_cast<std::uint32_t>(
                    root_index + 1)};

            for (std::uint32_t offset = 0;
                 offset <
                    range.count;
                 ++offset) {

                const auto dependency =
                    dependencies[
                        range.begin +
                        offset];

                const auto position =
                    find_position(
                        dependency);

                if (position >=
                    dependency_index.size()) {

                    return server_status::
                        project_artifact_invalid;
                }

                auto& cursor =
                    next[position];

                const auto& slot =
                    dependency_index[
                        position];

                if (cursor <
                        slot.roots.begin ||
                    cursor >=
                        slot.roots.begin +
                            slot.roots.count) {

                    return server_status::
                        project_artifact_invalid;
                }

                dependent_roots[
                    cursor++] =
                        root;
            }
        }

        // root_index traversal is ascending, so every reverse adjacency range
        // is emitted ascending without a sort.

        for (const auto& contribution :
             contributions) {

            if (!contribution.file ||
                contribution.file.value() >
                    file_count) {

                return server_status::
                    project_artifact_invalid;
            }

            auto& file_range =
                file_ranges[
                    contribution.file.value() -
                    1];

            if (!increment(
                    file_range.count)) {

                return server_status::io_error;
            }

            if (is_type(
                    contribution.data)) {

                const auto identity =
                    identities.at_slot(
                        contribution.data.slot());

                const auto type =
                    identity.kind() ==
                        identity_kind::type
                    ? G.find_type(
                        identity)
                    : type_handle{};

                type_entry entry;

                if (!type ||
                    !G.type(
                        type,
                        entry)) {

                    return server_status::
                        project_artifact_invalid;
                }

                auto& presence =
                    type_presence[
                        type.value() - 1];

                if (!increment(
                        presence.declarations)) {

                    return server_status::io_error;
                }

                if (contribution.data.kind() ==
                    source_data_kind::
                        type_definition) {

                    if (!entry.defined()) {
                        return server_status::
                            project_artifact_invalid;
                    }

                    if (!increment(
                            presence.definitions)) {

                        return server_status::
                            io_error;
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
                    identity.kind() ==
                        identity_kind::object
                    ? G.find_object(
                        identity)
                    : object_handle{};

                if (!object) {
                    return server_status::
                        project_artifact_invalid;
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

                return server_status::
                    project_artifact_invalid;
            }

            if (!increment(
                    link_presence[
                        contribution.data.slot() -
                        1])) {

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
            file_next(
                file_count);

        for (std::size_t index = 0;
             index < file_count;
             ++index) {

            file_next[index] =
                file_ranges[index].begin;
        }

        for (std::size_t index = 0;
             index <
                contributions.size();
             ++index) {

            file_indices[
                file_next[
                    contributions[index].
                        file.value() - 1]++] =
                static_cast<std::uint32_t>(
                    index);
        }

        root_seen.clear();
        root_seen.rehash(0);

        root_dependency_seen.clear();
        root_dependency_seen.rehash(0);

        completed_roots.clear();

        finalized_value = true;

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

std::span<const source_contribution_record>
source_map::root(
    file_id id) const noexcept {

    if (!finalized_value ||
        !id ||
        id.value() >
            root_ranges.size()) {

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
        id.value() >
            file_ranges.size()) {

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

std::span<const source_dependency_ref>
source_map::root_dependencies(
    file_id root) const noexcept {

    if (!finalized_value ||
        !root ||
        root.value() >
            root_dependency_ranges.size()) {

        return {};
    }

    const auto range =
        root_dependency_ranges[
            root.value() - 1];

    return std::span<
        const source_dependency_ref>{
            dependencies}
        .subspan(
            range.begin,
            range.count);
}

std::span<const file_id>
source_map::dependents(
    source_dependency_ref dependency) const noexcept {

    if (!finalized_value ||
        !dependency ||
        dependency_index.empty()) {

        return {};
    }

    const auto mask =
        dependency_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            source_dependency_hash(
                dependency)) &
        mask;

    for (std::size_t probe = 0;
         probe <
            dependency_index.size();
         ++probe) {

        const auto& slot =
            dependency_index[
                position];

        if (!slot.dependency) {
            return {};
        }

        if (slot.dependency ==
            dependency) {

            return std::span<const file_id>{
                dependent_roots}
                .subspan(
                    slot.roots.begin,
                    slot.roots.count);
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

}
