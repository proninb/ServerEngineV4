#pragma once

#include "../graph/graph.hpp"
#include "../graph/enum_semantics.hpp"
#include <limits>

namespace cw::server {

// No global WHO/type database: enumerate only the enums already published in G.
// A numeric value's operand carries its enum WHO transiently inside the parser.
// It is validated and erased before entering G/persistence.
inline bool enum_constant(const graph& G, const identity_space& identities,
    identity_ref scope, string_id name, construction_value& value,
    bool qualified = false) noexcept {
    if (G.enum_types().empty()) return false;
    for (auto current = scope; current;) {
        bool found = false;
        for (auto handle : G.enum_types()) {
            const auto* entry = G.find(handle);
            const auto identity = G.identity(handle);
            identity_record record;
            if (!entry || !(entry->flags & graph_enum_body) ||
                !identities.record(identity, record)) continue;
            if (qualified ? identity != current :
                (record.parent != current || (entry->flags & graph_enum_scoped))) continue;
            const auto index = G.find_member(handle, name);
            const auto* constant = G.construction(handle, index);
            if (!constant) continue;
            if (found) return false; // Ambiguous unscoped injection.
            value = *constant;
            value.operand = identity.value();
            found = true;
        }
        if (found) return true;
        if (qualified || current == identities.root()) return false;
        identity_record record;
        if (!identities.record(current, record)) return false;
        current = record.parent;
    }
    return false;
}

inline bool normalize_enum_constant(const graph& G, type_ref target,
    construction_value& value) noexcept {
    if (G.enum_types().empty()) return true;
    derived_type_record qualifiers;
    auto scalar_target = target;
    while (G.derived(scalar_target, qualifiers) &&
        (qualifiers.kind == derived_type_kind::const_qualified ||
         qualifiers.kind == derived_type_kind::volatile_qualified)) scalar_target = qualifiers.child;
    type_handle target_handle;
    const auto* target_entry = G.named(scalar_target, target_handle) ? G.find(target_handle) : nullptr;
    if (!value.operand) {
        return !(target_entry && target_entry->kind == graph_type_kind::enumeration) ||
            value.kind == construction_kind::zero;
    }
    if (value.kind != construction_kind::signed_integer &&
        value.kind != construction_kind::unsigned_integer) return true;
    const auto source = identity_ref::from_raw(value.operand);
    const auto* entry = G.find(G.find_type(source));
    if (!entry || entry->kind != graph_type_kind::enumeration) return false;
    derived_type_record d;
    while (G.derived(target, d) && (d.kind == derived_type_kind::const_qualified ||
        d.kind == derived_type_kind::volatile_qualified)) target = d.child;
    type_handle handle;
    const auto* destination = G.named(target, handle) ? G.find(handle) : nullptr;
    if (destination && destination->kind == graph_type_kind::enumeration) {
        if (G.identity(handle) != source) return false;
    } else if (entry->flags & graph_enum_scoped) return false;
    value.operand = 0;
    return true;
}


} // namespace cw::server
