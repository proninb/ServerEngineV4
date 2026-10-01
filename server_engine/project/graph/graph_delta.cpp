#include "graph_delta.hpp"
#include "construction_semantics.hpp"
#include "../persistence/compiled_project.hpp"

#include <bit>
#include <limits>
#include <type_traits>
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

[[nodiscard]] bool valid_member_access(
    graph_member_access access) noexcept {

    return access ==
            graph_member_access::public_access ||
        access ==
            graph_member_access::protected_access ||
        access ==
            graph_member_access::private_access;
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
        !base_records.empty() ||
        !member_records.empty() ||
        !member_construction.empty() ||
        !objects.empty() ||
        !object_identities.empty() ||
        !object_construction.empty() ||
        !links.empty() ||
        !initialization_patches.empty() ||
        !initialization_target_index.empty() ||
        !derived_types.empty() ||
        !endpoint_paths.empty() ||
        !endpoint_path_steps.empty() ||
        !endpoint_path_index.empty()) {

        return server_status::
            project_configuration_invalid;
    }

    baseline = &value;

    baseline_type_count =
        value.type_slot_count();

    baseline_base_count =
        value.base_count();

    baseline_member_count =
        value.member_count();

    baseline_object_count =
        value.object_slot_count();

    baseline_object_construction_count =
        value.object_construction_count();

    baseline_link_count =
        value.link_slot_count();

    baseline_derived_count =
        value.derived_type_count();

    baseline_endpoint_path_count =
        value.endpoint_path_count();

    baseline_endpoint_path_step_count =
        value.endpoint_path_step_count();

    live_type_count_value =
        value.live_type_count();

    live_object_count_value =
        value.live_object_count();

    live_link_count_value =
        value.live_link_count();

    live_initialization_count_value =
        value.initialization_count();

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
                baseline->find_type_lineage(
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
                baseline->find_object_lineage(
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

    if (!baseline->type_raw(
            type_value,
            value)) {

        return server_status::
            project_artifact_invalid;
    }

    const bool live =
        baseline->type_slot_live(
            type_value);

    try {
        type_patches.push_back({
            type_value.value(),
            value,
            live,
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

    const bool live =
        baseline->object_slot_live(
            object_value);

    if (!baseline->object_raw(
            object_value,
            value) ||
        (live &&
         !baseline->construction(
             object_value,
             construction))) {

        return server_status::
            project_artifact_invalid;
    }

    try {
        object_patches.push_back({
            object_value.value(),
            value,
            construction,
            live,
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

    if (!baseline->link_raw(
            link_value,
            value)) {

        return server_status::
            project_artifact_invalid;
    }

    const bool live =
        baseline->link_slot_live(
            link_value);

    try {
        link_patches.push_back({
            link_value.value(),
            value,
            live,
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

        return patch != nullptr
            ? patch->live
            : baseline != nullptr &&
                baseline->type_slot_live(
                    type_value);
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

        return patch != nullptr
            ? patch->live
            : baseline != nullptr &&
                baseline->object_slot_live(
                    object_value);
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

        return patch != nullptr
            ? patch->live
            : baseline != nullptr &&
                baseline->link_slot_live(
                    link_value);
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

        return static_cast<bool>(
            find_type(
                identity_ref::make(
                    type_value.payload(),
                    identity_kind::type)));
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
    entry.bases = {};
    entry.flags &=
        static_cast<std::uint16_t>(
            ~graph_type_flag_mask);

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

bool graph_delta::base(
    type_handle type_value,
    std::uint32_t local_base,
    base_record& output) const noexcept {

    output = {};

    type_entry entry;

    if (!type(
            type_value,
            entry) ||
        !entry.defined() ||
        local_base >=
            entry.bases.count) {

        return false;
    }

    const auto logical =
        static_cast<std::size_t>(
            entry.bases.begin) +
        local_base;

    if (type_value.value() <=
            baseline_type_count &&
        find_type_patch(
            type_value.value()) ==
            nullptr) {

        return baseline != nullptr &&
            baseline->base_at(
                logical,
                output);
    }

    if (logical <
        baseline_base_count) {

        return false;
    }

    const auto index =
        logical -
        baseline_base_count;

    if (index >=
        base_records.size()) {

        return false;
    }

    output =
        base_records[index];

    return true;
}

bool graph_delta::polymorphic(
    type_handle type_value) const noexcept {

    type_entry entry;

    return type(
               type_value,
               entry) &&
        entry.defined() &&
        entry.polymorphic();
}

server_status graph_delta::define_record(
    type_handle type_value,
    graph_record_kind kind,
    std::span<const member_record> definition,
    std::span<const construction_value> construction_values,
    std::span<const base_record> bases_value,
    bool declares_virtual) noexcept {

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
            definition.size()) ||
        (kind ==
             graph_record_kind::union_type &&
         (!bases_value.empty() ||
          declares_virtual))) {

        return server_status::
            project_configuration_invalid;
    }

    bool polymorphic_value =
        declares_virtual;

    for (std::size_t index = 0;
         index < bases_value.size();
         ++index) {

        const auto& base_value =
            bases_value[index];

        type_entry base_type;

        if (!base_value.type ||
            base_value.type ==
                type_value ||
            !type(
                base_value.type,
                base_type) ||
            !base_type.defined() ||
            base_type.kind !=
                graph_type_kind::record ||
            base_type.record_kind ==
                graph_record_kind::union_type ||
            !valid_member_access(
                base_value.access) ||
            (base_value.flags &
                ~graph_base_flag_mask) != 0 ||
            base_value.reserved != 0) {

            return server_status::
                project_configuration_invalid;
        }

        for (std::size_t previous = 0;
             previous < index;
             ++previous) {

            if (bases_value[previous].type ==
                base_value.type) {

                return server_status::
                    project_configuration_invalid;
            }
        }

        polymorphic_value =
            polymorphic_value ||
            base_type.polymorphic();
    }

    for (std::size_t index = 0;
         index < definition.size();
         ++index) {

        if (!definition[index].name ||
            !contains(
                definition[index].type) ||
            !valid_member_access(
                definition[index].access)) {

            return server_status::
                project_configuration_invalid;
        }

        const auto construction =
            construction_values.empty()
            ? construction_value{}
            : construction_values[index];

        if (!valid_construction(
                construction) ||
            !construction_compatible(
                *this,
                definition[index].type,
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
            entry.polymorphic() !=
                polymorphic_value ||
            entry.bases.count !=
                bases_value.size() ||
            entry.members.count !=
                definition.size()) {

            return server_status::
                project_configuration_invalid;
        }

        for (std::size_t index = 0;
             index < bases_value.size();
             ++index) {

            base_record existing;

            if (!base(
                    type_value,
                    static_cast<std::uint32_t>(
                        index),
                    existing) ||
                existing.type !=
                    bases_value[index].type ||
                existing.access !=
                    bases_value[index].access ||
                existing.flags !=
                    bases_value[index].flags) {

                return server_status::
                    project_configuration_invalid;
            }
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

    const auto logical_member_begin =
        member_count();

    const auto logical_base_begin =
        base_count();

    if (logical_member_begin > maximum ||
        definition.size() > maximum ||
        definition.size() >
            maximum -
                logical_member_begin ||
        logical_base_begin > maximum ||
        bases_value.size() > maximum ||
        bases_value.size() >
            maximum -
                logical_base_begin) {

        return server_status::io_error;
    }

    const auto old_member_count =
        member_records.size();

    const auto old_base_count =
        base_records.size();

    try {
        base_records.insert(
            base_records.end(),
            bases_value.begin(),
            bases_value.end());

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
        base_records.resize(
            old_base_count);

        member_records.resize(
            old_member_count);

        member_construction.resize(
            old_member_count);

        return server_status::io_error;
    }

    entry.bases = {
        static_cast<std::uint32_t>(
            logical_base_begin),
        static_cast<std::uint32_t>(
            bases_value.size()),
    };

    entry.members = {
        static_cast<std::uint32_t>(
            logical_member_begin),
        static_cast<std::uint32_t>(
            definition.size()),
    };

    entry.record_kind =
        kind;

    entry.flags &=
        static_cast<std::uint16_t>(
            ~graph_type_flag_mask);

    entry.flags |=
        graph_type_defined;

    if (polymorphic_value) {
        entry.flags |=
            graph_type_polymorphic;
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

            base_records.resize(
                old_base_count);

            member_records.resize(
                old_member_count);

            member_construction.resize(
                old_member_count);

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

        base_records.resize(
            old_base_count);

        member_records.resize(
            old_member_count);

        member_construction.resize(
            old_member_count);

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

bool graph_delta::member(
    type_handle type_value,
    std::uint32_t local_member,
    member_record& output) const noexcept {

    const auto value =
        std::bit_cast<member_index>(
            local_member);

    return member(
        type_value,
        value,
        output);
}

bool graph_delta::construction(
    type_handle type_value,
    std::uint32_t local_member,
    construction_value& output) const noexcept {

    const auto value =
        std::bit_cast<member_index>(
            local_member);

    return construction(
        type_value,
        value,
        output);
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
        !construction_compatible(
            *this,
            type_value,
            initial) ||
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

    const auto value =
        identity(type_value);

    return value &&
        value.kind() ==
            identity_kind::type
        ? type_ref::make(
            type_ref_kind::named,
            value.slot())
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

    if (!type_value ||
        type_value.kind() !=
            type_ref_kind::named) {

        return false;
    }

    output =
        find_type(
            identity_ref::make(
                type_value.payload(),
                identity_kind::type));

    return static_cast<bool>(
        output);
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

std::uint64_t graph_delta::hash_endpoint_path(
    type_ref root_type,
    std::span<const endpoint_path_step> steps) noexcept {

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

    mix(root_type.value());

    for (const auto& step :
         steps) {

        mix(static_cast<std::uint8_t>(
            step.kind));

        mix(step.value);
    }

    return hash == 0
        ? 1
        : hash;
}

bool graph_delta::resolve_endpoint_path(
    type_ref root_type,
    std::span<const endpoint_path_step> steps,
    type_ref& output) const noexcept {

    output = {};

    if (!contains(
            root_type) ||
        steps.empty()) {

        return false;
    }

    auto current_type =
        root_type;

    for (const auto& step :
         steps) {

        for (const auto reserved :
             step.reserved) {

            if (reserved != 0) {
                return false;
            }
        }

        derived_type_record derived_value;

        while (derived(
                   current_type,
                   derived_value) &&
               (derived_value.kind ==
                    derived_type_kind::
                        const_qualified ||
                derived_value.kind ==
                    derived_type_kind::
                        volatile_qualified)) {

            current_type =
                derived_value.child;
        }

        switch (step.kind) {
        case endpoint_path_step_kind::member: {
            if (step.value >
                (std::numeric_limits<
                    std::uint32_t>::max)()) {

                return false;
            }

            type_handle record;

            if (!named(
                    current_type,
                    record)) {

                return false;
            }

            member_record value;

            const member_index member_value{
                static_cast<std::uint32_t>(
                    step.value)};

            if (!member(
                    record,
                    member_value,
                    value)) {

                return false;
            }

            current_type =
                value.type;

            break;
        }

        case endpoint_path_step_kind::array_index:
            if (!derived(
                    current_type,
                    derived_value) ||
                derived_value.kind !=
                    derived_type_kind::
                        bounded_array ||
                step.value >=
                    derived_value.payload) {

                return false;
            }

            current_type =
                derived_value.child;

            break;

        default:
            return false;
        }
    }

    output =
        current_type;

    return contains(output);
}

endpoint_path_handle
graph_delta::find_local_endpoint_path(
    type_ref root_type,
    std::span<const endpoint_path_step> steps,
    std::uint64_t hash,
    std::uint32_t fingerprint_value) const noexcept {

    if (endpoint_path_index.empty()) {
        return {};
    }

    const auto mask =
        endpoint_path_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    for (std::size_t probe = 0;
         probe <
            endpoint_path_index.size();
         ++probe) {

        const auto& slot =
            endpoint_path_index[
                position];

        if (!slot.path) {
            return {};
        }

        if (slot.fingerprint ==
                fingerprint_value &&
            slot.path.value() >
                baseline_endpoint_path_count) {

            const auto local =
                static_cast<std::size_t>(
                    slot.path.value() -
                    baseline_endpoint_path_count -
                    1);

            if (local <
                endpoint_paths.size()) {

                const auto& record =
                    endpoint_paths[local];

                if (record.root_type ==
                        root_type &&
                    record.steps.count ==
                        steps.size() &&
                    record.steps.begin >=
                        baseline_endpoint_path_step_count) {

                    const auto local_begin =
                        static_cast<std::size_t>(
                            record.steps.begin -
                            baseline_endpoint_path_step_count);

                    if (local_begin <=
                            endpoint_path_steps.size() &&
                        record.steps.count <=
                            endpoint_path_steps.size() -
                                local_begin) {

                        bool equal = true;

                        for (std::size_t index = 0;
                             index < steps.size();
                             ++index) {

                            if (endpoint_path_steps[
                                    local_begin +
                                    index] !=
                                steps[index]) {

                                equal = false;
                                break;
                            }
                        }

                        if (equal) {
                            return slot.path;
                        }
                    }
                }
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

server_status
graph_delta::ensure_endpoint_path_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<
            std::size_t>::max)() -
            endpoint_paths.size()) {

        return server_status::io_error;
    }

    const auto required =
        endpoint_paths.size() +
        additional;

    if (!endpoint_path_index.empty() &&
        required <=
            endpoint_path_index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(
            required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<endpoint_path_index_slot>
            candidate(capacity);

        for (std::size_t index = 0;
             index <
                endpoint_paths.size();
             ++index) {

            const auto& record =
                endpoint_paths[index];

            if (record.steps.begin <
                baseline_endpoint_path_step_count) {

                return server_status::
                    project_artifact_invalid;
            }

            const auto local_begin =
                static_cast<std::size_t>(
                    record.steps.begin -
                    baseline_endpoint_path_step_count);

            if (local_begin >
                    endpoint_path_steps.size() ||
                record.steps.count >
                    endpoint_path_steps.size() -
                        local_begin) {

                return server_status::
                    project_artifact_invalid;
            }

            const auto steps =
                std::span<const endpoint_path_step>{
                    endpoint_path_steps.data() +
                        local_begin,
                    record.steps.count};

            const auto hash =
                hash_endpoint_path(
                    record.root_type,
                    steps);

            const endpoint_path_handle path{
                static_cast<std::uint32_t>(
                    baseline_endpoint_path_count +
                    index +
                    1)};

            insert_endpoint_path_index(
                candidate,
                path,
                hash,
                fingerprint(hash));
        }

        endpoint_path_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

void graph_delta::insert_endpoint_path_index(
    std::vector<endpoint_path_index_slot>& target,
    endpoint_path_handle path,
    std::uint64_t hash,
    std::uint32_t fingerprint_value) const noexcept {

    const auto mask =
        target.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    while (target[position].path) {
        position =
            (position + 1) &
            mask;
    }

    target[position] = {
        fingerprint_value,
        path,
    };
}

bool graph_delta::endpoint_path(
    endpoint_path_handle path,
    endpoint_path_record& output) const noexcept {

    output = {};

    if (!path ||
        path.value() >
            endpoint_path_count()) {

        return false;
    }

    if (path.value() <=
        baseline_endpoint_path_count) {

        return baseline != nullptr &&
            baseline->endpoint_path(
                path,
                output);
    }

    const auto index =
        static_cast<std::size_t>(
            path.value() -
            baseline_endpoint_path_count -
            1);

    if (index >=
        endpoint_paths.size()) {

        return false;
    }

    output =
        endpoint_paths[index];

    return true;
}

bool graph_delta::endpoint_path_step_at(
    std::size_t index,
    endpoint_path_step& output) const noexcept {

    output = {};

    if (index <
        baseline_endpoint_path_step_count) {

        return baseline != nullptr &&
            baseline->endpoint_path_step_at(
                index,
                output);
    }

    const auto local =
        index -
        baseline_endpoint_path_step_count;

    if (local >=
        endpoint_path_steps.size()) {

        return false;
    }

    output =
        endpoint_path_steps[local];

    return true;
}

server_status graph_delta::intern_endpoint_path(
    type_ref root_type,
    std::span<const endpoint_path_step> steps,
    endpoint_path_handle& output,
    type_ref* value_type) noexcept {

    output = {};

    if (value_type != nullptr) {
        *value_type = {};
    }

    type_ref resolved;

    if (!resolve_endpoint_path(
            root_type,
            steps,
            resolved)) {

        return server_status::
            project_configuration_invalid;
    }

    if (baseline != nullptr) {
        const auto existing =
            baseline->find_endpoint_path(
                root_type,
                steps);

        if (existing) {
            endpoint_path_record record;

            if (!baseline->endpoint_path(
                    existing,
                    record) ||
                record.value_type !=
                    resolved) {

                return server_status::
                    project_artifact_invalid;
            }

            output =
                existing;

            if (value_type != nullptr) {
                *value_type =
                    resolved;
            }

            return server_status::success;
        }
    }

    const auto hash =
        hash_endpoint_path(
            root_type,
            steps);

    const auto fingerprint_value =
        fingerprint(
            hash);

    if (const auto existing =
            find_local_endpoint_path(
                root_type,
                steps,
                hash,
                fingerprint_value);
        existing) {

        output =
            existing;

        if (value_type != nullptr) {
            *value_type =
                resolved;
        }

        return server_status::success;
    }

    if (endpoint_path_count() >=
            endpoint_path_handle::
                maximum_slot ||
        steps.size() >
            (std::numeric_limits<
                std::uint32_t>::max)() ||
        endpoint_path_step_count() >
            (std::numeric_limits<
                std::uint32_t>::max)() -
                steps.size()) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_endpoint_path_index_capacity(
            1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto old_path_count =
        endpoint_paths.size();

    const auto old_step_count =
        endpoint_path_steps.size();

    try {
        endpoint_path_steps.insert(
            endpoint_path_steps.end(),
            steps.begin(),
            steps.end());

        endpoint_paths.push_back({
            {
                static_cast<std::uint32_t>(
                    baseline_endpoint_path_step_count +
                    old_step_count),
                static_cast<std::uint32_t>(
                    steps.size()),
            },
            root_type,
            resolved,
        });

        output =
            endpoint_path_handle{
                static_cast<std::uint32_t>(
                    baseline_endpoint_path_count +
                    endpoint_paths.size())};

        insert_endpoint_path_index(
            endpoint_path_index,
            output,
            hash,
            fingerprint_value);

        if (value_type != nullptr) {
            *value_type =
                resolved;
        }

        return server_status::success;
    }
    catch (...) {
        endpoint_paths.resize(
            old_path_count);

        endpoint_path_steps.resize(
            old_step_count);

        output = {};

        if (value_type != nullptr) {
            *value_type = {};
        }

        return server_status::io_error;
    }
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
        ? baseline->find_link_target_lineage(
            target)
        : link_handle{};
}

const graph_delta::initialization_patch*
graph_delta::find_initialization_patch(
    object_endpoint target) const noexcept {

    const auto key =
        link_target_key(
            target);

    if (key == 0 ||
        initialization_target_index.empty()) {

        return nullptr;
    }

    const auto mask =
        initialization_target_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix64(key)) &
        mask;

    for (std::size_t probe = 0;
         probe <
            initialization_target_index.size();
         ++probe) {

        const auto& slot =
            initialization_target_index[
                position];

        if (slot.key == 0) {
            return nullptr;
        }

        if (slot.key == key) {
            return slot.patch != 0 &&
                slot.patch <=
                    initialization_patches.size()
                ? &initialization_patches[
                    slot.patch - 1]
                : nullptr;
        }

        position =
            (position + 1) &
            mask;
    }

    return nullptr;
}

graph_delta::initialization_patch*
graph_delta::find_initialization_patch(
    object_endpoint target) noexcept {

    const auto key =
        link_target_key(
            target);

    if (key == 0 ||
        initialization_target_index.empty()) {

        return nullptr;
    }

    const auto mask =
        initialization_target_index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            mix64(key)) &
        mask;

    for (std::size_t probe = 0;
         probe <
            initialization_target_index.size();
         ++probe) {

        const auto& slot =
            initialization_target_index[
                position];

        if (slot.key == 0) {
            return nullptr;
        }

        if (slot.key == key) {
            return slot.patch != 0 &&
                slot.patch <=
                    initialization_patches.size()
                ? &initialization_patches[
                    slot.patch - 1]
                : nullptr;
        }

        position =
            (position + 1) &
            mask;
    }

    return nullptr;
}

server_status
graph_delta::ensure_initialization_target_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<
            std::size_t>::max)() -
            initialization_patches.size()) {

        return server_status::io_error;
    }

    const auto required =
        initialization_patches.size() +
        additional;

    if (!initialization_target_index.empty() &&
        required <=
            initialization_target_index.size() / 2) {

        return server_status::success;
    }

    const auto capacity =
        next_index_capacity(
            required);

    if (capacity == 0) {
        return server_status::io_error;
    }

    try {
        std::vector<initialization_target_index_slot>
            candidate(
                capacity);

        for (const auto& value :
             initialization_target_index) {

            if (value.key == 0) {
                continue;
            }

            if (value.patch == 0 ||
                value.patch >
                    initialization_patches.size()) {

                return server_status::
                    project_artifact_invalid;
            }

            insert_initialization_target_index(
                candidate,
                value.key,
                value.patch);
        }

        initialization_target_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

void graph_delta::insert_initialization_target_index(
    std::vector<initialization_target_index_slot>& target,
    std::uint64_t key,
    std::uint32_t patch) const noexcept {

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
        patch,
        0,
    };
}

bool graph_delta::scalar_initialization_target(
    type_ref type_value) const noexcept {

    derived_type_record derived_value;

    for (;;) {
        if (!derived(
                type_value,
                derived_value)) {

            break;
        }

        if (derived_value.kind ==
            derived_type_kind::
                const_qualified) {

            return false;
        }

        if (derived_value.kind ==
            derived_type_kind::
                volatile_qualified) {

            type_value =
                derived_value.child;

            continue;
        }

        break;
    }

    if (type_value.kind() ==
        type_ref_kind::intrinsic) {

        const auto intrinsic_value =
            static_cast<intrinsic_type>(
                type_value.payload());

        return intrinsic_value >
                intrinsic_type::none &&
            intrinsic_value <=
                intrinsic_type::nullptr_type &&
            intrinsic_value !=
                intrinsic_type::void_type;
    }

    return type_value.kind() ==
            type_ref_kind::derived &&
        derived(
            type_value,
            derived_value) &&
        derived_value.kind ==
            derived_type_kind::pointer;
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

    if (endpoint.member.is_path()) {
        endpoint_path_record path;

        if (!endpoint_path(
                endpoint.member.path(),
                path) ||
            path.root_type !=
                object_value.type) {

            return false;
        }

        output =
            path.value_type;

        return contains(output);
    }

    const auto member_value =
        endpoint.member.direct_member();

    if (!member_value) {
        return false;
    }

    auto object_type =
        object_value.type;

    derived_type_record derived_value;

    while (derived(
               object_type,
               derived_value) &&
           (derived_value.kind ==
                derived_type_kind::
                    const_qualified ||
            derived_value.kind ==
                derived_type_kind::
                    volatile_qualified)) {

        object_type =
            derived_value.child;
    }

    type_handle type_value;

    if (!named(
            object_type,
            type_value)) {

        return false;
    }

    member_record value;

    if (!member(
            type_value,
            member_value,
            value)) {

        return false;
    }

    output =
        value.type;

    return static_cast<bool>(
        output);
}

bool graph_delta::initialization(
    object_endpoint target,
    object_initialization_record& output) const noexcept {

    output = {};

    if (!target.object ||
        !target.member) {

        return false;
    }

    if (const auto* patch =
            find_initialization_patch(
                target);
        patch != nullptr) {

        if (!patch->live ||
            patch->value.target !=
                target) {

            return false;
        }

        output =
            patch->value;

        return true;
    }

    return baseline != nullptr &&
        baseline->initialization(
            target,
            output);
}

server_status graph_delta::visit_initializations(
    void* context,
    initialization_visitor visitor) const noexcept {

    if (visitor == nullptr) {
        return server_status::
            project_configuration_invalid;
    }

    std::size_t emitted = 0;

    if (baseline != nullptr) {
        for (std::size_t index = 0;
             index <
                baseline->initialization_count();
             ++index) {

            object_initialization_record value;

            if (!baseline->initialization_at(
                    index,
                    value)) {

                return server_status::
                    project_artifact_invalid;
            }

            if (const auto* patch =
                    find_initialization_patch(
                        value.target);
                patch != nullptr) {

                if (!patch->live) {
                    continue;
                }

                value =
                    patch->value;
            }

            const auto visited =
                visitor(
                    context,
                    value);

            if (!succeeded(visited)) {
                return visited;
            }

            ++emitted;
        }
    }

    for (const auto& patch :
         initialization_patches) {

        if (!patch.live) {
            continue;
        }

        object_initialization_record baseline_value;

        if (baseline != nullptr &&
            baseline->initialization(
                patch.value.target,
                baseline_value)) {

            continue;
        }

        const auto visited =
            visitor(
                context,
                patch.value);

        if (!succeeded(visited)) {
            return visited;
        }

        ++emitted;
    }

    return emitted ==
            live_initialization_count_value
        ? server_status::success
        : server_status::
            project_artifact_invalid;
}

server_status graph_delta::add_initialization(
    object_endpoint target,
    construction_value value,
    bool& replaced) noexcept {

    replaced = false;

    type_ref target_type;

    if (!endpoint_type(
            target,
            target_type) ||
        !scalar_initialization_target(
            target_type) ||
        !valid_construction(
            value) ||
        value.kind ==
            construction_kind::
                member_binding ||
        value.kind ==
            construction_kind::
                object_binding ||
        value.kind ==
            construction_kind::
                unsupported ||
        !construction_compatible(
            *this,
            target_type,
            value)) {

        return server_status::
            project_configuration_invalid;
    }

    if (auto* patch =
            find_initialization_patch(
                target);
        patch != nullptr) {

        replaced =
            patch->live;

        if (!patch->live) {
            ++live_initialization_count_value;
        }

        patch->value = {
            target,
            value,
        };

        patch->live = true;

        return server_status::success;
    }

    object_initialization_record
        baseline_value;

    const auto baseline_present =
        baseline != nullptr &&
        baseline->initialization(
            target,
            baseline_value);

    const auto prepared =
        ensure_initialization_target_index_capacity(
            1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto key =
        link_target_key(
            target);

    if (key == 0 ||
        initialization_patches.size() >=
            (std::numeric_limits<
                std::uint32_t>::max)()) {

        return key == 0
            ? server_status::
                project_configuration_invalid
            : server_status::io_error;
    }

    try {
        initialization_patches.push_back({
            {
                target,
                value,
            },
            true,
        });
    }
    catch (...) {
        return server_status::io_error;
    }

    insert_initialization_target_index(
        initialization_target_index,
        key,
        static_cast<std::uint32_t>(
            initialization_patches.size()));

    replaced =
        baseline_present;

    if (!baseline_present) {
        ++live_initialization_count_value;
    }

    return server_status::success;
}

server_status graph_delta::invalidate_initialization(
    object_endpoint target) noexcept {

    if (!target.object ||
        !target.member) {

        return server_status::
            project_configuration_invalid;
    }

    if (auto* patch =
            find_initialization_patch(
                target);
        patch != nullptr) {

        if (!patch->live) {
            return server_status::success;
        }

        if (live_initialization_count_value == 0) {
            return server_status::
                project_artifact_invalid;
        }

        patch->live = false;
        --live_initialization_count_value;

        return server_status::success;
    }

    object_initialization_record
        baseline_value;

    if (baseline == nullptr ||
        !baseline->initialization(
            target,
            baseline_value)) {

        return server_status::
            project_artifact_invalid;
    }

    const auto prepared =
        ensure_initialization_target_index_capacity(
            1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto key =
        link_target_key(
            target);

    if (key == 0 ||
        live_initialization_count_value == 0 ||
        initialization_patches.size() >=
            (std::numeric_limits<
                std::uint32_t>::max)()) {

        return server_status::
            project_artifact_invalid;
    }

    try {
        initialization_patches.push_back({
            baseline_value,
            false,
        });
    }
    catch (...) {
        return server_status::io_error;
    }

    insert_initialization_target_index(
        initialization_target_index,
        key,
        static_cast<std::uint32_t>(
            initialization_patches.size()));

    --live_initialization_count_value;

    return server_status::success;
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

namespace {

template <typename T>
[[nodiscard]] T dense_from_raw(
    std::uint32_t value) noexcept {

    static_assert(
        sizeof(T) ==
            sizeof(std::uint32_t));

    static_assert(
        std::is_trivially_copyable_v<T>);

    return std::bit_cast<T>(
        value);
}

[[nodiscard]] std::uint32_t
dense_type_ref_raw(
    type_ref lineage,
    std::uint32_t payload) noexcept {

    return payload != 0 &&
        payload <=
            type_ref::maximum_payload
        ? (lineage.value() &
           ~type_ref::maximum_payload) |
              payload
        : 0;
}

}

void graph_dense_projection::reset() noexcept {

    source = nullptr;

    type_slots.clear();
    object_slots.clear();
    link_slots.clear();
    derived_slots.clear();
    endpoint_path_slots.clear();

    final_type_count = 0;
    final_object_count = 0;
    final_link_count = 0;
    final_derived_count = 0;
    final_endpoint_path_count = 0;
    final_member_count = 0;
    final_base_count = 0;
    final_object_construction_count = 0;
    final_endpoint_path_step_count = 0;
}

type_handle graph_dense_projection::remap(
    type_handle value) const noexcept {

    if (!valid() ||
        !value ||
        value.value() >=
            type_slots.size()) {

        return {};
    }

    const auto slot =
        type_slots[
            value.value()];

    return slot != 0
        ? dense_from_raw<type_handle>(
            slot)
        : type_handle{};
}

object_handle graph_dense_projection::remap(
    object_handle value) const noexcept {

    if (!valid() ||
        !value ||
        value.value() >=
            object_slots.size()) {

        return {};
    }

    const auto slot =
        object_slots[
            value.value()];

    return slot != 0
        ? dense_from_raw<object_handle>(
            slot)
        : object_handle{};
}

link_handle graph_dense_projection::remap(
    link_handle value) const noexcept {

    if (!valid() ||
        !value ||
        value.value() >=
            link_slots.size()) {

        return {};
    }

    const auto slot =
        link_slots[
            value.value()];

    return slot != 0
        ? dense_from_raw<link_handle>(
            slot)
        : link_handle{};
}

type_ref graph_dense_projection::remap(
    type_ref value) const noexcept {

    if (!valid() ||
        !value) {

        return {};
    }

    if (value.kind() ==
        type_ref_kind::intrinsic) {

        return value;
    }

    if (value.kind() ==
        type_ref_kind::named) {

        type_handle lineage;

        return source != nullptr &&
            source->named(
                value,
                lineage) &&
            remap(lineage)
            ? value
            : type_ref{};
    }

    if (value.kind() ==
        type_ref_kind::derived) {

        if (value.payload() >=
            derived_slots.size()) {

            return {};
        }

        const auto raw =
            dense_type_ref_raw(
                value,
                derived_slots[
                    value.payload()]);

        return raw != 0
            ? dense_from_raw<type_ref>(
                raw)
            : type_ref{};
    }

    return {};
}

endpoint_path_handle
graph_dense_projection::remap(
    endpoint_path_handle value) const noexcept {

    if (!valid() ||
        !value ||
        value.value() >=
            endpoint_path_slots.size()) {

        return {};
    }

    const auto slot =
        endpoint_path_slots[
            value.value()];

    return slot != 0
        ? dense_from_raw<
            endpoint_path_handle>(
                slot)
        : endpoint_path_handle{};
}

bool graph_dense_projection::remap(
    object_endpoint value,
    object_endpoint& output) const noexcept {

    output = {};

    const auto object =
        remap(
            value.object);

    if (!object ||
        !value.member) {

        return false;
    }

    endpoint_ref member =
        value.member;

    if (member.is_path()) {
        const auto path =
            remap(
                member.path());

        if (!path) {
            return false;
        }

        member =
            endpoint_ref::from_path(
                path);
    }

    output = {
        object,
        member,
    };

    return true;
}

bool graph_dense_projection::remap(
    construction_value value,
    construction_value& output) const noexcept {

    output = {};

    if (!valid_construction(
            value)) {

        return false;
    }

    if (value.kind ==
        construction_kind::
            object_binding) {

        if (value.operand == 0 ||
            value.operand >=
                object_slots.size()) {

            return false;
        }

        const auto object =
            object_slots[
                value.operand];

        if (object == 0) {
            return false;
        }

        value.operand =
            object;
    }

    output =
        value;

    return true;
}

server_status graph_dense_projection::prepare(
    const graph_delta& graph) noexcept {

    reset();

    if (graph.type_count() >
            type_handle::maximum_slot ||
        graph.object_count() >
            object_handle::maximum_slot ||
        graph.link_count() >
            link_handle::maximum_slot ||
        graph.derived_type_count() >
            type_ref::maximum_payload ||
        graph.endpoint_path_count() >
            endpoint_path_handle::
                maximum_slot) {

        return server_status::io_error;
    }

    try {
        type_slots.assign(
            graph.type_count() + 1,
            0);

        object_slots.assign(
            graph.object_count() + 1,
            0);

        link_slots.assign(
            graph.link_count() + 1,
            0);

        derived_slots.assign(
            graph.derived_type_count() + 1,
            0);

        endpoint_path_slots.assign(
            graph.endpoint_path_count() + 1,
            0);
    }
    catch (...) {
        reset();
        return server_status::io_error;
    }

    source =
        &graph;

    for (std::size_t index = 0;
         index < graph.type_count();
         ++index) {

        const auto lineage =
            graph.type_at(
                index);

        if (!lineage) {
            continue;
        }

        type_slots[
            lineage.value()] =
            static_cast<std::uint32_t>(
                ++final_type_count);
    }

    for (std::size_t index = 0;
         index < graph.object_count();
         ++index) {

        const auto lineage =
            graph.object_at(
                index);

        if (!lineage) {
            continue;
        }

        object_slots[
            lineage.value()] =
            static_cast<std::uint32_t>(
                ++final_object_count);
    }

    for (std::size_t index = 0;
         index < graph.link_count();
         ++index) {

        const auto lineage =
            graph.link_at(
                index);

        if (!lineage) {
            continue;
        }

        link_slots[
            lineage.value()] =
            static_cast<std::uint32_t>(
                ++final_link_count);
    }

    if (final_type_count !=
            graph.live_type_count() ||
        final_object_count !=
            graph.live_object_count() ||
        final_link_count !=
            graph.live_link_count()) {

        reset();
        return server_status::
            project_artifact_invalid;
    }

    for (std::size_t index = 0;
         index <
            graph.derived_type_count();
         ++index) {

        const auto lineage =
            graph.derived_at(
                index);

        derived_type_record record;

        if (!lineage ||
            !graph.derived(
                lineage,
                record)) {

            reset();
            return server_status::
                project_artifact_invalid;
        }

        if (!remap(
                record.child)) {

            continue;
        }

        derived_slots[
            lineage.payload()] =
            static_cast<std::uint32_t>(
                ++final_derived_count);
    }

    for (std::size_t index = 0;
         index <
            graph.endpoint_path_count();
         ++index) {

        const auto lineage =
            graph.endpoint_path_at(
                index);

        endpoint_path_record record;

        if (!lineage ||
            !graph.endpoint_path(
                lineage,
                record)) {

            reset();
            return server_status::
                project_artifact_invalid;
        }

        if (!remap(
                record.root_type) ||
            !remap(
                record.value_type)) {

            continue;
        }

        auto current_type =
            record.root_type;

        bool path_live = true;

        for (std::uint32_t step = 0;
             step <
                record.steps.count;
             ++step) {

            endpoint_path_step value;

            if (!graph.endpoint_path_step_at(
                    static_cast<std::size_t>(
                        record.steps.begin) +
                        step,
                    value)) {

                reset();
                return server_status::
                    project_artifact_invalid;
            }

            for (const auto reserved :
                 value.reserved) {

                if (reserved != 0) {
                    reset();
                    return server_status::
                        project_artifact_invalid;
                }
            }

            derived_type_record
                derived_value;

            while (graph.derived(
                       current_type,
                       derived_value) &&
                   (derived_value.kind ==
                        derived_type_kind::
                            const_qualified ||
                    derived_value.kind ==
                        derived_type_kind::
                            volatile_qualified)) {

                current_type =
                    derived_value.child;
            }

            if (value.kind ==
                endpoint_path_step_kind::
                    array_index) {

                if (!graph.derived(
                        current_type,
                        derived_value) ||
                    derived_value.kind !=
                        derived_type_kind::
                            bounded_array ||
                    value.value >=
                        derived_value.payload) {

                    path_live = false;
                    break;
                }

                current_type =
                    derived_value.child;
                continue;
            }

            if (value.kind !=
                    endpoint_path_step_kind::
                        member ||
                value.value >
                    (std::numeric_limits<
                        std::uint32_t>::max)() ||
                current_type.kind() !=
                    type_ref_kind::named) {

                path_live = false;
                break;
            }

            type_handle record_type;

            if (!graph.named(
                    current_type,
                    record_type)) {

                path_live = false;
                break;
            }

            member_record member;

            if (!graph.member(
                    record_type,
                    static_cast<std::uint32_t>(
                        value.value),
                    member)) {

                path_live = false;
                break;
            }

            current_type =
                member.type;
        }

        if (!path_live ||
            current_type !=
                record.value_type) {

            continue;
        }

        if (record.steps.count >
            (std::numeric_limits<
                std::uint32_t>::max)() -
                final_endpoint_path_step_count) {

            reset();
            return server_status::io_error;
        }

        final_endpoint_path_step_count +=
            record.steps.count;

        endpoint_path_slots[
            lineage.value()] =
            static_cast<std::uint32_t>(
                ++final_endpoint_path_count);
    }

    for (std::size_t index = 0;
         index < graph.type_count();
         ++index) {

        const auto lineage =
            graph.type_at(
                index);

        if (!lineage) {
            continue;
        }

        type_entry type;

        if (!graph.type(
                lineage,
                type)) {

            reset();
            return server_status::
                project_artifact_invalid;
        }

        constexpr auto maximum =
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)());

        if (type.members.count >
                maximum -
                    final_member_count ||
            type.bases.count >
                maximum -
                    final_base_count) {

            reset();
            return server_status::io_error;
        }

        final_member_count +=
            type.members.count;

        final_base_count +=
            type.bases.count;

        for (std::uint32_t local = 0;
             local < type.bases.count;
             ++local) {

            base_record base;

            if (!graph.base(
                    lineage,
                    local,
                    base) ||
                !remap(
                    base.type)) {

                reset();
                return server_status::
                    project_artifact_invalid;
            }
        }

        for (std::uint32_t local = 0;
             local < type.members.count;
             ++local) {

            member_record member;
            construction_value construction;
            construction_value remapped;

            if (!graph.member(
                    lineage,
                    local,
                    member) ||
                !remap(
                    member.type) ||
                !graph.construction(
                    lineage,
                    local,
                    construction) ||
                !remap(
                    construction,
                    remapped)) {

                reset();
                return server_status::
                    project_artifact_invalid;
            }
        }
    }

    for (std::size_t index = 0;
         index < graph.object_count();
         ++index) {

        const auto lineage =
            graph.object_at(
                index);

        if (!lineage) {
            continue;
        }

        object_entry object;
        construction_value construction;
        construction_value remapped;

        if (!graph.object(
                lineage,
                object) ||
            !remap(
                object.type) ||
            !graph.construction(
                lineage,
                construction) ||
            !remap(
                construction,
                remapped)) {

            reset();
            return server_status::
                project_artifact_invalid;
        }

        if (object.non_default_initializer()) {
            if (object.construction_slot() == 0 ||
                construction.kind ==
                    construction_kind::
                        member_binding ||
                construction.kind ==
                    construction_kind::
                        object_binding ||
                final_object_construction_count ==
                    graph_object_construction_slot_mask) {

                reset();
                return construction.kind ==
                            construction_kind::
                                member_binding ||
                       construction.kind ==
                            construction_kind::
                                object_binding
                    ? server_status::
                        project_artifact_invalid
                    : server_status::io_error;
            }

            ++final_object_construction_count;
        }
        else if (object.construction_slot() != 0 ||
                 construction !=
                    construction_value{}) {

            reset();
            return server_status::
                project_artifact_invalid;
        }
    }

    for (std::size_t index = 0;
         index < graph.link_count();
         ++index) {

        const auto lineage =
            graph.link_at(
                index);

        if (!lineage) {
            continue;
        }

        link_record link;
        object_endpoint source_endpoint;
        object_endpoint target_endpoint;

        if (!graph.link(
                lineage,
                link) ||
            !remap(
                link.source,
                source_endpoint) ||
            !remap(
                link.target,
                target_endpoint)) {

            reset();
            return server_status::
                project_artifact_invalid;
        }
    }

    const auto initialization_check =
        [](void* context,
           const object_initialization_record& value) noexcept
        -> server_status {

            const auto& projection =
                *static_cast<
                    const graph_dense_projection*>(
                        context);

            object_endpoint target;
            construction_value construction;

            return projection.remap(
                       value.target,
                       target) &&
                   projection.remap(
                       value.value,
                       construction)
                ? server_status::success
                : server_status::
                    project_artifact_invalid;
        };

    const auto visited =
        graph.visit_initializations(
            this,
            initialization_check);

    if (!succeeded(visited)) {
        reset();
        return visited;
    }

    return server_status::success;
}

}
