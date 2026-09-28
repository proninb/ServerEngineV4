#include "communication/session/client_session.hpp"
#include <iostream>
#include <string>
using namespace cw::server;

int main() {
    client_session s{client_session_id{1}};
    request_identity id;
    if (s.logged_in() || s.make_request_identity(id)) return 1;
    if (s.login("Studio") != client_session_login_result::success) return 2;
    if (!s.logged_in() || !s.make_request_identity(id) || id.name != "Studio") return 3;
    if (s.login("Other") != client_session_login_result::already_logged_in) return 4;

    client_session invalid{client_session_id{}};
    if (invalid.login("Studio") != client_session_login_result::invalid_request) return 5;

    client_session a{client_session_id{10}}, b{client_session_id{11}};
    if (a.login("Studio") != client_session_login_result::success ||
        b.login("Studio") != client_session_login_result::success ||
        a.name() != b.name()) return 6;

    std::string long_name(129, 'x');
    client_session c{client_session_id{12}};
    if (c.login(long_name) != client_session_login_result::invalid_request) return 7;
    return 0;
}
