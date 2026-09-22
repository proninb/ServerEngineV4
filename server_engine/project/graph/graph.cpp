#include "graph.hpp"

#include <limits>

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

}

std::uint32_t graph::encode_location(
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

graph::location_kind graph::decode_location_kind(
    std::uint32_t value) noexcept {

    return static_cast<location_kind>(
        value >> 30);
}

std::uint32_t graph::decode_location_slot(
    std::uint32_t value) noexcept {

    return value &
        type_ref::maximum_payload;
}

server_status graph::ensure_identity_slot(
    identity_ref identity) noexcept {

    if (!identity) {
        return server_status::
            project_configuration_invalid;
    }

    const auto slot =
        static_cast<std::size_t>(
            identity.slot());

    if (slot < identity_locations.size()) {
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

bool graph::contains(
    type_handle type) const noexcept {

    return type &&
        type.value() <= types.size();
}

bool graph::contains(
    object_handle object) const noexcept {

    return object &&
        object.value() <= objects.size();
}

bool graph::contains(
    link_handle link) const noexcept {

    return link &&
        link.value() <= links.size();
}

bool graph::contains(
    type_ref type) const noexcept {

    if (!type) {
        return false;
    }

    switch (type.kind()) {
    case type_ref_kind::intrinsic:
        return
            type.payload() <=
                static_cast<std::uint32_t>(
                    intrinsic_type::nullptr_type);

    case type_ref_kind::named:
        return
            type.payload() <=
                types.size();

    case type_ref_kind::derived:
        return
            type.payload() <=
                derived_types.size();

    case type_ref_kind::invalid:
        break;
    }

    return false;
}

const type_entry* graph::find(
    type_handle type) const noexcept {

    return contains(type)
        ? &types[type.value() - 1]
        : nullptr;
}

const object_entry* graph::find(
    object_handle object) const noexcept {

    return contains(object)
        ? &objects[object.value() - 1]
        : nullptr;
}

const link_record* graph::find(
    link_handle link) const noexcept {

    return contains(link)
        ? &links[link.value() - 1]
        : nullptr;
}

type_handle graph::find_type(
    identity_ref identity) const noexcept {

    if (!identity ||
        identity.slot() >=
            identity_locations.size()) {

        return {};
    }

    const auto location =
        identity_locations[
            identity.slot()];

    if (decode_location_kind(location) !=
        location_kind::type) {

        return {};
    }

    const type_handle output{
        decode_location_slot(
            location)};

    return contains(output)
        ? output
        : type_handle{};
}

object_handle graph::find_object(
    identity_ref identity) const noexcept {

    if (!identity ||
        identity.slot() >=
            identity_locations.size()) {

        return {};
    }

    const auto location =
        identity_locations[
            identity.slot()];

    if (decode_location_kind(location) !=
        location_kind::object) {

        return {};
    }

    const object_handle output{
        decode_location_slot(
            location)};

    return contains(output)
        ? output
        : object_handle{};
}

identity_ref graph::identity(
    type_handle type) const noexcept {

    return contains(type)
        ? type_identities[type.value() - 1]
        : identity_ref{};
}

identity_ref graph::identity(
    object_handle object) const noexcept {

    return contains(object)
        ? object_identities[object.value() - 1]
        : identity_ref{};
}

server_status graph::declare_record(
    identity_ref identity,
    graph_record_kind kind,
    type_handle& output) noexcept {

    output = {};

    if (!identity ||
        identity.kind() != identity_kind::type ||
        !valid_record_kind(kind)) {

        return server_status::
            project_configuration_invalid;
    }

    if (const auto existing =
            find_type(identity);
        existing) {

        const auto* entry =
            find(existing);

        if (entry == nullptr ||
            entry->kind !=
                graph_type_kind::record ||
            !compatible_record_kind(
                entry->record_kind,
                kind)) {

            return server_status::
                project_configuration_invalid;
        }

        output = existing;
        return server_status::success;
    }

    if (types.size() >=
        type_handle::maximum_slot) {

        return server_status::io_error;
    }

    const auto old_location_count =
        identity_locations.size();

    const auto prepared =
        ensure_identity_slot(identity);

    if (!succeeded(prepared)) {
        return prepared;
    }

    if (identity_locations[
            identity.slot()] != 0) {

        if (identity_locations.size() >
            old_location_count) {

            identity_locations.resize(
                old_location_count);
        }

        return server_status::
            project_configuration_invalid;
    }

    const auto old_type_count =
        types.size();

    const auto old_identity_count =
        type_identities.size();

    try {
        types.push_back({
            {},
            graph_type_kind::record,
            kind,
            0,
        });

        type_identities.push_back(
            identity);

        output = type_handle{
            static_cast<std::uint32_t>(
                types.size())};

        identity_locations[
            identity.slot()] =
                encode_location(
                    location_kind::type,
                    output.value());

        return server_status::success;
    }
    catch (...) {
        types.resize(
            old_type_count);

        type_identities.resize(
            old_identity_count);

        if (identity_locations.size() >
            old_location_count) {

            identity_locations.resize(
                old_location_count);
        }

        output = {};
        return server_status::io_error;
    }
}

server_status graph::define_record(
    type_handle type,
    graph_record_kind kind,
    std::span<const member_record> definition,
    std::span<const construction_value> construction_values) noexcept {

    auto* entry =
        contains(type)
        ? &types[type.value() - 1]
        : nullptr;

    if (entry == nullptr ||
        entry->kind != graph_type_kind::record ||
        !valid_record_kind(kind) ||
        !compatible_record_kind(
            entry->record_kind,
            kind) ||
        (!construction_values.empty() &&
         construction_values.size() != definition.size())) {

        return server_status::project_configuration_invalid;
    }

    for (std::size_t index = 0;
         index < definition.size();
         ++index) {

        if (!definition[index].name ||
            !contains(definition[index].type)) {

            return server_status::project_configuration_invalid;
        }

        const auto construction =
            construction_values.empty()
            ? construction_value{}
            : construction_values[index];

        if (!valid_construction(construction)) {
            return server_status::project_configuration_invalid;
        }
    }

    if (entry->defined()) {
        if (entry->record_kind != kind) {
            return server_status::project_configuration_invalid;
        }

        const auto existing =
            members(type);

        if (existing.size() != definition.size()) {
            return server_status::project_configuration_invalid;
        }

        for (std::size_t index = 0;
             index < existing.size();
             ++index) {

            if (existing[index].name != definition[index].name ||
                existing[index].type != definition[index].type ||
                existing[index].access != definition[index].access) {

                return server_status::project_configuration_invalid;
            }

            const auto expected =
                construction_values.empty()
                ? construction_value{}
                : construction_values[index];

            const auto position =
                static_cast<std::size_t>(
                    entry->members.begin) +
                index;

            if (position >= member_construction.size() ||
                member_construction[position] != expected) {

                return server_status::project_configuration_invalid;
            }
        }

        return server_status::success;
    }

    const auto maximum =
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)());

    if (member_records.size() > maximum ||
        definition.size() > maximum ||
        definition.size() >
            maximum - member_records.size()) {

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
        member_records.resize(old_count);
        member_construction.resize(old_count);
        return server_status::io_error;
    }

    entry->members = {
        static_cast<std::uint32_t>(old_count),
        static_cast<std::uint32_t>(definition.size()),
    };

    entry->record_kind = kind;
    entry->flags |= graph_type_defined;

    return server_status::success;
}

server_status graph::add_object(
    identity_ref identity,
    type_ref type,
    object_handle& output,
    std::uint32_t flags) noexcept {

    output = {};

    if (!identity ||
        identity.kind() != identity_kind::object ||
        !contains(type) ||
        (flags & ~graph_object_non_default_initializer) != 0) {

        return server_status::project_configuration_invalid;
    }

    if (const auto existing =
            find_object(identity);
        existing) {

        const auto* entry =
            find(existing);

        if (entry == nullptr ||
            entry->type != type ||
            entry->flags != flags) {

            return server_status::project_configuration_invalid;
        }

        output = existing;
        return server_status::success;
    }

    if (objects.size() >=
        object_handle::maximum_slot) {

        return server_status::io_error;
    }

    const auto old_location_count =
        identity_locations.size();

    const auto prepared =
        ensure_identity_slot(identity);

    if (!succeeded(prepared)) {
        return prepared;
    }

    if (identity_locations[identity.slot()] != 0) {
        if (identity_locations.size() >
            old_location_count) {

            identity_locations.resize(
                old_location_count);
        }

        return server_status::project_configuration_invalid;
    }

    const auto old_object_count =
        objects.size();

    const auto old_identity_count =
        object_identities.size();

    try {
        objects.push_back({
            type,
            flags,
        });

        object_identities.push_back(
            identity);

        output = object_handle{
            static_cast<std::uint32_t>(
                objects.size())};

        identity_locations[identity.slot()] =
            encode_location(
                location_kind::object,
                output.value());

        return server_status::success;
    }
    catch (...) {
        objects.resize(
            old_object_count);

        object_identities.resize(
            old_identity_count);

        if (identity_locations.size() >
            old_location_count) {

            identity_locations.resize(
                old_location_count);
        }

        output = {};
        return server_status::io_error;
    }
}

type_ref graph::intrinsic(
    intrinsic_type type) const noexcept {

    return valid_intrinsic(type)
        ? type_ref::make(
            type_ref_kind::intrinsic,
            static_cast<std::uint32_t>(
                type))
        : type_ref{};
}

type_ref graph::named(
    type_handle type) const noexcept {

    return contains(type)
        ? type_ref::make(
            type_ref_kind::named,
            type.value())
        : type_ref{};
}

std::uint64_t graph::hash_derived(
    type_ref child,
    derived_type_kind kind,
    std::uint64_t payload) noexcept {

    std::uint64_t hash =
        1469598103934665603ull;

    auto mix =
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
    mix(static_cast<std::uint8_t>(kind));
    mix(payload);

    return hash == 0
        ? 1
        : hash;
}

std::uint32_t graph::fingerprint(
    std::uint64_t hash) noexcept {

    const auto folded =
        static_cast<std::uint32_t>(
            hash ^
            (hash >> 32));

    return folded == 0
        ? 1
        : folded;
}

type_ref graph::find_derived(
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
            slot.type.payload() <=
                derived_types.size()) {

            const auto& record =
                derived_types[
                    slot.type.payload() - 1];

            if (record.child == child &&
                record.kind == kind &&
                record.payload == payload) {

                return slot.type;
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

void graph::insert_derived_index(
    std::vector<derived_index_slot>& target,
    type_ref type,
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
        type,
    };
}

server_status graph::ensure_derived_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
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
             index < derived_types.size();
             ++index) {

            const auto type =
                type_ref::make(
                    type_ref_kind::derived,
                    static_cast<std::uint32_t>(
                        index + 1));

            const auto& record =
                derived_types[index];

            const auto hash =
                hash_derived(
                    record.child,
                    record.kind,
                    record.payload);

            insert_derived_index(
                candidate,
                type,
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

server_status graph::derive(
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

    if (derived_types.size() >=
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
                    derived_types.size()));

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

std::span<const member_record> graph::members(
    type_handle type) const noexcept {

    const auto* entry =
        find(type);

    if (entry == nullptr ||
        !entry->defined() ||
        entry->members.count == 0) {

        return {};
    }

    const auto begin =
        static_cast<std::size_t>(
            entry->members.begin);

    const auto count =
        static_cast<std::size_t>(
            entry->members.count);

    if (begin > member_records.size() ||
        count >
            member_records.size() - begin) {

        return {};
    }

    return {
        member_records.data() + begin,
        count,
    };
}

member_index graph::find_member(
    type_handle type,
    string_id name) const noexcept {

    if (!name) {
        return {};
    }

    const auto values =
        members(type);

    for (std::size_t index = 0;
         index < values.size();
         ++index) {

        if (values[index].name == name) {
            return member_index{
                static_cast<std::uint32_t>(
                    index)};
        }
    }

    return {};
}

const member_record* graph::member(
    type_handle type,
    member_index member_value) const noexcept {

    if (!member_value) {
        return nullptr;
    }

    const auto values =
        members(type);

    return member_value.value() <
        values.size()
        ? &values[
            member_value.value()]
        : nullptr;
}

const construction_value* graph::construction(
    type_handle type,
    member_index member_value) const noexcept {

    const auto* entry =
        find(type);

    if (entry == nullptr ||
        !entry->defined() ||
        !member_value ||
        member_value.value() >= entry->members.count) {

        return nullptr;
    }

    const auto position =
        static_cast<std::size_t>(
            entry->members.begin) +
        member_value.value();

    return position < member_construction.size()
        ? &member_construction[position]
        : nullptr;
}


bool graph::intrinsic(
    type_ref type,
    intrinsic_type& output) const noexcept {

    output =
        intrinsic_type::none;

    if (!contains(type) ||
        type.kind() !=
            type_ref_kind::intrinsic) {

        return false;
    }

    output =
        static_cast<intrinsic_type>(
            type.payload());

    return valid_intrinsic(output);
}

bool graph::named(
    type_ref type,
    type_handle& output) const noexcept {

    output = {};

    if (!contains(type) ||
        type.kind() !=
            type_ref_kind::named) {

        return false;
    }

    output =
        type_handle{
            type.payload()};

    return contains(output);
}

bool graph::derived(
    type_ref type,
    derived_type_record& output) const noexcept {

    output = {};

    if (!contains(type) ||
        type.kind() !=
            type_ref_kind::derived) {

        return false;
    }

    output =
        derived_types[
            type.payload() - 1];

    return true;
}

bool graph::endpoint_valid(
    object_endpoint endpoint) const noexcept {

    const auto* object =
        find(endpoint.object);

    if (object == nullptr ||
        !endpoint.member) {

        return false;
    }

    type_handle type;

    if (!named(
            object->type,
            type)) {

        return false;
    }

    return member(
        type,
        endpoint.member) != nullptr;
}

server_status graph::add_link(
    object_endpoint source,
    object_endpoint target,
    link_handle& output) noexcept {

    output = {};

    if (!endpoint_valid(source) ||
        !endpoint_valid(target)) {

        return server_status::project_configuration_invalid;
    }

    for (std::size_t index = 0;
         index < links.size();
         ++index) {

        const auto& existing =
            links[index];

        if (existing.target != target) {
            continue;
        }

        if (existing.source != source) {
            return server_status::project_configuration_invalid;
        }

        output = link_handle{
            static_cast<std::uint32_t>(
                index + 1)};

        return server_status::success;
    }

    if (links.size() >=
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)())) {

        return server_status::io_error;
    }

    try {
        links.push_back({
            source,
            target,
        });

        output = link_handle{
            static_cast<std::uint32_t>(
                links.size())};

        return server_status::success;
    }
    catch (...) {
        output = {};
        return server_status::io_error;
    }
}

}
