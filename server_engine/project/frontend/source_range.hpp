/*
 * Exact transient source-byte range used by frontend diagnostics.
 */
#pragma once

#include <cstdint>

namespace cw::server {

struct source_range final {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

static_assert(sizeof(source_range) == 8);

}
