#include "server_policy.hpp"

namespace cw::server {

bool server_policy::allows(
    server_mode mode,
    server_request_kind request) const noexcept {

    if (mode == server_mode::full) {
        return true;
    }

    return !is_demo_mutation(request);
}

bool server_policy::is_demo_mutation(
    server_request_kind request) noexcept {

    switch (request) {
    case server_request_kind::publish:
    case server_request_kind::build:
    case server_request_kind::rebuild:
    case server_request_kind::reset_ic:
    case server_request_kind::delete_ic:
    case server_request_kind::run:
    case server_request_kind::freeze:
        return true;

    case server_request_kind::load:
    case server_request_kind::unload:
    case server_request_kind::get_state:
    case server_request_kind::get_object:
    case server_request_kind::get_type:
    case server_request_kind::get_value:
    case server_request_kind::snap_ic:
    case server_request_kind::list_ic:
    case server_request_kind::shutdown:
        return false;
    }

    return true;
}

}
