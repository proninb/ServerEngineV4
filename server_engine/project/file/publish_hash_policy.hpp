#pragma once
namespace cw::server::detail {
#ifdef CW_PUBLISH_NO_SHA256_AB
inline bool publish_hash_reference = true;
#endif
}

#ifdef CW_FILE_HASH_AUDIT
#include <atomic>
#include <cstdint>
namespace cw::server::detail {
inline std::atomic<std::uint64_t> file_hash_calls = 0, file_hash_bytes = 0;
}
#endif
