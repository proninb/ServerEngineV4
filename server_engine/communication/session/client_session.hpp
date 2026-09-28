/*
 * Transport-neutral Client Session state.
 */
#pragma once
#include "../request_identity.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cw::server {

struct client_session_id final {
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(client_session_id, client_session_id) noexcept = default;
};

enum class client_session_state : std::uint8_t {
    connected = 0,
    logged_in,
};

enum class client_session_login_result : std::uint8_t {
    success = 0,
    already_logged_in,
    invalid_request,
};

class client_session final {
public:
    explicit client_session(client_session_id id) noexcept;
    [[nodiscard]] client_session_id id() const noexcept;
    [[nodiscard]] client_session_state state() const noexcept;
    [[nodiscard]] bool logged_in() const noexcept;
    [[nodiscard]] client_session_login_result login(std::string_view name);
    [[nodiscard]] bool make_request_identity(request_identity& identity) const;
    [[nodiscard]] std::string_view name() const noexcept;

private:
    static constexpr std::size_t max_name_size = 128;
    client_session_id session_id;
    client_session_state session_state = client_session_state::connected;
    std::string client_name;
};

}
