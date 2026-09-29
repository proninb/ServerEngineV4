#include "server_policy.hpp"
#include <iostream>

using namespace cw::server;

int main() {
    const server_policy policy;

    const server_request_kind all[] = {
        server_request_kind::load,
        server_request_kind::publish,
        server_request_kind::build,
        server_request_kind::unload,
        server_request_kind::rebuild,
        server_request_kind::get_state,
        server_request_kind::get_value,
        server_request_kind::snap_ic,
        server_request_kind::reset_ic,
        server_request_kind::shutdown,
    };

    for (const auto request : all) {
        if (!policy.allows(
                server_mode::full,
                request)) {

            std::cerr << "FULL must allow every current Server request\n";
            return 1;
        }
    }

    if (!policy.allows(
            server_mode::demo,
            server_request_kind::load) ||
        !policy.allows(
            server_mode::demo,
            server_request_kind::unload) ||
        !policy.allows(
            server_mode::demo,
            server_request_kind::get_state) ||
        !policy.allows(
            server_mode::demo,
            server_request_kind::get_value) ||
        !policy.allows(
            server_mode::demo,
            server_request_kind::snap_ic) ||
        !policy.allows(
            server_mode::demo,
            server_request_kind::shutdown)) {

        std::cerr << "DEMO load/work policy failed\n";
        return 2;
    }

    if (policy.allows(
            server_mode::demo,
            server_request_kind::publish) ||
        policy.allows(
            server_mode::demo,
            server_request_kind::build) ||
        policy.allows(
            server_mode::demo,
            server_request_kind::rebuild) ||
        policy.allows(
            server_mode::demo,
            server_request_kind::reset_ic)) {

        std::cerr << "DEMO project-construction denial failed\n";
        return 3;
    }

    return 0;
}
