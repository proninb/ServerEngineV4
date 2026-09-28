#include "client_session.hpp"

namespace cw::server {

client_session::client_session(client_session_id id) noexcept : session_id(id) {}
client_session_id client_session::id() const noexcept { return session_id; }
client_session_state client_session::state() const noexcept { return session_state; }
bool client_session::logged_in() const noexcept { return session_state == client_session_state::logged_in; }

client_session_login_result client_session::login(std::string_view name) {
    if (logged_in()) return client_session_login_result::already_logged_in;
    if (!session_id.valid() || name.empty() || name.size() > max_name_size)
        return client_session_login_result::invalid_request;
    client_name.assign(name.data(), name.size());
    session_state = client_session_state::logged_in;
    return client_session_login_result::success;
}

bool client_session::make_request_identity(request_identity& identity) const {
    if (!logged_in()) return false;
    identity.origin = request_origin_kind::tcp;
    identity.name = client_name;
    return true;
}

std::string_view client_session::name() const noexcept { return client_name; }

}
