/*
 * Central Server request access policy.
 */
#include "server_policy.hpp"

namespace cw::server {

bool server_policy::allows(
    const request_identity& identity,
    server_request_kind request) const noexcept {

    if (!is_project_lifecycle(request)) {
        // Access rules for other request domains are intentionally not frozen
        // by this slice. Preserve their current behavior.
        return true;
    }

    switch (identity.origin) {
    case request_origin_kind::internal:
    case request_origin_kind::console:
        return true;

    case request_origin_kind::tcp:
        return is_engineering_studio(identity);
    }

    return false;
}

bool server_policy::is_project_lifecycle(
    server_request_kind request) noexcept {

    switch (request) {
    case server_request_kind::load:
    case server_request_kind::publish:
    case server_request_kind::build:
    case server_request_kind::unload:
    case server_request_kind::rebuild:
        return true;

    case server_request_kind::get_state:
    case server_request_kind::get_value:
    case server_request_kind::shutdown:
        return false;
    }

    return false;
}

bool server_policy::is_engineering_studio(
    const request_identity& identity) noexcept {

    return
        identity.name == "Studio" &&
        identity.subid == "Studio";
}

}
