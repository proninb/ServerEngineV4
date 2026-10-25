#pragma once
#include <cstddef>
#include <cstdint>
namespace cw::server::detail {
#ifdef CW_SOURCE_STRING_SCRATCH_AB
// Experiment-only; set before the single Source pass begins.
inline bool source_string_scratch_reuse = false;
#ifdef CW_SOURCE_STRING_SCRATCH_AUDIT
inline std::uint64_t string_scratch_statements = 0;
inline std::uint64_t string_bytes_growths = 0, string_bytes_requested = 0;
inline std::uint64_t string_paths_growths = 0, string_paths_requested = 0;
template<class T> void audit_string_growth(std::size_t before, const T& buffer,
    std::uint64_t& calls, std::uint64_t& bytes) noexcept {
    if (before != buffer.capacity()) {
        ++calls;
        bytes += buffer.capacity() * sizeof(typename T::value_type);
    }
}
#endif
#endif
}
