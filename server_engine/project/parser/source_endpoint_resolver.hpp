// Read-only Header member resolution. Each instance owns its transition cache;
// it cannot mutate Graph, intern names/paths, or publish Source Map entries.
#pragma once
#include "../graph/graph.hpp"
#if defined(CW_SOURCE_TYPE_PREPARE_AB) || defined(CW_SOURCE_TYPE_PREPARE_AUDIT)
#include "source_type_prepare_policy.hpp"
#endif
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace cw::server {
struct source_typed_endpoint {
    type_ref type{};
    type_ref object_type{};
    std::vector<endpoint_path_step> path;
    void reset() noexcept { type = {}; object_type = {}; path.clear(); }
};

enum class source_selector_kind : std::uint8_t { member, index };
struct source_bound_selector final {
    source_selector_kind kind = source_selector_kind::member;
    string_id name{};
    std::uint64_t index = 0;
};
struct source_endpoint_request final {
    identity_ref object{};
    std::span<const source_bound_selector> selectors;
};
struct source_endpoint_result final {
    identity_ref object{};
    source_typed_endpoint endpoint;
    std::vector<identity_ref> dependencies;
};

class source_endpoint_resolver final {
public:
    enum class failure { none, inherited_depth, member_missing, definition_missing,
        object_missing, array_required, index_out_of_range, record_required, endpoint_depth };
    explicit source_endpoint_resolver(const graph& value,
        std::uint64_t* lookups = nullptr) noexcept : G(value), lookup_count(lookups) {}

    [[nodiscard]] server_status begin_endpoint(identity_ref who,
        source_typed_endpoint& output, failure& error) const noexcept {
        output.reset();
        error = failure::none;
        if (!object_type(who, output.type)) {
            error = failure::object_missing;
            return server_status::project_configuration_invalid;
        }
        output.object_type = output.type;
        return server_status::success;
    }

    [[nodiscard]] server_status append_index(source_typed_endpoint& output,
        const derived_type_record& array, std::uint64_t index, failure& error) const noexcept {
        error = failure::none;
        if (index >= array.payload) {
            error = failure::index_out_of_range;
            return server_status::project_configuration_invalid;
        }
        try { output.path.push_back({index, endpoint_path_step_kind::array_index, {}}); }
        catch (...) { return server_status::io_error; }
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
        ++detail::source_resolver_counts.index_steps;
#endif
        output.type = array.child;
        return server_status::success;
    }

    [[nodiscard]] server_status prepare_member(source_typed_endpoint& output,
        type_handle& record, failure& error) const noexcept {
        error = failure::none;
        bool dereference = false;
        const auto found = record_type(output.type, record, dereference);
        if (dereference) {
            try { output.path.push_back({0, endpoint_path_step_kind::dereference, {}}); }
            catch (...) { return server_status::io_error; }
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
            ++detail::source_resolver_counts.dereference_steps;
#endif
        }
        if (!found) {
            error = failure::record_required;
            return server_status::project_configuration_invalid;
        }
        return server_status::success;
    }

    [[nodiscard]] server_status append_member(source_typed_endpoint& output,
        type_handle record, string_id name, type_handle& owner, failure& error) noexcept {
        member_index member;
        type_ref type;
        const auto status = resolve_member(record, name, member, owner, type, output.path, error);
        if (!succeeded(status)) return status;
        try { output.path.push_back({member.value(), endpoint_path_step_kind::member, {}}); }
        catch (...) { return server_status::io_error; }
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
        ++detail::source_resolver_counts.member_steps;
#endif
        output.type = type;
        return server_status::success;
    }

    // All names and WHO are already bound. The request can be read on a worker
    // with its own resolver/cache while G remains frozen. Results stay local.
    [[nodiscard]] server_status resolve(const source_endpoint_request& request,
        source_endpoint_result& output, failure& error, std::size_t& failed_selector) noexcept {
        failed_selector = 0;
        output.object = request.object;
        output.dependencies.clear();
        auto status = begin_endpoint(request.object, output.endpoint, error);
        if (!succeeded(status)) return status;
        try {
            output.dependencies.push_back(request.object);
            for (std::size_t i = 0; i < request.selectors.size(); ++i) {
                failed_selector = i;
                if (output.endpoint.path.size() >= 128) {
                    error = failure::endpoint_depth;
                    return server_status::project_configuration_invalid;
                }
                const auto& selector = request.selectors[i];
                if (selector.kind == source_selector_kind::index) {
                    derived_type_record array;
                    if (!array_type(output.endpoint.type, array)) {
                        error = failure::array_required;
                        return server_status::project_configuration_invalid;
                    }
                    status = append_index(output.endpoint, array, selector.index, error);
                } else {
                    type_handle record, owner;
                    status = prepare_member(output.endpoint, record, error);
                    if (!succeeded(status)) return status;
                    output.dependencies.push_back(G.identity(record));
                    status = append_member(output.endpoint, record, selector.name, owner, error);
                    if (succeeded(status) && owner != record)
                        output.dependencies.push_back(G.identity(owner));
                }
                if (!succeeded(status)) return status;
            }
        } catch (...) { return server_status::io_error; }
        failed_selector = request.selectors.size();
        return server_status::success;
    }

    [[nodiscard]] bool object_type(identity_ref who, type_ref& type) const noexcept {
        type = {};
        const auto* object = G.find(G.find_object(who));
        if (!who || object == nullptr) return false;
        type = object->type;
        return true;
    }

    [[nodiscard]] bool array_type(type_ref type, derived_type_record& array) const noexcept {
        return G.derived(without_cv(type), array) &&
            array.kind == derived_type_kind::bounded_array;
    }

    [[nodiscard]] bool record_type(type_ref type, type_handle& record,
        bool& dereference) const noexcept {
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
        auto& counts = detail::source_resolver_counts;
        ++counts.record_calls;
        counts.record_named += type.kind() == type_ref_kind::named;
        counts.record_derived += type.kind() == type_ref_kind::derived;
        counts.observe(counts.input_types, type.value());
#endif
#if defined(CW_SOURCE_TYPE_PREPARE_AB) || defined(CW_SOURCE_TYPE_PREPARE_V2)
        if (preparation_enabled()) {
            if (type && !prepared_records.empty()) {
                const auto& entry = prepared_records[preparation_slot(type)];
                if (entry.input == type && entry.record) {
                    record = entry.record;
                    dereference = entry.dereference;
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
                    ++counts.preparation_hits;
                    ++counts.record_found;
                    counts.record_dereference += dereference;
#endif
                    return true;
                }
            }
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
            ++counts.preparation_misses;
#endif
        }
#endif
        auto selected = without_cv(type);
        derived_type_record derived;
        dereference = G.derived(selected, derived) &&
            (derived.kind == derived_type_kind::lvalue_reference ||
             derived.kind == derived_type_kind::rvalue_reference);
        if (dereference) selected = without_cv(derived.child);
        const auto found = G.named(selected, record) && G.find(record) &&
            G.find(record)->kind == graph_type_kind::record;
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
        counts.record_found += found;
        counts.record_dereference += dereference;
#endif
#if defined(CW_SOURCE_TYPE_PREPARE_AB) || defined(CW_SOURCE_TYPE_PREPARE_V2)
        // Cache only successful catalog reads. A later Graph append can make a
        // previously missing identity valid; negative results must not survive it.
        if (found && preparation_enabled())
            remember_preparation(type, record, dereference);
#endif
        return found;
    }

    [[nodiscard]] server_status resolve_member(type_handle record, string_id name,
        member_index& member, type_handle& owner, type_ref& type,
        std::vector<endpoint_path_step>& ancestry, failure& error) noexcept {
        error = failure::none;
        depth_exceeded = false;
        member = {};
        owner = {};
        type = {};
        if (cached_transition(record, name, member, owner, type, ancestry))
            return server_status::success;
        const auto begin = ancestry.size();
        const auto status = find_inherited(record, name, member, owner, ancestry);
        if (!succeeded(status)) {
            if (depth_exceeded) error = failure::inherited_depth;
            return status;
        }
        if (!member) { error = failure::member_missing;
            return server_status::project_configuration_invalid; }
        const auto* value = G.member(owner, member);
        if (!value) { error = failure::definition_missing;
            return server_status::project_configuration_invalid; }
        type = value->type;
        remember_transition(record, name, member, owner, type,
            std::span<const endpoint_path_step>{ancestry}.subspan(begin));
        return server_status::success;
    }
private:
#if defined(CW_SOURCE_TYPE_PREPARE_AB) || defined(CW_SOURCE_TYPE_PREPARE_V2)
    [[nodiscard]] static bool preparation_enabled() noexcept {
#ifdef CW_SOURCE_TYPE_PREPARE_AB
        return detail::source_type_prepare_cache;
#else
        return true;
#endif
    }
    struct prepared_record final {
        type_ref input{};
        type_handle record{};
        bool dereference = false;
    };
    static_assert(sizeof(prepared_record) == 12);
    static constexpr std::size_t preparation_slots = 4096;
    [[nodiscard]] static std::size_t preparation_slot(type_ref type) noexcept {
        const auto key = type.value() ^ (type.value() >> 16);
        return static_cast<std::uint32_t>(key * 0x9e3779b9u) >> 20;
    }
    void remember_preparation(type_ref type, type_handle record, bool dereference) const noexcept {
        if (prepared_records.empty()) {
            if (preparation_allocation_attempted) return;
            preparation_allocation_attempted = true;
#ifdef CW_SOURCE_TYPE_PREPARE_TEST
            if (detail::source_type_prepare_force_unavailable) return;
#endif
            try { prepared_records.resize(preparation_slots); }
            catch (...) { return; } // Acceleration failure preserves the original path.
        }
        prepared_records[preparation_slot(type)] = {type, record, dereference};
    }
    // Resolver-local, positive-only. Source cannot redefine Header record types.
    mutable std::vector<prepared_record> prepared_records;
    mutable bool preparation_allocation_attempted = false;
#endif
    [[nodiscard]] type_ref without_cv(type_ref type) const noexcept {
        derived_type_record derived;
        while (G.derived(type, derived) &&
            (derived.kind == derived_type_kind::const_qualified ||
             derived.kind == derived_type_kind::volatile_qualified)) type = derived.child;
        return type;
    }
    // SOURCE-V2-FAST-BIND-02: the Header type/member catalog is immutable
    // for the whole Source pass. Cache resolved typed transitions, NOT text.
    // Every hit contains the exact member, defining owner and base ancestry;
    // member, dependency, endpoint-path and Graph mutation semantics stay intact.
    struct member_transition final {
        std::uint64_t key = 0;
        member_index member{};
        type_handle owner{};
        type_ref value_type{};
        std::uint32_t base_begin = 0;
        std::uint32_t base_count = 0;
    };

    static constexpr std::size_t member_transition_slots = 1u << 16;

    [[nodiscard]] static std::uint64_t transition_key(
        type_handle record, string_id name) noexcept {
        return (static_cast<std::uint64_t>(record.value()) << 32) |
            static_cast<std::uint64_t>(name.value());
    }

    [[nodiscard]] static std::size_t transition_slot(std::uint64_t key) noexcept {
        // Strong 64-bit mixing: low slot bits must depend on both the type and
        // member ID. A direct-mapped table has no per-lookup allocations.
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return static_cast<std::size_t>(key) & (member_transition_slots - 1);
    }

    [[nodiscard]] bool cached_transition(type_handle record, string_id name,
        member_index& member, type_handle& owner, type_ref& value_type,
        std::vector<endpoint_path_step>& ancestry) noexcept {
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
        const auto key = transition_key(record, name);
        auto& counts = detail::source_resolver_counts;
        ++counts.member_calls;
        counts.observe(counts.transition_keys, key);
#endif
        if (member_transitions.empty()) {
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
            ++counts.member_empty;
#endif
            return false;
        }
#ifndef CW_SOURCE_TYPE_PREPARE_AUDIT
        const auto key = transition_key(record, name);
#endif
        const auto& entry = member_transitions[transition_slot(key)];
        if (!key || entry.key != key || !entry.member) {
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
            if (entry.key) ++counts.member_conflicts;
            else ++counts.member_empty;
#endif
            return false;
        }
        if (entry.base_count != 0) {
            const auto begin = static_cast<std::size_t>(entry.base_begin);
            const auto count = static_cast<std::size_t>(entry.base_count);
            if (begin > transition_bases.size() ||
                count > transition_bases.size() - begin) return false;
            try {
                ancestry.insert(ancestry.end(),
                    transition_bases.begin() + begin,
                    transition_bases.begin() + begin + count);
            } catch (...) { return false; }
        }
        member = entry.member;
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
        ++counts.member_hits;
        counts.copied_base_steps += entry.base_count;
#endif
        owner = entry.owner;
        value_type = entry.value_type;
        return true;
    }

    void remember_transition(type_handle record, string_id name,
        member_index member, type_handle owner, type_ref value_type,
        std::span<const endpoint_path_step> base_steps) noexcept {
        if (!record || !name || !member || !owner || !value_type) return;
        // One allocation for the whole table. If it fails, parsing proceeds
        // through the original uncached Graph lookup path.
        if (member_transitions.empty()) {
            if (transition_allocation_attempted) return;
            transition_allocation_attempted = true;
            try { member_transitions.resize(member_transition_slots); }
            catch (...) { return; }
        }
        std::uint32_t begin = 0;
        if (!base_steps.empty()) {
            constexpr auto max_u32 =
                static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)());
            if (transition_bases.size() > max_u32 - base_steps.size()) return;
            begin = static_cast<std::uint32_t>(transition_bases.size());
            try {
                transition_bases.insert(transition_bases.end(),
                    base_steps.begin(), base_steps.end());
            } catch (...) { return; }
        }
        const auto key = transition_key(record, name);
        member_transitions[transition_slot(key)] = {
            key, member, owner, value_type, begin,
            static_cast<std::uint32_t>(base_steps.size())};
    }

    [[nodiscard]] server_status find_inherited(type_handle record, string_id name,
        member_index& index, type_handle& owner,
        std::vector<endpoint_path_step>& ancestry, unsigned depth = 0) noexcept {
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
        ++detail::source_resolver_counts.inherited_lookups;
#endif
        if (depth >= 64) { depth_exceeded = true; return server_status::project_configuration_invalid; }
        if (lookup_count != nullptr) ++*lookup_count;
        index = G.find_member(record, name);
        if (index) { owner = record; return server_status::success; }
        const auto bases = G.bases(record);
        for (std::size_t i = 0; i < bases.size(); ++i) {
            const auto base = G.find_type(bases[i].type);
            if (!base) continue;
            std::vector<endpoint_path_step> local;
            try {
                local = ancestry;
                local.push_back({i, endpoint_path_step_kind::base, {}});
            } catch (...) { return server_status::io_error; }
            member_index found;
            type_handle found_owner;
            auto status = find_inherited(base, name, found, found_owner, local, depth + 1);
            if (!succeeded(status)) return status;
            if (found) { ancestry = std::move(local); index = found;
                owner = found_owner; return server_status::success; }
        }
        return server_status::success;
    }

    const graph& G;
    std::uint64_t* lookup_count = nullptr;
    std::vector<member_transition> member_transitions;
    std::vector<endpoint_path_step> transition_bases;
    bool transition_allocation_attempted = false;
    bool depth_exceeded = false;
};
} // namespace cw::server
