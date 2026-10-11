#pragma once
// Diagnostic-only counter invariants. Pure, constexpr, no runtime allocation.
#include <cstdint>
namespace v340_diag {
constexpr bool named(std::uint64_t attempts,std::uint64_t hits,
    std::uint64_t misses,std::uint64_t rejected) noexcept {
    return hits<=attempts && misses<=attempts && rejected<=attempts &&
        hits+misses+rejected==attempts;
}
constexpr bool fallback(std::uint64_t attempts,std::uint64_t qualified,
    std::uint64_t contextual,std::uint64_t modifiers,std::uint64_t other,
    std::uint64_t success,std::uint64_t failure) noexcept {
    return qualified<=attempts && contextual<=attempts && modifiers<=attempts &&
        other<=attempts && success<=attempts && failure<=attempts &&
        qualified+contextual+modifiers+other==attempts &&
        success+failure==attempts;
}
constexpr bool shape(std::uint64_t attempts,std::uint64_t failed,
    std::uint64_t zero,std::uint64_t one,std::uint64_t many) noexcept {
    return failed<=attempts && zero<=attempts && one<=attempts &&
        many<=attempts && failed+zero+one+many==attempts;
}
}
