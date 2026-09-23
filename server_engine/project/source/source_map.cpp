#include "source_map.hpp"
#include <algorithm>
#include <limits>

namespace cw::server {
namespace {
constexpr auto max_index = (std::numeric_limits<std::uint32_t>::max)();
bool is_type(source_data_ref data) noexcept {
    return data.kind() == source_data_kind::type_declaration ||
           data.kind() == source_data_kind::type_definition;
}
std::uint64_t key(file_id file, std::uint32_t data) noexcept {
    return (std::uint64_t{file.value()} << 32) | data;
}
} // namespace
server_status source_map::reset(std::size_t file_count) noexcept {
    if (file_count > max_index)
        return server_status::io_error;
    try {
        root_ranges.assign(file_count, {});
        completed_roots.assign(file_count, false);
        file_ranges.clear();
        contributions.clear();
        root_indices.clear();
        file_indices.clear();
        type_presence.clear();
        object_presence.clear();
        link_presence.clear();
        canonical.clear();
        root_seen.clear();
        active_root = {};
        active_root_begin = 0;
        finalized_value = false;
        return server_status::success;
    } catch (...) {
        return server_status::io_error;
    }
}
server_status source_map::begin_root(file_id root) noexcept {
    if (!root || active_root || finalized_value || root_indices.size() > max_index)
        return server_status::project_configuration_invalid;
    try {
        if (root.value() > root_ranges.size()) {
            root_ranges.resize(root.value());
            completed_roots.resize(root.value());
        }
        if (completed_roots[root.value() - 1])
            return server_status::project_configuration_invalid;
        root_seen.clear();
        active_root = root;
        active_root_begin = static_cast<std::uint32_t>(root_indices.size());
        return server_status::success;
    } catch (...) {
        return server_status::io_error;
    }
}
server_status source_map::add(file_id file, source_data_ref data) noexcept {
    if (!active_root || finalized_value || !file || !data)
        return server_status::project_configuration_invalid;
    try {
        // Declaration -> definition dominance is LOCAL to this root/file pair.
        const auto owner_key = key(file, is_type(data) ? data.slot() : data.raw());
        auto owner = root_seen.find(owner_key);
        if (owner != root_seen.end()) {
            const auto existing = contributions[root_indices[owner->second]].data;
            if (existing == data || data.kind() == source_data_kind::type_declaration)
                return server_status::success;
        }
        const auto canonical_key = key(file, data.raw());
        auto found = canonical.find(canonical_key);
        std::uint32_t id;
        if (found == canonical.end()) {
            if (contributions.size() >= max_index)
                return server_status::io_error;
            id = static_cast<std::uint32_t>(contributions.size());
            contributions.push_back({file, data});
            canonical.emplace(canonical_key, id);
        } else
            id = found->second;
        if (owner != root_seen.end())
            root_indices[owner->second] = id;
        else {
            if (root_indices.size() >= max_index)
                return server_status::io_error;
            const auto position = static_cast<std::uint32_t>(root_indices.size());
            root_indices.push_back(id);
            root_seen.emplace(owner_key, position);
        }
        return server_status::success;
    } catch (...) {
        return server_status::io_error;
    }
}
server_status source_map::end_root() noexcept {
    if (!active_root || finalized_value)
        return server_status::project_configuration_invalid;
    root_ranges[active_root.value() - 1] = {
        active_root_begin, static_cast<std::uint32_t>(root_indices.size() - active_root_begin)};
    completed_roots[active_root.value() - 1] = true;
    active_root = {};
    root_seen.clear();
    return server_status::success;
}
server_status source_map::finalize(std::size_t file_count, const graph &G) noexcept {
    if (active_root || finalized_value || file_count > max_index || root_ranges.size() > file_count)
        return server_status::project_configuration_invalid;
    try {
        // Compact contributions abandoned by root-local declaration upgrades.
        std::vector<std::uint32_t> owners(contributions.size());
        for (auto id : root_indices) {
            if (id >= owners.size() || owners[id] == max_index)
                return server_status::project_artifact_invalid;
            ++owners[id];
        }
        std::vector<std::uint32_t> remap(contributions.size(), max_index);
        std::size_t retained = 0;
        for (std::size_t i = 0; i < contributions.size(); ++i)
            if (owners[i]) {
                remap[i] = static_cast<std::uint32_t>(retained);
                contributions[retained] = contributions[i];
                owners[retained++] = owners[i];
            }
        contributions.resize(retained);
        owners.resize(retained);
        for (auto &id : root_indices)
            id = remap[id];
        root_ranges.resize(file_count);
        file_ranges.assign(file_count, {});
        type_presence.assign(G.type_count(), {});
        object_presence.assign(G.object_count(), 0);
        link_presence.assign(G.link_count(), 0);
        std::unordered_map<std::uint32_t, std::size_t> types, objects;
        std::size_t i = 0;
        for (auto id : G.type_identity_entries())
            types.emplace(id.slot(), i++);
        i = 0;
        for (auto id : G.object_identity_entries())
            objects.emplace(id.slot(), i++);
        for (i = 0; i < contributions.size(); ++i) {
            const auto c = contributions[i];
            if (!c.file || c.file.value() > file_count)
                return server_status::project_artifact_invalid;
            ++file_ranges[c.file.value() - 1].count;
            const auto increment = [&](std::uint32_t &value) {
                if (owners[i] > max_index - value)
                    return false;
                value += owners[i];
                return true;
            };
            if (is_type(c.data)) {
                auto t = types.find(c.data.slot());
                if (t == types.end())
                    return server_status::project_artifact_invalid;
                auto &p = type_presence[t->second];
                if (!increment(p.declarations))
                    return server_status::io_error;
                if (c.data.kind() == source_data_kind::type_definition) {
                    if (!G.type_entries()[t->second].defined())
                        return server_status::project_artifact_invalid;
                    if (!increment(p.definitions))
                        return server_status::io_error;
                }
            } else if (c.data.kind() == source_data_kind::object) {
                auto o = objects.find(c.data.slot());
                if (o == objects.end())
                    return server_status::project_artifact_invalid;
                if (!increment(object_presence[o->second]))
                    return server_status::io_error;
            } else {
                if (c.data.slot() > link_presence.size())
                    return server_status::project_artifact_invalid;
                if (!increment(link_presence[c.data.slot() - 1]))
                    return server_status::io_error;
            }
        }
        std::uint32_t cursor = 0;
        for (auto &range : file_ranges) {
            range.begin = cursor;
            cursor += range.count;
        }
        file_indices.resize(contributions.size());
        std::vector<std::uint32_t> next(file_count);
        for (i = 0; i < file_count; ++i)
            next[i] = file_ranges[i].begin;
        for (i = 0; i < contributions.size(); ++i)
            file_indices[next[contributions[i].file.value() - 1]++] = static_cast<std::uint32_t>(i);
        canonical.clear();
        canonical.rehash(0);
        root_seen.clear();
        root_seen.rehash(0);
        completed_roots.clear();
        finalized_value = true;
        return server_status::success;
    } catch (...) {
        return server_status::io_error;
    }
}
source_map_file_view source_map::root(file_id id) const noexcept {
    source_map_file_view output;
    if (!finalized_value || !id || id.value() > root_ranges.size())
        return output;
    auto r = root_ranges[id.value() - 1];
    output.contributions = contributions;
    output.indices = std::span<const std::uint32_t>{root_indices}.subspan(r.begin, r.count);
    return output;
}
bool source_map::file(file_id id, source_map_file_view &output) const noexcept {
    output = {};
    if (!finalized_value || !id || id.value() > file_ranges.size())
        return false;
    auto r = file_ranges[id.value() - 1];
    output.contributions = contributions;
    output.indices = std::span<const std::uint32_t>{file_indices}.subspan(r.begin, r.count);
    return true;
}
} // namespace cw::server
