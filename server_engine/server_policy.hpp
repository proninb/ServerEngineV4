/*
 * Central Server request access policy.
 *
 * Policy decides whether a transport-neutral request identity may submit a
 * Server request. Authentication and command execution remain separate.
 */
#pragma once

#include "communication/request_identity.hpp"
#include "communication/server_request.hpp"

namespace cw::server {

// Process-level access policy for requests entering from Communication.
class server_policy final {
public:
    [[nodiscard]] bool allows(
        const request_identity& identity,
        server_request_kind request) const noexcept;

private:
    [[nodiscard]] static bool is_project_lifecycle(
        server_request_kind request) noexcept;

    [[nodiscard]] static bool is_engineering_studio(
        const request_identity& identity) noexcept;
};

}
