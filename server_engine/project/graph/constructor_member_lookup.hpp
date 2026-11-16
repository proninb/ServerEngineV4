#pragma once

#include "graph.hpp"
#include <array>

namespace cw::server {

// Resolve the same unqualified field path during Header construction, compiled
// image validation and SHM materialization. Keep base ancestry ABI independent.
struct constructor_member_lookup final {
    type_handle owner{};
    member_index local{};
    member_record member{};
    std::array<std::size_t, 128> bases{};
    std::size_t depth = 0;
    bool accessible = true;
    bool invalid = false;
};

template<class Types>
bool constructor_base_at(const Types& types, std::size_t index, base_record& base) noexcept {
    if constexpr (requires { types.base_at(index, base); }) return types.base_at(index, base);
    else {
        const auto entries = types.base_entries();
        if (index >= entries.size()) return false;
        base = entries[index];
        return true;
    }
}

template<class Types>
bool constructor_member_at(const Types& types, type_handle owner, member_index local,
    member_record& member) noexcept {
    if constexpr (requires { types.member(owner, local); }) {
        const auto* value = types.member(owner, local);
        if (!value) return false;
        member = *value;
        return true;
    } else {
        type_entry entry;
        return types.type(owner, entry) && local &&
            types.member_at(static_cast<std::size_t>(entry.members.begin) + local.value(), member);
    }
}

template<class Types>
bool find_constructor_member(const Types& types, type_handle record, string_id name,
    constructor_member_lookup& output, std::size_t depth = 0) noexcept {
    output = {};
    if (depth >= output.bases.size()) { output.invalid = true; return false; }
    const auto local = types.find_member(record, name);
    if (local) {
        output.owner = record;
        output.local = local;
        if (!constructor_member_at(types, record, local, output.member)) return false;
        output.accessible = depth == 0 || output.member.access != graph_member_access::private_access;
        return true;
    }
    type_entry entry;
    if (!types.type(record, entry)) return false;
    bool found = false;
    for (std::size_t i = 0; i < entry.bases.count; ++i) {
        const auto global = static_cast<std::size_t>(entry.bases.begin) + i;
        base_record base;
        constructor_member_lookup candidate;
        if (!constructor_base_at(types, global, base)) return false;
        if (!find_constructor_member(types, types.find_type(base.type), name, candidate, depth + 1)) {
            if (candidate.invalid) { output.invalid = true; return false; }
            continue;
        }
        // Two base subobjects bearing the name are ambiguous, even when they
        // share a declaration (the nonvirtual diamond case).
        if (found) { output.invalid = true; return false; }
        found = true;
        output = candidate;
        for (std::size_t j = output.depth; j > 0; --j) output.bases[j] = output.bases[j - 1];
        output.bases[0] = global;
        ++output.depth;
        output.accessible &= depth == 0 || base.access != graph_member_access::private_access;
    }
    return found;
}
}
