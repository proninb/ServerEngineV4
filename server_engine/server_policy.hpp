#pragma once
#include "communication/request_identity.hpp"
#include "communication/server_request.hpp"
#include "server_mode.hpp"

namespace cw::server {

class server_policy final {
public:
    [[nodiscard]] bool allows(
        server_mode mode,
        server_request_kind request) const noexcept;

private:
    [[nodiscard]] static bool is_demo_mutation(server_request_kind request) noexcept;
};

}
