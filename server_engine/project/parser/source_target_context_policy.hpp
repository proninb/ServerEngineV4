#pragma once
#include <cstdint>
namespace cw::server::detail {
#ifdef CW_SOURCE_TARGET_CONTEXT_AB
inline bool source_target_context = false;
#endif
#ifdef CW_SOURCE_TARGET_CONTEXT_AUDIT
struct target_context_audit {
    std::uint64_t lookup_calls = 0, name_hits = 0;
    std::uint64_t target_endpoints = 0, type_hits = 0, rhs_endpoints = 0;
    std::uint64_t declaration_seeds = 0, root_resets = 0, namespace_resets = 0;
};
inline target_context_audit target_context_counts;
#endif
}
