/*
 * Microsoft Entra access-token validation implementation boundary.
 *
 * Transport/session code calls microsoft_entra_authentication::validate_token();
 * this free function keeps JWT parsing and cryptography out of provider lifecycle.
 */
#pragma once

#include "microsoft_entra.hpp"

namespace cw::server {

[[nodiscard]] entra_token_validation_result
validate_microsoft_entra_access_token(
    std::string_view access_token,
    std::string_view tenant_id,
    std::string_view audience,
    std::string_view issuer,
    const std::vector<microsoft_entra_signing_key>& signing_keys,
    std::chrono::system_clock::time_point now,
    entra_token_identity& identity) noexcept;

}
