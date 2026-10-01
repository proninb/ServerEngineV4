#include "graph.hpp"
#include "construction_semantics.hpp"

#include <algorithm>
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
        return static_cast<bool>(
            find_type(
                identity_ref::make(
                    type.payload(),
                    identity_kind::type)));

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
    std::span<const construction_value> construction_values,
    std::span<const base_record> bases_value,
    bool declares_virtual) noexcept {

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
         construction_values.size() != definition.size()) ||
        (kind ==
             graph_record_kind::union_type &&
         (!bases_value.empty() ||
          declares_virtual))) {

        return server_status::project_configuration_invalid;
    }

    const auto type_identity =
        identity(type);

    if (!type_identity ||
        type_identity.kind() !=
            identity_kind::type) {

        return server_status::project_configuration_invalid;
    }

    bool polymorphic_value =
        declares_virtual;

    for (std::size_t index = 0;
         index < bases_value.size();
         ++index) {

        const auto& base =
            bases_value[index];

        const auto base_handle =
            find_type(
                base.type);

        const auto* base_entry =
            find(base_handle);

        if (!base.type ||
            base.type.kind() !=
                identity_kind::type ||
            base.type == type_identity ||
            !base_handle ||
            base_entry == nullptr ||
            !base_entry->defined() ||
            base_entry->kind !=
                graph_type_kind::record ||
            base_entry->record_kind ==
                graph_record_kind::union_type ||
            !valid_member_access(
                base.access) ||
            (base.flags &
                ~graph_base_flag_mask) != 0 ||
            base.reserved != 0) {

            return server_status::project_configuration_invalid;
        }

        for (std::size_t previous = 0;
             previous < index;
             ++previous) {

            if (bases_value[previous].type ==
                base.type) {

                return server_status::
                    project_configuration_invalid;
            }
        }

        polymorphic_value =
            polymorphic_value ||
            base_entry->polymorphic();
    }

    for (std::size_t index = 0;
         index < definition.size();
         ++index) {

        if (!definition[index].name ||
            !contains(definition[index].type) ||
            !valid_member_access(
                definition[index].access)) {

            return server_status::project_configuration_invalid;
        }

        const auto construction =
            construction_values.empty()
            ? construction_value{}
            : construction_values[index];

        if (!valid_construction(construction) ||
            !construction_compatible(
                *this,
                definition[index].type,
                construction)) {

            return server_status::project_configuration_invalid;
        }

        if (construction.kind ==
            construction_kind::member_binding) {

            if (construction.operand >
                    definition.size() ||
                !reference_binding_compatible(
                    definition[index].type,
                    definition[
                        construction.operand - 1].type)) {

                return server_status::project_configuration_invalid;
            }
        }

        if (construction.kind ==
            construction_kind::object_binding) {

            const auto* object =
                find(
                    object_handle{
                        construction.operand});

            if (object == nullptr ||
                !object->internal_static() ||
                !reference_binding_compatible(
                    definition[index].type,
                    object->type)) {

                return server_status::project_configuration_invalid;
            }
        }
    }

    if (entry->defined()) {
        if (entry->record_kind != kind ||
            entry->polymorphic() !=
                polymorphic_value) {

            return server_status::project_configuration_invalid;
        }

        const auto existing_bases =
            bases(type);

        if (existing_bases.size() !=
            bases_value.size()) {

            return server_status::project_configuration_invalid;
        }

        for (std::size_t index = 0;
             index < existing_bases.size();
             ++index) {

            if (existing_bases[index].type !=
                    bases_value[index].type ||
                existing_bases[index].access !=
                    bases_value[index].access ||
                existing_bases[index].flags !=
                    bases_value[index].flags) {

                return server_status::project_configuration_invalid;
            }
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
            maximum - member_records.size() ||
        base_records.size() > maximum ||
        bases_value.size() > maximum ||
        bases_value.size() >
            maximum - base_records.size()) {

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

    entry->bases = {
        static_cast<std::uint32_t>(
            old_base_count),
        static_cast<std::uint32_t>(
            bases_value.size()),
    };

    entry->members = {
        static_cast<std::uint32_t>(
            old_member_count),
        static_cast<std::uint32_t>(
            definition.size()),
    };

    entry->record_kind = kind;
    entry->flags |= graph_type_defined;

    if (polymorphic_value) {
        entry->flags |=
            graph_type_polymorphic;
    }

    return server_status::success;
}

server_status graph::add_object(
    identity_ref identity,
    type_ref type,
    object_handle& output,
    std::uint32_t flags,
    construction_value initial) noexcept {

    output = {};

    if (!identity ||
        identity.kind() != identity_kind::object ||
        !contains(type) ||
        (flags & ~graph_object_flag_mask) != 0 ||
        !valid_construction(initial) ||
        !construction_compatible(
            *this,
            type,
            initial) ||
        (!(flags &
            graph_object_non_default_initializer) &&
         initial != construction_value{}) ||
        initial.kind ==
            construction_kind::member_binding ||
        initial.kind ==
            construction_kind::object_binding) {

        return server_status::project_configuration_invalid;
    }

    if (const auto existing =
            find_object(identity);
        existing) {

        const auto* entry =
            find(existing);

        construction_value existing_initial;

        if (entry == nullptr ||
            !construction(
                existing,
                existing_initial) ||
            entry->type != type ||
            (entry->state &
                graph_object_flag_mask) != flags ||
            existing_initial != initial) {

            return server_status::project_configuration_invalid;
        }

        output = existing;
        return server_status::success;
    }

    if (objects.size() >=
        object_handle::maximum_slot) {

        return server_status::io_error;
    }

    const auto has_initial =
        (flags &
            graph_object_non_default_initializer) != 0;

    if (has_initial &&
        object_construction.size() >=
            graph_object_construction_slot_mask) {

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

    const auto old_construction_count =
        object_construction.size();

    try {
        std::uint32_t construction_slot = 0;

        if (has_initial) {
            object_construction.push_back(
                initial);

            construction_slot =
                static_cast<std::uint32_t>(
                    object_construction.size());
        }

        objects.push_back({
            type,
            flags |
                construction_slot,
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

        object_construction.resize(
            old_construction_count);

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

    const auto value =
        identity(type);

    return value &&
        value.kind() ==
            identity_kind::type
        ? type_ref::make(
            type_ref_kind::named,
            value.slot())
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

    const auto payload_valid =
        kind ==
            derived_type_kind::bounded_array
        ? payload != 0
        : payload == 0;

    if (!contains(child) ||
        !valid_derived_kind(kind) ||
        !payload_valid) {

        return server_status::
            project_configuration_invalid;
    }

    auto unqualified_child =
        child;

    bool child_reference = false;
    bool child_void = false;

    for (;;) {
        if (unqualified_child.kind() ==
            type_ref_kind::intrinsic) {

            child_void =
                unqualified_child.payload() ==
                static_cast<std::uint32_t>(
                    intrinsic_type::void_type);
            break;
        }

        if (unqualified_child.kind() !=
            type_ref_kind::derived) {

            break;
        }

        const auto& record =
            derived_types[
                unqualified_child.payload() - 1];

        if (record.kind ==
                derived_type_kind::const_qualified ||
            record.kind ==
                derived_type_kind::volatile_qualified) {

            unqualified_child =
                record.child;
            continue;
        }

        child_reference =
            record.kind ==
                derived_type_kind::lvalue_reference ||
            record.kind ==
                derived_type_kind::rvalue_reference;

        break;
    }

    if (child_reference &&
        (kind ==
             derived_type_kind::pointer ||
         kind ==
             derived_type_kind::lvalue_reference ||
         kind ==
             derived_type_kind::rvalue_reference ||
         kind ==
             derived_type_kind::bounded_array ||
         kind ==
             derived_type_kind::unbounded_array)) {

        return server_status::
            project_configuration_invalid;
    }

    if (child_void &&
        (kind ==
             derived_type_kind::lvalue_reference ||
         kind ==
             derived_type_kind::rvalue_reference ||
         kind ==
             derived_type_kind::bounded_array ||
         kind ==
             derived_type_kind::unbounded_array)) {

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

std::span<const base_record> graph::bases(
    type_handle type) const noexcept {

    const auto* entry =
        find(type);

    if (entry == nullptr ||
        !entry->defined() ||
        entry->bases.count == 0) {

        return {};
    }

    const auto begin =
        static_cast<std::size_t>(
            entry->bases.begin);

    const auto count =
        static_cast<std::size_t>(
            entry->bases.count);

    if (begin > base_records.size() ||
        count >
            base_records.size() - begin) {

        return {};
    }

    return {
        base_records.data() + begin,
        count,
    };
}

bool graph::polymorphic(
    type_handle type) const noexcept {

    const auto* entry =
        find(type);

    return entry != nullptr &&
        entry->defined() &&
        entry->polymorphic();
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

bool graph::construction(
    object_handle object,
    construction_value& output) const noexcept {

    output = {};

    const auto* entry =
        find(object);

    if (entry == nullptr) {
        return false;
    }

    if (!entry->non_default_initializer()) {
        return entry->construction_slot() == 0;
    }

    const auto slot =
        entry->construction_slot();

    if (slot == 0 ||
        slot > object_construction.size()) {

        return false;
    }

    output =
        object_construction[slot - 1];

    return true;
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

    if (!type ||
        type.kind() !=
            type_ref_kind::named) {

        return false;
    }

    output =
        find_type(
            identity_ref::make(
                type.payload(),
                identity_kind::type));

    return static_cast<bool>(
        output);
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


std::uint64_t graph::hash_endpoint_path(
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

    for (const auto& step : steps) {
        mix(static_cast<std::uint8_t>(
            step.kind));
        mix(step.value);
    }

    return hash == 0
        ? 1
        : hash;
}

bool graph::resolve_endpoint_path(
    type_ref root_type,
    std::span<const endpoint_path_step> steps,
    type_ref& output) const noexcept {

    output = {};

    if (!contains(root_type) ||
        steps.empty()) {

        return false;
    }

    auto current_type =
        root_type;

    for (const auto& step : steps) {
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
                    derived_type_kind::const_qualified ||
                derived_value.kind ==
                    derived_type_kind::volatile_qualified)) {

            current_type =
                derived_value.child;
        }

        switch (step.kind) {
        case endpoint_path_step_kind::member: {
            if (step.value >
                (std::numeric_limits<std::uint32_t>::max)()) {

                return false;
            }

            type_handle record;

            if (!named(
                    current_type,
                    record)) {

                return false;
            }

            const auto* entry =
                find(record);

            if (entry == nullptr ||
                !entry->defined() ||
                step.value >=
                    entry->members.count) {

                return false;
            }

            const auto global =
                static_cast<std::size_t>(
                    entry->members.begin) +
                static_cast<std::size_t>(
                    step.value);

            if (global >=
                member_records.size()) {

                return false;
            }

            current_type =
                member_records[
                    global].type;

            break;
        }

        case endpoint_path_step_kind::array_index:
            if (!derived(
                    current_type,
                    derived_value) ||
                derived_value.kind !=
                    derived_type_kind::bounded_array ||
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

endpoint_path_handle graph::find_endpoint_path(
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
            slot.path.value() <=
                endpoint_paths.size()) {

            const auto& record =
                endpoint_paths[
                    slot.path.value() - 1];

            if (record.root_type ==
                    root_type &&
                record.steps.count ==
                    steps.size() &&
                record.steps.begin <=
                    endpoint_path_step_values.size() &&
                record.steps.count <=
                    endpoint_path_step_values.size() -
                        record.steps.begin) {

                const auto existing =
                    std::span<const endpoint_path_step>{
                        endpoint_path_step_values.data() +
                            record.steps.begin,
                        record.steps.count};

                if (std::equal(
                        existing.begin(),
                        existing.end(),
                        steps.begin(),
                        steps.end())) {

                    return slot.path;
                }
            }
        }

        position =
            (position + 1) &
            mask;
    }

    return {};
}

void graph::insert_endpoint_path_index(
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

server_status graph::ensure_endpoint_path_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
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

            const auto path =
                endpoint_path_handle{
                    static_cast<std::uint32_t>(
                        index + 1)};

            const auto& record =
                endpoint_paths[index];

            const auto steps =
                std::span<const endpoint_path_step>{
                    endpoint_path_step_values.data() +
                        record.steps.begin,
                    record.steps.count};

            const auto hash =
                hash_endpoint_path(
                    record.root_type,
                    steps);

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

server_status graph::intern_endpoint_path(
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

    const auto hash =
        hash_endpoint_path(
            root_type,
            steps);

    const auto fingerprint_value =
        fingerprint(hash);

    if (const auto existing =
            find_endpoint_path(
                root_type,
                steps,
                hash,
                fingerprint_value);
        existing) {

        output = existing;

        if (value_type != nullptr) {
            *value_type =
                endpoint_paths[
                    existing.value() - 1].
                    value_type;
        }

        return server_status::success;
    }

    if (endpoint_paths.size() >=
            endpoint_path_handle::maximum_slot ||
        steps.size() >
            (std::numeric_limits<std::uint32_t>::max)() ||
        endpoint_path_step_values.size() >
            (std::numeric_limits<std::uint32_t>::max)() -
                steps.size()) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_endpoint_path_index_capacity(1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    const auto old_path_count =
        endpoint_paths.size();

    const auto old_step_count =
        endpoint_path_step_values.size();

    try {
        endpoint_path_step_values.insert(
            endpoint_path_step_values.end(),
            steps.begin(),
            steps.end());

        endpoint_paths.push_back({
            {
                static_cast<std::uint32_t>(
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

        endpoint_path_step_values.resize(
            old_step_count);

        output = {};

        if (value_type != nullptr) {
            *value_type = {};
        }

        return server_status::io_error;
    }
}

bool graph::endpoint_path(
    endpoint_path_handle path,
    endpoint_path_record& output) const noexcept {

    output = {};

    if (!path ||
        path.value() >
            endpoint_paths.size()) {

        return false;
    }

    output =
        endpoint_paths[
            path.value() - 1];

    return output.steps.begin <=
            endpoint_path_step_values.size() &&
        output.steps.count <=
            endpoint_path_step_values.size() -
                output.steps.begin;
}

std::span<const endpoint_path_step>
graph::endpoint_path_steps(
    endpoint_path_handle path) const noexcept {

    endpoint_path_record record;

    if (!endpoint_path(
            path,
            record)) {

        return {};
    }

    return {
        endpoint_path_step_values.data() +
            record.steps.begin,
        record.steps.count,
    };
}


std::uint64_t graph::link_target_key(
    object_endpoint target) noexcept {

    if (!target.object || !target.member) return 0;
    return (static_cast<std::uint64_t>(target.object.value()) << 32) |
        (static_cast<std::uint64_t>(target.member.value()) + 1);
}

server_status graph::ensure_link_target_index_capacity(
    std::size_t additional) noexcept {

    if (additional > (std::numeric_limits<std::size_t>::max)() - link_target_index_count)
        return server_status::io_error;

    const auto required = link_target_index_count + additional;
    if (!link_target_index.empty() && required <= link_target_index.size() / 2)
        return server_status::success;

    const auto capacity = next_index_capacity(required);
    if (capacity == 0) return server_status::io_error;

    try {
        std::vector<link_target_index_slot> candidate(capacity);
        for (const auto& value : link_target_index) {
            if (value.key != 0)
                insert_link_target_index(candidate, value.key, value.link);
        }
        link_target_index = std::move(candidate);
        return server_status::success;
    }
    catch (...) { return server_status::io_error; }
}

void graph::insert_link_target_index(
    std::vector<link_target_index_slot>& target,
    std::uint64_t key,
    link_handle link_value) const noexcept {

    const auto mask = target.size() - 1;
    auto position = static_cast<std::size_t>(mix64(key)) & mask;
    while (target[position].key != 0) position = (position + 1) & mask;
    target[position] = {key, link_value};
}

link_handle graph::find_link_target(
    object_endpoint target) const noexcept {

    const auto key = link_target_key(target);
    if (key == 0 || link_target_index.empty()) return {};

    const auto mask = link_target_index.size() - 1;
    auto position = static_cast<std::size_t>(mix64(key)) & mask;
    for (std::size_t probe = 0; probe < link_target_index.size(); ++probe) {
        const auto& slot = link_target_index[position];
        if (slot.key == 0) return {};
        if (slot.key == key) return slot.link;
        position = (position + 1) & mask;
    }
    return {};
}

server_status graph::ensure_initialization_target_index_capacity(
    std::size_t additional) noexcept {

    if (additional >
        (std::numeric_limits<std::size_t>::max)() -
            initialization_target_index_count) {

        return server_status::io_error;
    }

    const auto required =
        initialization_target_index_count +
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

            if (value.key != 0) {
                insert_initialization_target_index(
                    candidate,
                    value.key,
                    value.position);
            }
        }

        initialization_target_index =
            std::move(candidate);

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

void graph::insert_initialization_target_index(
    std::vector<initialization_target_index_slot>& target,
    std::uint64_t key,
    std::uint32_t position) const noexcept {

    const auto mask =
        target.size() - 1;

    auto index =
        static_cast<std::size_t>(
            mix64(key)) &
        mask;

    while (target[index].key != 0) {
        index =
            (index + 1) &
            mask;
    }

    target[index] = {
        key,
        position,
        0,
    };
}

std::uint32_t graph::find_initialization_position(
    object_endpoint target) const noexcept {

    const auto key =
        link_target_key(
            target);

    if (key == 0 ||
        initialization_target_index.empty()) {

        return 0;
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
            return 0;
        }

        if (slot.key == key) {
            return slot.position;
        }

        position =
            (position + 1) &
            mask;
    }

    return 0;
}

bool graph::initialization(
    object_endpoint target,
    object_initialization_record& output) const noexcept {

    output = {};

    const auto position =
        find_initialization_position(
            target);

    if (position == 0 ||
        position >
            object_initializations.size()) {

        return false;
    }

    const auto& value =
        object_initializations[
            position - 1];

    if (value.target != target) {
        return false;
    }

    output = value;
    return true;
}


bool graph::reference_binding_compatible(
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

bool graph::scalar_initialization_target(
    type_ref type) const noexcept {

    derived_type_record derived_value;

    for (;;) {
        if (!derived(
                type,
                derived_value)) {

            break;
        }

        if (derived_value.kind ==
            derived_type_kind::const_qualified) {

            return false;
        }

        if (derived_value.kind ==
            derived_type_kind::volatile_qualified) {

            type =
                derived_value.child;
            continue;
        }

        break;
    }

    if (type.kind() ==
        type_ref_kind::intrinsic) {

        const auto intrinsic =
            static_cast<intrinsic_type>(
                type.payload());

        return intrinsic >
                intrinsic_type::none &&
            intrinsic <=
                intrinsic_type::nullptr_type &&
            intrinsic !=
                intrinsic_type::void_type;
    }

    return type.kind() ==
            type_ref_kind::derived &&
        derived(
            type,
            derived_value) &&
        derived_value.kind ==
            derived_type_kind::pointer;
}


bool graph::endpoint_type(
    object_endpoint endpoint,
    type_ref& output) const noexcept {

    output = {};

    const auto* object =
        find(endpoint.object);

    if (object == nullptr ||
        !endpoint.member) {

        return false;
    }

    if (endpoint.member.is_path()) {
        endpoint_path_record path;

        if (!endpoint_path(
                endpoint.member.path(),
                path) ||
            path.root_type !=
                object->type) {

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
        object->type;

    derived_type_record derived_value;

    while (derived(
               object_type,
               derived_value) &&
           (derived_value.kind ==
                derived_type_kind::const_qualified ||
            derived_value.kind ==
                derived_type_kind::volatile_qualified)) {

        object_type =
            derived_value.child;
    }

    type_handle type;

    if (!named(
            object_type,
            type)) {

        return false;
    }

    const auto* value =
        member(
            type,
            member_value);

    if (value == nullptr) {
        return false;
    }

    output = value->type;
    return static_cast<bool>(output);
}

server_status graph::add_initialization(
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
            construction_kind::member_binding ||
        value.kind ==
            construction_kind::object_binding ||
        value.kind ==
            construction_kind::unsupported ||
        !construction_compatible(
            *this,
            target_type,
            value)) {

        return server_status::
            project_configuration_invalid;
    }

    const auto key =
        link_target_key(
            target);

    if (key == 0) {
        return server_status::
            project_configuration_invalid;
    }

    const auto existing =
        find_initialization_position(
            target);

    if (existing != 0) {
        if (existing >
            object_initializations.size()) {

            return server_status::
                project_artifact_invalid;
        }

        auto& current =
            object_initializations[
                existing - 1];

        if (current.target != target) {
            return server_status::
                project_artifact_invalid;
        }

        current.value = value;
        replaced = true;

        return server_status::success;
    }

    if (object_initializations.size() >=
        (std::numeric_limits<std::uint32_t>::max)()) {

        return server_status::io_error;
    }

    const auto prepared =
        ensure_initialization_target_index_capacity(
            1);

    if (!succeeded(prepared)) {
        return prepared;
    }

    try {
        object_initializations.push_back({
            target,
            value,
        });
    }
    catch (...) {
        return server_status::io_error;
    }

    const auto position =
        static_cast<std::uint32_t>(
            object_initializations.size());

    insert_initialization_target_index(
        initialization_target_index,
        key,
        position);

    ++initialization_target_index_count;

    return server_status::success;
}


server_status graph::add_link(
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

        return server_status::project_configuration_invalid;
    }

    if (const auto existing = find_link_target(target); existing) {
        const auto* value = find(existing);
        if (value == nullptr || value->target != target || value->source != source)
            return server_status::project_configuration_invalid;
        output = existing;
        return server_status::success;
    }

    if (links.size() >= static_cast<std::size_t>(link_handle::maximum_slot))
        return server_status::io_error;

    const auto prepared = ensure_link_target_index_capacity(1);
    if (!succeeded(prepared)) return prepared;

    const auto old_count = links.size();
    try { links.push_back({source, target}); }
    catch (...) { return server_status::io_error; }

    output = link_handle{static_cast<std::uint32_t>(links.size())};
    const auto key = link_target_key(target);
    if (key == 0) { links.resize(old_count); output = {}; return server_status::project_configuration_invalid; }

    insert_link_target_index(link_target_index, key, output);
    ++link_target_index_count;
    return server_status::success;
}

}
