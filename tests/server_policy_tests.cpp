#include "server_policy.hpp"

#include <iostream>

namespace {

using namespace cw::server;

[[nodiscard]] request_identity identity(
    request_origin_kind origin,
    const char* name = "",
    const char* subid = "") {

    request_identity value;
    value.origin = origin;
    value.name = name;
    value.subid = subid;
    return value;
}

[[nodiscard]] bool lifecycle_allowed(
    const server_policy& policy,
    const request_identity& source) {

    return
        policy.allows(source, server_request_kind::load) &&
        policy.allows(source, server_request_kind::publish) &&
        policy.allows(source, server_request_kind::build) &&
        policy.allows(source, server_request_kind::rebuild) &&
        policy.allows(source, server_request_kind::unload);
}

}

int main() {
    const server_policy policy;

    if (!lifecycle_allowed(
            policy,
            identity(request_origin_kind::internal))) {

        std::cerr << "internal Project lifecycle policy failed\n";
        return 1;
    }

    if (!lifecycle_allowed(
            policy,
            identity(request_origin_kind::console))) {

        std::cerr << "console Project lifecycle policy failed\n";
        return 1;
    }

    if (!lifecycle_allowed(
            policy,
            identity(
                request_origin_kind::tcp,
                "Studio",
                "Studio"))) {

        std::cerr << "Studio/Studio Project lifecycle policy failed\n";
        return 1;
    }

    if (lifecycle_allowed(
            policy,
            identity(
                request_origin_kind::tcp,
                "Studio",
                "HMI")) ||
        lifecycle_allowed(
            policy,
            identity(
                request_origin_kind::tcp,
                "Studio",
                "Viewer")) ||
        lifecycle_allowed(
            policy,
            identity(
                request_origin_kind::tcp,
                "Other"))) {

        std::cerr << "TCP Project lifecycle denial policy failed\n";
        return 1;
    }

    // This slice intentionally preserves existing access for non-lifecycle
    // requests until their policy is defined.
    if (!policy.allows(
            identity(request_origin_kind::tcp, "Other"),
            server_request_kind::get_state) ||
        !policy.allows(
            identity(request_origin_kind::tcp, "Other"),
            server_request_kind::get_value) ||
        !policy.allows(
            identity(request_origin_kind::tcp, "Other"),
            server_request_kind::shutdown)) {

        std::cerr << "non-lifecycle policy compatibility failed\n";
        return 1;
    }

    return 0;
}
