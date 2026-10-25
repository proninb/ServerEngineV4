#pragma once
#include <cstdint>
#include <cstdlib>
namespace cw::server::path_intern02 {
inline constexpr unsigned prehashed=1, reserve_vectors=2, reserve_index=4,
    single_probe=8, cached_hashes=16, skip_existing=32, skip_all=64;
inline unsigned initial_mode() noexcept {
    const auto* text=std::getenv("CW_PATH_INTERN_MODE");
    const auto mode=text?static_cast<unsigned>(std::atoi(text)):0;
    // Oracle variants are selected only inside replay, never by a PUBLISH arm.
    return mode==single_probe || mode==cached_hashes || mode==(single_probe|cached_hashes) ? mode:0;
}
inline unsigned mode=initial_mode();
inline std::uint64_t supplied_hash=0;
inline bool enabled(unsigned bit) noexcept { return (mode&bit)!=0; }
}
