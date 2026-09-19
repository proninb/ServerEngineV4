/*
 * Project semantic identity boundary.
 *
 * Physical-file identity and stable acquisition belong to file/file_identity.
 * Project semantic identity remains separate from byte/configuration identity.
 */
#pragma once

#include <array>
#include <cstddef>

namespace cw::server {

// Reserved for future canonical semantic composition. It is deliberately not
// the aggregate configuration-manifest or construction-content hash.
struct project_semantic_fingerprint final {
    std::array<std::byte, 32> bytes{};

    friend constexpr bool operator==(
        const project_semantic_fingerprint&,
        const project_semantic_fingerprint&) noexcept = default;
};

}
