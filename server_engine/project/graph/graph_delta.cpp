#include "graph_delta.hpp"
#include "../persistence/compiled_project.hpp"

#include <limits>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] std::size_t next_index_capacity(
    std::size_t required) noexcept {

    if (required == 0) {
        return 0;
    }

    constexpr std::size_t minimum_capacity = 8;

    const auto maximum =
        (std::numeric_limits<std::size_t>::max)();

    if (required > maximum / 2) {
        return 0;
    }

    const auto minimum =
        required * 2;

    std::size_t capacity =
        minimum_capacity;

    while (capacity < minimum) {
        if (capacity > maximum / 2) {
            return 0;
        }

        capacity *= 2;
    }

    return capacity;
}

[[nodiscard]] bool valid_intrinsic(
    intrinsic_type type) noexcept {

    return type > intrinsic_type::none &&
        type <= intrinsic_type::nullptr_type;
}

[[nodiscard]] bool valid_record_kind(
    graph_record_kind kind) noexcept {

    return kind == graph_record_kind::struct_type ||
        kind == graph_record_kind::class_type ||
        kind == graph_record_kind::union_type;
}

[[nodiscard]] bool compatible_record_kind(
    graph_record_kind left,
    graph_record_kind right) noexcept {

    const auto left_union =
        left == graph_record_kind::union_type;

    const auto right_union =
        right == graph_record_kind::union_type;

    return left_union == right_union;
}

[[nodiscard]] bool valid_derived_kind(
    derived_type_kind kind) noexcept {

    return kind >= derived_type_kind::const_qualified &&
        kind <= derived_type_kind::unbounded_array;
}

[[nodiscard]] std::uint32_t mix32(
    std::uint32_t value) noexcept {

    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;

    return value;
}

[[nodiscard]] std::uint64_t mix64(
    std::uint64_t value) noexcept {

    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;

    return value;
}

}

std::uint32_t graph_delta::sparse_index::find(
    std::uint32_t key) const noexcept {

    if (key == 0 ||
        slots.empty()) {

        return 0;
    }

    const auto mask =
        slots.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix32(key)) &
        mask;

    for (std::size_t probe = 0;
         probe < slots.size();
         ++probe) {

        const auto& slot =
            slots[position];

        if (slot.key == 0) {
            return 0;
        }

        if (slot.key == key) {
            return slot.value;
        }

        position =
            (position + 1) &
            mask;
    }

    return 0;
}

server_status graph_delta::sparse_index::ensure_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            count) {

        return server_status::io_error;
    }

    const auto required =
        count + additional;

    if (!slots.empty() &&
        required <=
            slots.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(
            required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<sparse_index_slot>
            candidate(capacity);

        const auto mask =
            candidate.size() - 1;

        for (const auto& value :
             slots) {

            if (value.key == 0) {
                continue;
            }

            auto position =
                static_cast<std::size_t>(
                    mix32(
                        value.key)) &
                mask;

            while (candidate[position].key != 0) {
                position =
                    (position + 1) &
                    mask;
            }

            candidate[position] =
                value;
        }

        slots =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status graph_delta::sparse_index::insert(
    std::uint32_t key,
    std::uint32_t value) noexcept {

    if (key == 0 ||
        value == 0) {

        return server_status::
            project_configuration_invalid;
    }

    if (const auto existing =
            find(key);
        existing != 0) {

        return existing == value
            ? server_status::success
            : server_status::
                project_configuration_invalid;
    }

    const auto prepared =
        ensure_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto mask =
        slots.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix32(key)) &
        mask;

    while (slots[position].key != 0) {
        position =
            (position + 1) &
            mask;
    }

    slots[position] = {
        key,
        value,
    };

    ++count;

    return server_status::success;
}

std::uint32_t graph_delta::encode_location(
    location_kind kind,
    std::uint32_t slot) noexcept {

    if (kind == location_kind::none ||
        slot == 0 ||
        slot > type_ref::maximum_payload) {

        return 0;
    }

    return
        (static_cast<std::uint32_t>(kind) << 30) |
        slot;
}

graph_delta::location_kind graph_delta::decode_location_kind(
    std::uint32_t value) noexcept {

    return static_cast<location_kind>(
        value >> 30);
}

std::uint32_t graph_delta::decode_location_slot(
    std::uint32_t value) noexcept {

    return value &
        type_ref::maximum_payload;
}

server_status graph_delta::bind_baseline(
    const compiled_project_view& value) noexcept {

    if (baseline != nullptr ||
        !value.valid() ||
        !identity_locations.empty() ||
        !identity_overlay.empty() ||
        !type_patches.empty() ||
        !object_patches.empty() ||
        !link_patches.empty() ||
        !types.empty() ||
        !type_identities.empty() ||
        !member_records.empty() ||
        !member_construction.empty() ||
        !objects.empty() ||
        !object_identities.empty() ||
        !object_construction.empty() ||
        !links.empty() ||
        !derived_types.empty()) {

        return server_status::
            project_configuration_invalid;
    }

    baseline = &value;

    baseline_type_count =
        value.type_count();

    baseline_member_count =
        value.member_count();

    baseline_object_count =
        value.object_count();

    baseline_object_construction_count =
        value.object_construction_count();

    baseline_link_count =
        value.link_count();

    baseline_derived_count =
        value.derived_type_count();

    live_type_count_value =
        baseline_type_count;

    live_object_count_value =
        baseline_object_count;

    live_link_count_value =
        baseline_link_count;

    return server_status::success;
}

server_status graph_delta::ensure_identity_slot(
    identity_ref identity) noexcept {

    if (!identity) {
        return server_status::
            project_configuration_invalid;
    }

    const auto slot =
        static_cast<std::size_t>(
            identity.slot());

    if (slot <
        identity_locations.size()) {

        return server_status::success;
    }

    try {
        identity_locations.resize(
            slot + 1);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status graph_delta::publish_identity_location(
    identity_ref identity,
    location_kind kind,
    std::uint32_t slot) noexcept {

    const auto location =
        encode_location(
            kind,
            slot);

    if (!identity ||
        location == 0) {

        return server_status::
            project_configuration_invalid;
    }

    if (baseline != nullptr) {
        return identity_overlay.insert(
            identity.value(),
            location);
    }

    const auto prepared =
        ensure_identity_slot(
            identity);

    if (!succeeded(prepared)) {
        return prepared;
    }

    auto& target =
        identity_locations[
            identity.slot()];

    if (target != 0) {
        return target == location
            ? server_status::success
            : server_status::
                project_configuration_invalid;
    }

    target = location;

    return server_status::success;
}

std::uint32_t graph_delta::lineage_location(
    identity_ref identity) const noexcept {

    if (!identity) {
        return 0;
    }

    if (baseline != nullptr) {
        if (const auto local =
                identity_overlay.find(
                    identity.value());
            local != 0) {

            return local;
        }

        if (identity.kind() ==
            identity_kind::type) {

            const auto type =
                baseline->find_type(
                    identity);

            return type
                ? encode_location(
                    location_kind::type,
                    type.value())
                : 0;
        }

        if (identity.kind() ==
            identity_kind::object) {

            const auto object =
                baseline->find_object(
                    identity);

            return object
                ? encode_location(
                    location_kind::object,
                    object.value())
                : 0;
        }

        return 0;
    }

    return identity.slot() <
        identity_locations.size()
        ? identity_locations[
            identity.slot()]
        : 0;
}

type_handle graph_delta::lineage_type(
    identity_ref identity) const noexcept {

    const auto location =
        lineage_location(
            identity);

    return decode_location_kind(
        location) ==
        location_kind::type
        ? type_handle{
            decode_location_slot(
                location)}
        : type_handle{};
}

object_handle graph_delta::lineage_object(
    identity_ref identity) const noexcept {

    const auto location =
        lineage_location(
            identity);

    return decode_location_kind(
        location) ==
        location_kind::object
        ? object_handle{
            decode_location_slot(
                location)}
        : object_handle{};
}

const graph_delta::type_patch* graph_delta::find_type_patch(
    std::uint32_t slot) const noexcept {

    const auto position =
        type_patch_index.find(
            slot);

    return position != 0 &&
        position <= type_patches.size()
        ? &type_patches[
            position - 1]
        : nullptr;
}

graph_delta::type_patch* graph_delta::find_type_patch(
    std::uint32_t slot) noexcept {

    const auto position =
        type_patch_index.find(
            slot);

    return position != 0 &&
        position <= type_patches.size()
        ? &type_patches[
            position - 1]
        : nullptr;
}

server_status graph_delta::ensure_type_patch(
    type_handle type_value,
    type_patch*& output) noexcept {

    output = nullptr;

    if (baseline == nullptr ||
        !type_value ||
        type_value.value() >
            baseline_type_count) {

        return server_status::
            project_configuration_invalid;
    }

    if (auto* existing =
            find_type_patch(
                type_value.value());
        existing != nullptr) {

        output = existing;
        return server_status::success;
    }

    type_entry value;

    if (!baseline->type(
            type_value,
            value)) {

        return server_status::
            project_artifact_invalid;
    }

    try {
        type_patches.push_back({
            type_value.value(),
            value,
            true,
        });
    }
    catch (...) {
        return server_status::io_error;
    }

    const auto inserted =
        type_patch_index.insert(
            type_value.value(),
            static_cast<std::uint32_t>(
                type_patches.size()));

    if (!succeeded(inserted)) {
        type_patches.pop_back();
        return inserted;
    }

    output =
        &type_patches.back();

    return server_status::success;
}

const graph_delta::object_patch* graph_delta::find_object_patch(
    std::uint32_t slot) const noexcept {

    const auto position =
        object_patch_index.find(
            slot);

    return position != 0 &&
        position <= object_patches.size()
        ? &object_patches[
            position - 1]
        : nullptr;
}

graph_delta::object_patch* graph_delta::find_object_patch(
    std::uint32_t slot) noexcept {

    const auto position =
        object_patch_index.find(
            slot);

    return position != 0 &&
        position <= object_patches.size()
        ? &object_patches[
            position - 1]
        : nullptr;
}

server_status graph_delta::ensure_object_patch(
    object_handle object_value,
    object_patch*& output) noexcept {

    output = nullptr;

    if (baseline == nullptr ||
        !object_value ||
        object_value.value() >
            baseline_object_count) {

        return server_status::
            project_configuration_invalid;
    }

    if (auto* existing =
            find_object_patch(
                object_value.value());
        existing != nullptr) {

        output = existing;
        return server_status::success;
    }

    object_entry value;
    construction_value construction;

    if (!baseline->object(
            object_value,
            value) ||
        !baseline->construction(
            object_value,
            construction)) {

        return server_status::
            project_artifact_invalid;
    }

    try {
        object_patches.push_back({
            object_value.value(),
            value,
            construction,
            true,
        });
    }
    catch (...) {
        return server_status::io_error;
    }

    const auto inserted =
        object_patch_index.insert(
            object_value.value(),
            static_cast<std::uint32_t>(
                object_patches.size()));

    if (!succeeded(inserted)) {
        object_patches.pop_back();
        return inserted;
    }

    output =
        &object_patches.back();

    return server_status::success;
}

const graph_delta::link_patch* graph_delta::find_link_patch(
    std::uint32_t slot) const noexcept {

    const auto position =
        link_patch_index.find(
            slot);

    return position != 0 &&
        position <= link_patches.size()
        ? &link_patches[
            position - 1]
        : nullptr;
}

graph_delta::link_patch* graph_delta::find_link_patch(
    std::uint32_t slot) noexcept {

    const auto position =
        link_patch_index.find(
            slot);

    return position != 0 &&
        position <= link_patches.size()
        ? &link_patches[
            position - 1]
        : nullptr;
}

server_status graph_delta::ensure_link_patch(
    link_handle link_value,
    link_patch*& output) noexcept {

    output = nullptr;

    if (baseline == nullptr ||
        !link_value ||
        link_value.value() >
            baseline_link_count) {

        return server_status::
            project_configuration_invalid;
    }

    if (auto* existing =
            find_link_patch(
                link_value.value());
        existing != nullptr) {

        output = existing;
        return server_status::success;
    }

    link_record value;

    if (!baseline->link(
            link_value,
            value)) {

        return server_status::
            project_artifact_invalid;
    }

    try {
        link_patches.push_back({
            link_value.value(),
            value,
            true,
        });
    }
    catch (...) {
        return server_status::io_error;
    }

    const auto inserted =
        link_patch_index.insert(
            link_value.value(),
            static_cast<std::uint32_t>(
                link_patches.size()));

    if (!succeeded(inserted)) {
        link_patches.pop_back();
        return inserted;
    }

    output =
        &link_patches.back();

    return server_status::success;
}

bool graph_delta::slot_exists(
    type_handle type_value) const noexcept {

    return type_value &&
        type_value.value() <=
            type_count();
}

bool graph_delta::slot_exists(
    object_handle object_value) const noexcept {

    return object_value &&
        object_value.value() <=
            object_count();
}

bool graph_delta::slot_exists(
    link_handle link_value) const noexcept {

    return link_value &&
        link_value.value() <=
            link_count();
}

bool graph_delta::contains(
    type_handle type_value) const noexcept {

    if (!slot_exists(type_value)) {
        return false;
    }

    if (type_value.value() <=
        baseline_type_count) {

        const auto* patch =
            find_type_patch(
                type_value.value());

        return patch == nullptr ||
            patch->live;
    }

    const auto index =
        static_cast<std::size_t>(
            type_value.value() -
            baseline_type_count -
            1);

    return index <
            type_live.size() &&
        type_live[index] != 0;
}

bool graph_delta::contains(
    object_handle object_value) const noexcept {

    if (!slot_exists(object_value)) {
        return false;
    }

    if (object_value.value() <=
        baseline_object_count) {

        const auto* patch =
            find_object_patch(
                object_value.value());

        return patch == nullptr ||
            patch->live;
    }

    const auto index =
        static_cast<std::size_t>(
            object_value.value() -
            baseline_object_count -
            1);

    return index <
            object_live.size() &&
        object_live[index] != 0;
}

bool graph_delta::contains(
    link_handle link_value) const noexcept {

    if (!slot_exists(link_value)) {
        return false;
    }

    if (link_value.value() <=
        baseline_link_count) {

        const auto* patch =
            find_link_patch(
                link_value.value());

        return patch == nullptr ||
            patch->live;
    }

    const auto index =
        static_cast<std::size_t>(
            link_value.value() -
            baseline_link_count -
            1);

    return index <
            link_live.size() &&
        link_live[index] != 0;
}

bool graph_delta::contains(
    type_ref type_value) const noexcept {

    if (!type_value) {
        return false;
    }

    if (type_value.kind() ==
        type_ref_kind::intrinsic) {

        return type_value.payload() <=
            static_cast<std::uint32_t>(
                intrinsic_type::nullptr_type);
    }

    if (type_value.kind() ==
        type_ref_kind::named) {

        return contains(
            type_handle{
                type_value.payload()});
    }

    if (type_value.kind() !=
        type_ref_kind::derived) {

        return false;
    }

    auto current =
        type_value;

    for (std::size_t depth = 0;
         depth <=
            derived_type_count();
         ++depth) {

        derived_type_record record;

        if (!derived(
                current,
                record)) {

            return false;
        }

        if (record.child.kind() ==
            type_ref_kind::derived) {

            current =
                record.child;

            continue;
        }

        return contains(
            record.child);
    }

    return false;
}

bool graph_delta::type(
    type_handle type_value,
    type_entry& output) const noexcept {

    output = {};

    if (!contains(type_value)) {
        return false;
    }

    if (type_value.value() <=
        baseline_type_count) {

        if (const auto* patch =
                find_type_patch(
                    type_value.value());
            patch != nullptr) {

            output =
                patch->value;

            return patch->live;
        }

        return baseline != nullptr &&
            baseline->type(
                type_value,
                output);
    }

    const auto index =
        static_cast<std::size_t>(
            type_value.value() -
            baseline_type_count -
            1);

    if (index >=
        types.size()) {

        return false;
    }

    output =
        types[index];

    return true;
}

bool graph_delta::object(
    object_handle object_value,
    object_entry& output) const noexcept {

    output = {};

    if (!contains(object_value)) {
        return false;
    }

    if (object_value.value() <=
        baseline_object_count) {

        if (const auto* patch =
                find_object_patch(
                    object_value.value());
            patch != nullptr) {

            output =
                patch->value;

            return patch->live;
        }

        return baseline != nullptr &&
            baseline->object(
                object_value,
                output);
    }

    const auto index =
        static_cast<std::size_t>(
            object_value.value() -
            baseline_object_count -
            1);

    if (index >=
        objects.size()) {

        return false;
    }

    output =
        objects[index];

    return true;
}

bool graph_delta::link(
    link_handle link_value,
    link_record& output) const noexcept {

    output = {};

    if (!contains(link_value)) {
        return false;
    }

    if (link_value.value() <=
        baseline_link_count) {

        if (const auto* patch =
                find_link_patch(
                    link_value.value());
            patch != nullptr) {

            output =
                patch->value;

            return patch->live;
        }

        return baseline != nullptr &&
            baseline->link(
                link_value,
                output);
    }

    const auto index =
        static_cast<std::size_t>(
            link_value.value() -
            baseline_link_count -
            1);

    if (index >=
        links.size()) {

        return false;
    }

    output =
        links[index];

    return true;
}

const type_entry* graph_delta::find(
    type_handle type_value) const noexcept {

    if (!contains(type_value)) {
        return nullptr;
    }

    if (type_value.value() <=
        baseline_type_count) {

        const auto* patch =
            find_type_patch(
                type_value.value());

        return patch != nullptr &&
            patch->live
            ? &patch->value
            : nullptr;
    }

    const auto index =
        static_cast<std::size_t>(
            type_value.value() -
            baseline_type_count -
            1);

    return index <
        types.size()
        ? &types[index]
        : nullptr;
}

const object_entry* graph_delta::find(
    object_handle object_value) const noexcept {

    if (!contains(object_value)) {
        return nullptr;
    }

    if (object_value.value() <=
        baseline_object_count) {

        const auto* patch =
            find_object_patch(
                object_value.value());

        return patch != nullptr &&
            patch->live
            ? &patch->value
            : nullptr;
    }

    const auto index =
        static_cast<std::size_t>(
            object_value.value() -
            baseline_object_count -
            1);

    return index <
        objects.size()
        ? &objects[index]
        : nullptr;
}

const link_record* graph_delta::find(
    link_handle link_value) const noexcept {

    if (!contains(link_value)) {
        return nullptr;
    }

    if (link_value.value() <=
        baseline_link_count) {

        const auto* patch =
            find_link_patch(
                link_value.value());

        return patch != nullptr &&
            patch->live
            ? &patch->value
            : nullptr;
    }

    const auto index =
        static_cast<std::size_t>(
            link_value.value() -
            baseline_link_count -
            1);

    return index <
        links.size()
        ? &links[index]
        : nullptr;
}

type_handle graph_delta::find_type(
    identity_ref identity_value) const noexcept {

    const auto output =
        lineage_type(
            identity_value);

    return contains(output)
        ? output
        : type_handle{};
}

object_handle graph_delta::find_object(
    identity_ref identity_value) const noexcept {

    const auto output =
        lineage_object(
            identity_value);

    return contains(output)
        ? output
        : object_handle{};
}

identity_ref graph_delta::identity(
    type_handle type_value) const noexcept {

    if (!contains(type_value)) {
        return {};
    }

    if (type_value.value() <=
        baseline_type_count) {

        return baseline != nullptr
            ? baseline->identity(
                type_value)
            : identity_ref{};
    }

    const auto index =
        static_cast<std::size_t>(
            type_value.value() -
            baseline_type_count -
            1);

    return index <
        type_identities.size()
        ? type_identities[index]
        : identity_ref{};
}

identity_ref graph_delta::identity(
    object_handle object_value) const noexcept {

    if (!contains(object_value)) {
        return {};
    }

    if (object_value.value() <=
        baseline_object_count) {

        return baseline != nullptr
            ? baseline->identity(
                object_value)
            : identity_ref{};
    }

    const auto index =
        static_cast<std::size_t>(
            object_value.value() -
            baseline_object_count -
            1);

    return index <
        object_identities.size()
        ? object_identities[index]
        : identity_ref{};
}

server_status graph_delta::declare_record(
    identity_ref identity_value,
    graph_record_kind kind,
    type_handle& output) noexcept {

    output = {};

    if (!identity_value ||
        identity_value.kind() !=
            identity_kind::type ||
        !valid_record_kind(kind)) {

        return server_status::
            project_configuration_invalid;
    }

    if (const auto existing =
            lineage_type(
                identity_value);
        existing) {

        if (contains(existing)) {
            type_entry entry;

            if (!type(
                    existing,
                    entry) ||
                entry.kind !=
                    graph_type_kind::record ||
                !compatible_record_kind(
                    entry.record_kind,
                    kind)) {

                return server_status::
                    project_configuration_invalid;
            }

            output = existing;
            return server_status::success;
        }

        if (existing.value() <=
            baseline_type_count) {

            type_patch* patch = nullptr;

            const auto prepared =
                ensure_type_patch(
                    existing,
                    patch);

            if (!succeeded(prepared) ||
                patch == nullptr) {

                return succeeded(prepared)
                    ? server_status::
                        project_artifact_invalid
                    : prepared;
            }

            patch->value = {
                {},
                graph_type_kind::record,
                kind,
                0,
            };

            patch->live = true;
        }
        else {
            const auto index =
                static_cast<std::size_t>(
                    existing.value() -
                    baseline_type_count -
                    1);

            if (index >= types.size() ||
                index >= type_live.size()) {

                return server_status::
                    project_artifact_invalid;
            }

            types[index] = {
                {},
                graph_type_kind::record,
                kind,
                0,
            };

            type_live[index] = 1;
        }

        ++live_type_count_value;

        output = existing;
        return server_status::success;
    }

    if (type_count() >=
        type_handle::maximum_slot) {

        return server_status::io_error;
    }

    const auto old_type_count =
        types.size();

    const auto old_identity_count =
        type_identities.size();

    const auto old_live_count =
        type_live.size();

    try {
        types.push_back({
            {},
            graph_type_kind::record,
            kind,
            0,
        });

        type_identities.push_back(
            identity_value);

        type_live.push_back(1);
    }
    catch (...) {
        types.resize(
            old_type_count);

        type_identities.resize(
            old_identity_count);

        type_live.resize(
            old_live_count);

        return server_status::io_error;
    }

    output = type_handle{
        static_cast<std::uint32_t>(
            type_count())};

    const auto published =
        publish_identity_location(
            identity_value,
            location_kind::type,
            output.value());

    if (!succeeded(published)) {
        types.resize(
            old_type_count);

        type_identities.resize(
            old_identity_count);

        type_live.resize(
            old_live_count);

        output = {};
        return published;
    }

    ++live_type_count_value;

    return server_status::success;
}

server_status graph_delta::clear_definition(
    type_handle type_value) noexcept {

    type_entry entry;

    if (!type(
            type_value,
            entry)) {

        return server_status::
            project_configuration_invalid;
    }

    if (!entry.defined()) {
        return server_status::success;
    }

    entry.members = {};
    entry.flags &=
        static_cast<std::uint16_t>(
            ~graph_type_defined);

    if (type_value.value() <=
        baseline_type_count) {

        type_patch* patch = nullptr;

        const auto prepared =
            ensure_type_patch(
                type_value,
                patch);

        if (!succeeded(prepared) ||
            patch == nullptr) {

            return succeeded(prepared)
                ? server_status::
                    project_artifact_invalid
                : prepared;
        }

        patch->value =
            entry;

        return server_status::success;
    }

    const auto index =
        static_cast<std::size_t>(
            type_value.value() -
            baseline_type_count -
            1);

    if (index >=
        types.size()) {

        return server_status::
            project_artifact_invalid;
    }

    types[index] =
        entry;

    return server_status::success;
}

server_status graph_delta::retire(
    type_handle type_value) noexcept {

    if (!contains(type_value)) {
        return server_status::
            project_configuration_invalid;
    }

    if (type_value.value() <=
        baseline_type_count) {

        type_patch* patch = nullptr;

        const auto prepared =
            ensure_type_patch(
                type_value,
                patch);

        if (!succeeded(prepared) ||
            patch == nullptr) {

            return succeeded(prepared)
                ? server_status::
                    project_artifact_invalid
                : prepared;
        }

        patch->live = false;
    }
    else {
        const auto index =
            static_cast<std::size_t>(
                type_value.value() -
                baseline_type_count -
                1);

        if (index >=
            type_live.size()) {

            return server_status::
                project_artifact_invalid;
        }

        type_live[index] = 0;
    }

    --live_type_count_value;

    return server_status::success;
}

server_status graph_delta::define_record(
    type_handle type_value,
    graph_record_kind kind,
    std::span<const member_record> definition,
    std::span<const construction_value> construction_values) noexcept {

    type_entry entry;

    if (!type(
            type_value,
            entry) ||
        entry.kind !=
            graph_type_kind::record ||
        !valid_record_kind(kind) ||
        !compatible_record_kind(
            entry.record_kind,
            kind) ||
        (!construction_values.empty() &&
         construction_values.size() !=
            definition.size())) {

        return server_status::
            project_configuration_invalid;
    }

    for (std::size_t index = 0;
         index < definition.size();
         ++index) {

        if (!definition[index].name ||
            !contains(
                definition[index].type)) {

            return server_status::
                project_configuration_invalid;
        }

        const auto construction =
            construction_values.empty()
            ? construction_value{}
            : construction_values[index];

        if (!valid_construction(
                construction)) {

            return server_status::
                project_configuration_invalid;
        }

        if (construction.kind ==
            construction_kind::
                member_binding) {

            if (construction.operand >
                    definition.size() ||
                !reference_binding_compatible(
                    definition[index].type,
                    definition[
                        construction.operand - 1].type)) {

                return server_status::
                    project_configuration_invalid;
            }
        }

        if (construction.kind ==
            construction_kind::
                object_binding) {

            object_entry object_value;

            if (!object(
                    object_handle{
                        construction.operand},
                    object_value) ||
                !object_value.
                    internal_static() ||
                !reference_binding_compatible(
                    definition[index].type,
                    object_value.type)) {

                return server_status::
                    project_configuration_invalid;
            }
        }
    }

    if (entry.defined()) {
        if (entry.record_kind !=
            kind ||
            entry.members.count !=
                definition.size()) {

            return server_status::
                project_configuration_invalid;
        }

        for (std::size_t index = 0;
             index < definition.size();
             ++index) {

            const member_index member_value{
                static_cast<std::uint32_t>(
                    index)};

            member_record existing;
            construction_value existing_construction;

            if (!member(
                    type_value,
                    member_value,
                    existing) ||
                !construction(
                    type_value,
                    member_value,
                    existing_construction) ||
                existing.name !=
                    definition[index].name ||
                existing.type !=
                    definition[index].type ||
                existing.access !=
                    definition[index].access) {

                return server_status::
                    project_configuration_invalid;
            }

            const auto expected =
                construction_values.empty()
                ? construction_value{}
                : construction_values[index];

            if (existing_construction !=
                expected) {

                return server_status::
                    project_configuration_invalid;
            }
        }

        return server_status::success;
    }

    const auto maximum =
        static_cast<std::size_t>(
            (std::numeric_limits<
                std::uint32_t>::max)());

    const auto logical_begin =
        member_count();

    if (logical_begin > maximum ||
        definition.size() > maximum ||
        definition.size() >
            maximum - logical_begin) {

        return server_status::io_error;
    }

    const auto old_count =
        member_records.size();

    try {
        member_records.insert(
            member_records.end(),
            definition.begin(),
            definition.end());

        if (construction_values.empty()) {
            member_construction.resize(
                member_records.size());
        }
        else {
            member_construction.insert(
                member_construction.end(),
                construction_values.begin(),
                construction_values.end());
        }
    }
    catch (...) {
        member_records.resize(
            old_count);

        member_construction.resize(
            old_count);

        return server_status::io_error;
    }

    entry.members = {
        static_cast<std::uint32_t>(
            logical_begin),
        static_cast<std::uint32_t>(
            definition.size()),
    };

    entry.record_kind =
        kind;

    entry.flags |=
        graph_type_defined;

    if (type_value.value() <=
        baseline_type_count) {

        type_patch* patch = nullptr;

        const auto prepared =
            ensure_type_patch(
                type_value,
                patch);

        if (!succeeded(prepared) ||
            patch == nullptr) {

            member_records.resize(
                old_count);

            member_construction.resize(
                old_count);

            return succeeded(prepared)
                ? server_status::
                    project_artifact_invalid
                : prepared;
        }

        patch->value =
            entry;

        return server_status::success;
    }

    const auto index =
        static_cast<std::size_t>(
            type_value.value() -
            baseline_type_count -
            1);

    if (index >=
        types.size()) {

        member_records.resize(
            old_count);

        member_construction.resize(
            old_count);

        return server_status::
            project_artifact_invalid;
    }

    types[index] =
        entry;

    return server_status::success;
}

std::span<const member_record> graph_delta::members(
    type_handle type_value) const noexcept {

    const auto* entry =
        find(type_value);

    if (entry == nullptr ||
        !entry->defined() ||
        entry->members.count == 0) {

        return {};
    }

    const auto logical_begin =
        static_cast<std::size_t>(
            entry->members.begin);

    if (logical_begin <
        baseline_member_count) {

        return {};
    }

    const auto begin =
        logical_begin -
        baseline_member_count;

    const auto count =
        static_cast<std::size_t>(
            entry->members.count);

    if (begin >
            member_records.size() ||
        count >
            member_records.size() -
                begin) {

        return {};
    }

    return {
        member_records.data() + begin,
        count,
    };
}

member_index graph_delta::find_member(
    type_handle type_value,
    string_id name) const noexcept {

    if (!name) {
        return {};
    }

    type_entry entry;

    if (!type(
            type_value,
            entry) ||
        !entry.defined()) {

        return {};
    }

    for (std::uint32_t index = 0;
         index <
            entry.members.count;
         ++index) {

        member_record value;

        const member_index member_value{
            index};

        if (member(
                type_value,
                member_value,
                value) &&
            value.name == name) {

            return member_value;
        }
    }

    return {};
}

const member_record* graph_delta::member(
    type_handle type_value,
    member_index member_value) const noexcept {

    if (!member_value) {
        return nullptr;
    }

    const auto values =
        members(
            type_value);

    return member_value.value() <
        values.size()
        ? &values[
            member_value.value()]
        : nullptr;
}

bool graph_delta::member(
    type_handle type_value,
    member_index member_value,
    member_record& output) const noexcept {

    output = {};

    type_entry entry;

    if (!type(
            type_value,
            entry) ||
        !entry.defined() ||
        !member_value ||
        member_value.value() >=
            entry.members.count) {

        return false;
    }

    if (type_value.value() <=
            baseline_type_count &&
        find_type_patch(
            type_value.value()) ==
            nullptr) {

        return baseline != nullptr &&
            baseline->member(
                type_value,
                member_value,
                output);
    }

    const auto logical =
        static_cast<std::size_t>(
            entry.members.begin) +
        member_value.value();

    if (logical <
        baseline_member_count) {

        return false;
    }

    const auto index =
        logical -
        baseline_member_count;

    if (index >=
        member_records.size()) {

        return false;
    }

    output =
        member_records[index];

    return true;
}

const construction_value* graph_delta::construction(
    type_handle type_value,
    member_index member_value) const noexcept {

    const auto* entry =
        find(type_value);

    if (entry == nullptr ||
        !entry->defined() ||
        !member_value ||
        member_value.value() >=
            entry->members.count) {

        return nullptr;
    }

    const auto logical =
        static_cast<std::size_t>(
            entry->members.begin) +
        member_value.value();

    if (logical <
        baseline_member_count) {

        return nullptr;
    }

    const auto index =
        logical -
        baseline_member_count;

    return index <
        member_construction.size()
        ? &member_construction[index]
        : nullptr;
}

bool graph_delta::construction(
    type_handle type_value,
    member_index member_value,
    construction_value& output) const noexcept {

    output = {};

    type_entry entry;

    if (!type(
            type_value,
            entry) ||
        !entry.defined() ||
        !member_value ||
        member_value.value() >=
            entry.members.count) {

        return false;
    }

    if (type_value.value() <=
            baseline_type_count &&
        find_type_patch(
            type_value.value()) ==
            nullptr) {

        return baseline != nullptr &&
            baseline->construction(
                type_value,
                member_value,
                output);
    }

    const auto logical =
        static_cast<std::size_t>(
            entry.members.begin) +
        member_value.value();

    if (logical <
        baseline_member_count) {

        return false;
    }

    const auto index =
        logical -
        baseline_member_count;

    if (index >=
        member_construction.size()) {

        return false;
    }

    output =
        member_construction[index];

    return true;
}

server_status graph_delta::add_object(
    identity_ref identity_value,
    type_ref type_value,
    object_handle& output,
    std::uint32_t flags,
    construction_value initial) noexcept {

    output = {};

    if (!identity_value ||
        identity_value.kind() !=
            identity_kind::object ||
        !contains(type_value) ||
        (flags &
            ~graph_object_flag_mask) !=
            0 ||
        !valid_construction(initial) ||
        (!(flags &
            graph_object_non_default_initializer) &&
         initial != construction_value{}) ||
        initial.kind ==
            construction_kind::
                member_binding ||
        initial.kind ==
            construction_kind::
                object_binding) {

        return server_status::
            project_configuration_invalid;
    }

    if (const auto existing =
            lineage_object(
                identity_value);
        existing) {

        if (contains(existing)) {
            object_entry entry;
            construction_value existing_initial;

            if (!object(
                    existing,
                    entry) ||
                !construction(
                    existing,
                    existing_initial) ||
                entry.type != type_value ||
                (entry.state &
                    graph_object_flag_mask) !=
                    flags ||
                existing_initial !=
                    initial) {

                return server_status::
                    project_configuration_invalid;
            }

            output = existing;
            return server_status::success;
        }

        if (existing.value() <=
            baseline_object_count) {

            object_patch* patch = nullptr;

            const auto prepared =
                ensure_object_patch(
                    existing,
                    patch);

            if (!succeeded(prepared) ||
                patch == nullptr) {

                return succeeded(prepared)
                    ? server_status::
                        project_artifact_invalid
                    : prepared;
            }

            std::uint32_t construction_slot = 0;

            if ((flags &
                graph_object_non_default_initializer) !=
                0) {

                const auto logical_construction_count =
                    baseline_object_construction_count +
                    object_construction.size();

                if (logical_construction_count >=
                    graph_object_construction_slot_mask) {

                    return server_status::
                        io_error;
                }

                try {
                    object_construction.push_back(
                        initial);
                }
                catch (...) {
                    return server_status::
                        io_error;
                }

                construction_slot =
                    static_cast<std::uint32_t>(
                        baseline_object_construction_count +
                        object_construction.size());
            }

            patch->value = {
                type_value,
                flags |
                    construction_slot,
            };

            patch->construction =
                initial;

            patch->live = true;
        }
        else {
            const auto index =
                static_cast<std::size_t>(
                    existing.value() -
                    baseline_object_count -
                    1);

            if (index >=
                    objects.size() ||
                index >=
                    object_live.size()) {

                return server_status::
                    project_artifact_invalid;
            }

            std::uint32_t construction_slot = 0;

            if ((flags &
                graph_object_non_default_initializer) !=
                0) {

                const auto logical_construction_count =
                    baseline_object_construction_count +
                    object_construction.size();

                if (logical_construction_count >=
                    graph_object_construction_slot_mask) {

                    return server_status::
                        io_error;
                }

                try {
                    object_construction.push_back(
                        initial);
                }
                catch (...) {
                    return server_status::
                        io_error;
                }

                construction_slot =
                    static_cast<std::uint32_t>(
                        baseline_object_construction_count +
                        object_construction.size());
            }

            objects[index] = {
                type_value,
                flags |
                    construction_slot,
            };

            object_live[index] = 1;
        }

        ++live_object_count_value;

        output = existing;
        return server_status::success;
    }

    if (object_count() >=
        object_handle::maximum_slot) {

        return server_status::io_error;
    }

    const auto old_object_count =
        objects.size();

    const auto old_identity_count =
        object_identities.size();

    const auto old_live_count =
        object_live.size();

    const auto old_construction_count =
        object_construction.size();

    try {
        std::uint32_t construction_slot = 0;

        if ((flags &
            graph_object_non_default_initializer) !=
            0) {

            const auto logical_construction_count =
                baseline_object_construction_count +
                object_construction.size();

            if (logical_construction_count >=
                graph_object_construction_slot_mask) {

                return server_status::io_error;
            }

            object_construction.push_back(
                initial);

            construction_slot =
                static_cast<std::uint32_t>(
                    baseline_object_construction_count +
                    object_construction.size());
        }

        objects.push_back({
            type_value,
            flags |
                construction_slot,
        });

        object_identities.push_back(
            identity_value);

        object_live.push_back(1);
    }
    catch (...) {
        objects.resize(
            old_object_count);

        object_identities.resize(
            old_identity_count);

        object_live.resize(
            old_live_count);

        object_construction.resize(
            old_construction_count);

        return server_status::io_error;
    }

    output = object_handle{
        static_cast<std::uint32_t>(
            object_count())};

    const auto published =
        publish_identity_location(
            identity_value,
            location_kind::object,
            output.value());

    if (!succeeded(published)) {
        objects.resize(
            old_object_count);

        object_identities.resize(
            old_identity_count);

        object_live.resize(
            old_live_count);

        object_construction.resize(
            old_construction_count);

        output = {};
        return published;
    }

    ++live_object_count_value;

    return server_status::success;
}

server_status graph_delta::retire(
    object_handle object_value) noexcept {

    if (!contains(object_value)) {
        return server_status::
            project_configuration_invalid;
    }

    if (object_value.value() <=
        baseline_object_count) {

        object_patch* patch = nullptr;

        const auto prepared =
            ensure_object_patch(
                object_value,
                patch);

        if (!succeeded(prepared) ||
            patch == nullptr) {

            return succeeded(prepared)
                ? server_status::
                    project_artifact_invalid
                : prepared;
        }

        patch->live = false;
    }
    else {
        const auto index =
            static_cast<std::size_t>(
                object_value.value() -
                baseline_object_count -
                1);

        if (index >=
            object_live.size()) {

            return server_status::
                project_artifact_invalid;
        }

        object_live[index] = 0;
    }

    --live_object_count_value;

    return server_status::success;
}

bool graph_delta::construction(
    object_handle object_value,
    construction_value& output) const noexcept {

    output = {};

    if (!contains(object_value)) {
        return false;
    }

    if (object_value.value() <=
        baseline_object_count) {

        if (const auto* patch =
                find_object_patch(
                    object_value.value());
            patch != nullptr) {

            if (!patch->live) {
                return false;
            }

            output =
                patch->construction;

            return true;
        }

        return baseline != nullptr &&
            baseline->construction(
                object_value,
                output);
    }

    const auto index =
        static_cast<std::size_t>(
            object_value.value() -
            baseline_object_count -
            1);

    if (index >=
        objects.size()) {

        return false;
    }

    const auto& entry =
        objects[index];

    if (!entry.
        non_default_initializer()) {

        return entry.
            construction_slot() == 0;
    }

    const auto slot =
        static_cast<std::size_t>(
            entry.construction_slot());

    if (slot <=
            baseline_object_construction_count ||
        slot >
            baseline_object_construction_count +
                object_construction.size()) {

        return false;
    }

    output =
        object_construction[
            slot -
            baseline_object_construction_count -
            1];

    return true;
}

type_ref graph_delta::intrinsic(
    intrinsic_type type_value) const noexcept {

    return valid_intrinsic(
        type_value)
        ? type_ref::make(
            type_ref_kind::intrinsic,
            static_cast<std::uint32_t>(
                type_value))
        : type_ref{};
}

type_ref graph_delta::named(
    type_handle type_value) const noexcept {

    return contains(type_value)
        ? type_ref::make(
            type_ref_kind::named,
            type_value.value())
        : type_ref{};
}

std::uint64_t graph_delta::hash_derived(
    type_ref child,
    derived_type_kind kind,
    std::uint64_t payload) noexcept {

    std::uint64_t hash =
        1469598103934665603ull;

    const auto mix =
        [&hash](std::uint64_t value) noexcept {
            for (std::size_t index = 0;
                 index < 8;
                 ++index) {

                hash ^=
                    static_cast<std::uint8_t>(
                        value & 0xffu);

                hash *=
                    1099511628211ull;

                value >>= 8;
            }
        };

    mix(child.value());
    mix(
        static_cast<std::uint8_t>(
            kind));
    mix(payload);

    return hash == 0
        ? 1
        : hash;
}

std::uint32_t graph_delta::fingerprint(
    std::uint64_t hash) noexcept {

    const auto folded =
        static_cast<std::uint32_t>(
            hash ^
            (hash >> 32));

    return folded == 0
        ? 1
        : folded;
}

type_ref graph_delta::find_derived(
    type_ref child,
    derived_type_kind kind,
    std::uint64_t payload,
    std::uint64_t hash,
    std::uint32_t fingerprint_value) const noexcept {

    if (derived_index.empty()) {
        return {};
    }

    const auto mask =
        derived_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    for (std::size_t probe = 0;
         probe < derived_index.size();
         ++probe) {

        const auto& slot =
            derived_index[position];

        if (!slot.type) {
            return {};
        }

        if (slot.fingerprint ==
                fingerprint_value &&
            slot.type.kind() ==
                type_ref_kind::derived &&
            slot.type.payload() >
                baseline_derived_count &&
            slot.type.payload() <=
                derived_type_count()) {

            const auto index =
                static_cast<std::size_t>(
                    slot.type.payload() -
                    baseline_derived_count -
                    1);

            if (index <
                derived_types.size()) {

                const auto& record =
                    derived_types[index];

                if (record.child == child &&
                    record.kind == kind &&
                    record.payload ==
                        payload) {

                    return slot.type;
                }
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

void graph_delta::insert_derived_index(
    std::vector<derived_index_slot>& target,
    type_ref type_value,
    std::uint64_t hash,
    std::uint32_t fingerprint_value) const noexcept {

    const auto mask =
        target.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    while (target[position].type) {
        position =
            (position + 1) &
            mask;
    }

    target[position] = {
        fingerprint_value,
        type_value,
    };
}

server_status graph_delta::ensure_derived_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<
            std::size_t>::max)() -
            derived_types.size()) {

        return server_status::io_error;
    }

    const auto required =
        derived_types.size() +
        additional;

    if (!derived_index.empty() &&
        required <=
            derived_index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(
            required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<derived_index_slot>
            candidate(capacity);

        for (std::size_t index = 0;
             index <
                derived_types.size();
             ++index) {

            const auto type_value =
                type_ref::make(
                    type_ref_kind::derived,
                    static_cast<std::uint32_t>(
                        baseline_derived_count +
                        index +
                        1));

            const auto& record =
                derived_types[index];

            const auto hash =
                hash_derived(
                    record.child,
                    record.kind,
                    record.payload);

            insert_derived_index(
                candidate,
                type_value,
                hash,
                fingerprint(hash));
        }

        derived_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

server_status graph_delta::derive(
    type_ref child,
    derived_type_kind kind,
    std::uint64_t payload,
    type_ref& output) noexcept {

    output = {};

    if (!contains(child) ||
        !valid_derived_kind(kind)) {

        return server_status::
            project_configuration_invalid;
    }

    const auto hash =
        hash_derived(
            child,
            kind,
            payload);

    const auto fingerprint_value =
        fingerprint(hash);

    if (const auto existing =
            find_derived(
                child,
                kind,
                payload,
                hash,
                fingerprint_value);
        existing) {

        output = existing;
        return server_status::success;
    }

    if (baseline != nullptr) {
        const auto existing =
            baseline->find_derived(
                child,
                kind,
                payload);

        if (existing) {
            output = existing;
            return server_status::success;
        }
    }

    if (derived_type_count() >=
        type_ref::maximum_payload) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_derived_index_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto old_count =
        derived_types.size();

    try {
        derived_types.push_back({
            payload,
            child,
            kind,
            {},
        });

        output =
            type_ref::make(
                type_ref_kind::derived,
                static_cast<std::uint32_t>(
                    derived_type_count()));

        insert_derived_index(
            derived_index,
            output,
            hash,
            fingerprint_value);

        return server_status::success;
    }
    catch (...) {
        derived_types.resize(
            old_count);

        output = {};
        return server_status::io_error;
    }
}

bool graph_delta::intrinsic(
    type_ref type_value,
    intrinsic_type& output) const noexcept {

    output =
        intrinsic_type::none;

    if (!contains(type_value) ||
        type_value.kind() !=
            type_ref_kind::intrinsic) {

        return false;
    }

    output =
        static_cast<intrinsic_type>(
            type_value.payload());

    return valid_intrinsic(
        output);
}

bool graph_delta::named(
    type_ref type_value,
    type_handle& output) const noexcept {

    output = {};

    if (!contains(type_value) ||
        type_value.kind() !=
            type_ref_kind::named) {

        return false;
    }

    output =
        type_handle{
            type_value.payload()};

    return contains(output);
}

bool graph_delta::derived(
    type_ref type_value,
    derived_type_record& output) const noexcept {

    output = {};

    if (!type_value ||
        type_value.kind() !=
            type_ref_kind::derived ||
        type_value.payload() == 0 ||
        type_value.payload() >
            derived_type_count()) {

        return false;
    }

    if (type_value.payload() <=
        baseline_derived_count) {

        return baseline != nullptr &&
            baseline->derived(
                type_value,
                output);
    }

    const auto index =
        static_cast<std::size_t>(
            type_value.payload() -
            baseline_derived_count -
            1);

    if (index >=
        derived_types.size()) {

        return false;
    }

    output =
        derived_types[index];

    return true;
}

std::uint64_t graph_delta::link_target_key(
    object_endpoint target) noexcept {

    if (!target.object ||
        !target.member) {

        return 0;
    }

    return
        (static_cast<std::uint64_t>(
             target.object.value()) << 32) |
        (static_cast<std::uint64_t>(
             target.member.value()) +
         1);
}

server_status graph_delta::ensure_link_target_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<
            std::size_t>::max)() -
            link_target_index_count) {

        return server_status::io_error;
    }

    const auto required =
        link_target_index_count +
        additional;

    if (!link_target_index.empty() &&
        required <=
            link_target_index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(
            required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<link_target_index_slot>
            candidate(capacity);

        for (const auto& value :
             link_target_index) {

            if (value.key == 0) {
                continue;
            }

            insert_link_target_index(
                candidate,
                value.key,
                value.link);
        }

        link_target_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

void graph_delta::insert_link_target_index(
    std::vector<link_target_index_slot>& target,
    std::uint64_t key,
    link_handle link_value) const noexcept {

    const auto mask =
        target.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix64(key)) &
        mask;

    while (target[position].key != 0) {
        position =
            (position + 1) &
            mask;
    }

    target[position] = {
        key,
        link_value,
    };
}

link_handle graph_delta::find_local_link_target(
    object_endpoint target) const noexcept {

    const auto key =
        link_target_key(
            target);

    if (key == 0 ||
        link_target_index.empty()) {

        return {};
    }

    const auto mask =
        link_target_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix64(key)) &
        mask;

    for (std::size_t probe = 0;
         probe <
            link_target_index.size();
         ++probe) {

        const auto& slot =
            link_target_index[
                position];

        if (slot.key == 0) {
            return {};
        }

        if (slot.key == key) {
            return slot.link;
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

link_handle graph_delta::lineage_link_target(
    object_endpoint target) const noexcept {

    if (const auto local =
            find_local_link_target(
                target);
        local) {

        return local;
    }

    return baseline != nullptr
        ? baseline->find_link_target(
            target)
        : link_handle{};
}

bool graph_delta::reference_binding_compatible(
    type_ref target,
    type_ref source) const noexcept {

    derived_type_record target_type;

    if (!derived(
            target,
            target_type) ||
        (target_type.kind !=
             derived_type_kind::lvalue_reference &&
         target_type.kind !=
             derived_type_kind::rvalue_reference)) {

        return false;
    }

    derived_type_record source_type;

    if (derived(
            source,
            source_type) &&
        (source_type.kind ==
             derived_type_kind::lvalue_reference ||
         source_type.kind ==
             derived_type_kind::rvalue_reference)) {

        source = source_type.child;
    }

    return target_type.child ==
        source;
}

bool graph_delta::endpoint_type(
    object_endpoint endpoint,
    type_ref& output) const noexcept {

    output = {};

    object_entry object_value;

    if (!object(
            endpoint.object,
            object_value) ||
        !endpoint.member) {

        return false;
    }

    type_handle type_value;

    if (!named(
            object_value.type,
            type_value)) {

        return false;
    }

    member_record value;

    if (!member(
            type_value,
            endpoint.member,
            value)) {

        return false;
    }

    output = value.type;
    return static_cast<bool>(output);
}

server_status graph_delta::add_link(
    object_endpoint source,
    object_endpoint target,
    link_handle& output) noexcept {

    output = {};

    type_ref source_type;
    type_ref target_type;

    if (!endpoint_type(
            source,
            source_type) ||
        !endpoint_type(
            target,
            target_type) ||
        !reference_binding_compatible(
            target_type,
            source_type)) {

        return server_status::
            project_configuration_invalid;
    }

    if (const auto existing =
            lineage_link_target(
                target);
        existing) {

        if (contains(existing)) {
            link_record value;

            if (!link(
                    existing,
                    value) ||
                value.target !=
                    target ||
                value.source !=
                    source) {

                return server_status::
                    project_configuration_invalid;
            }

            output = existing;
            return server_status::success;
        }

        if (existing.value() <=
            baseline_link_count) {

            link_patch* patch = nullptr;

            const auto prepared =
                ensure_link_patch(
                    existing,
                    patch);

            if (!succeeded(prepared) ||
                patch == nullptr) {

                return succeeded(prepared)
                    ? server_status::
                        project_artifact_invalid
                    : prepared;
            }

            patch->value = {
                source,
                target,
            };

            patch->live = true;
        }
        else {
            const auto index =
                static_cast<std::size_t>(
                    existing.value() -
                    baseline_link_count -
                    1);

            if (index >=
                    links.size() ||
                index >=
                    link_live.size()) {

                return server_status::
                    project_artifact_invalid;
            }

            links[index] = {
                source,
                target,
            };

            link_live[index] = 1;
        }

        ++live_link_count_value;

        output = existing;
        return server_status::success;
    }

    if (link_count() >=
        static_cast<std::size_t>(
            link_handle::maximum_slot)) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_link_target_index_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto old_link_count =
        links.size();

    const auto old_live_count =
        link_live.size();

    try {
        links.push_back({
            source,
            target,
        });

        link_live.push_back(1);
    }
    catch (...) {
        links.resize(
            old_link_count);

        link_live.resize(
            old_live_count);

        return server_status::io_error;
    }

    output = link_handle{
        static_cast<std::uint32_t>(
            link_count())};

    const auto key =
        link_target_key(
            target);

    if (key == 0) {
        links.resize(
            old_link_count);

        link_live.resize(
            old_live_count);

        output = {};

        return server_status::
            project_configuration_invalid;
    }

    insert_link_target_index(
        link_target_index,
        key,
        output);

    ++link_target_index_count;
    ++live_link_count_value;

    return server_status::success;
}

server_status graph_delta::retire(
    link_handle link_value) noexcept {

    if (!contains(link_value)) {
        return server_status::
            project_configuration_invalid;
    }

    if (link_value.value() <=
        baseline_link_count) {

        link_patch* patch = nullptr;

        const auto prepared =
            ensure_link_patch(
                link_value,
                patch);

        if (!succeeded(prepared) ||
            patch == nullptr) {

            return succeeded(prepared)
                ? server_status::
                    project_artifact_invalid
                : prepared;
        }

        patch->live = false;
    }
    else {
        const auto index =
            static_cast<std::size_t>(
                link_value.value() -
                baseline_link_count -
                1);

        if (index >=
            link_live.size()) {

            return server_status::
                project_artifact_invalid;
        }

        link_live[index] = 0;
    }

    --live_link_count_value;

    return server_status::success;
}

}
