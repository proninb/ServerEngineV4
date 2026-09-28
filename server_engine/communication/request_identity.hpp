/*
 * Transport-neutral identity attached to a queued Server request.
 *
 * This is request-origin/application metadata only. It is not an authenticated
 * user identity and does not define authorization credentials.
 */
#pragma once

#include <cstdint>
#include <string>

namespace cw::server {

enum class request_origin_kind : std::uint8_t {
    internal,
    console,
    tcp,
};

// Identifies the request origin presented to Server Policy.
struct request_identity final {
    request_origin_kind origin = request_origin_kind::internal;

    // Logical client/application family for TCP sessions.
    std::string name;

    // Optional specialization within the client/application family.
    std::string subid;
};

}
