#pragma once
#include <cstdint>
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
#include <unordered_set>
#endif
namespace cw::server::detail {
#ifdef CW_SOURCE_TYPE_PREPARE_AB
inline bool source_type_prepare_cache = false;
#ifdef CW_SOURCE_TYPE_PREPARE_TEST
inline bool source_type_prepare_force_unavailable = false;
#endif
#endif
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
struct source_resolver_audit {
    std::uint64_t record_calls = 0, record_named = 0, record_derived = 0;
    std::uint64_t record_found = 0, record_dereference = 0;
    std::uint64_t preparation_hits = 0, preparation_misses = 0;
    std::uint64_t member_calls = 0, member_hits = 0, member_empty = 0, member_conflicts = 0;
    std::uint64_t inherited_lookups = 0, copied_base_steps = 0, member_steps = 0;
    std::uint64_t dereference_steps = 0, index_steps = 0;
    bool allocation_failed = false;
    std::unordered_set<std::uint32_t> input_types;
    std::unordered_set<std::uint64_t> transition_keys;
    template<class Set, class Key> void observe(Set& set, Key key) noexcept {
        try { set.insert(key); } catch (...) { allocation_failed = true; }
    }
};
// Diagnostic Source pass runs on its owner; isolated resolver tests may use workers.
inline thread_local source_resolver_audit source_resolver_counts;
#endif
}
