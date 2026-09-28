/*
 * Microsoft Entra external Authentication provider startup state.
 *
 * V1 validates a locally persisted metadata/signing-key cache so Server startup
 * does not inherently require live Microsoft connectivity. JWT validation is a
 * later slice.
 */
#pragma once

#include "../configuration/server_configuration.hpp"
#include "../server_status.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cw::server {

struct microsoft_entra_signing_key final {
    std::string kid;
    std::string kty;
    std::string alg;
    std::string n;
    std::string e;
    std::string issuer;
};

enum class entra_token_validation_result : std::uint8_t {
    success = 0,
    provider_not_ready,
    invalid_format,
    unsupported_algorithm,
    key_not_found,
    invalid_signing_key,
    invalid_signature,
    missing_claim,
    invalid_issuer,
    invalid_audience,
    invalid_tenant,
    expired,
    not_yet_valid,
    crypto_unavailable,
};

struct entra_token_identity final {
    std::string tenant_id;
    std::string subject;
    std::string object_id;
    std::string client_id;
};

class microsoft_entra_authentication final {
public:
    [[nodiscard]] server_status start(
        const external_authentication_configuration& configuration,
        const std::filesystem::path& configuration_directory,
        std::string& detail) noexcept;

    void stop() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::string_view issuer() const noexcept;
    [[nodiscard]] std::string_view jwks_uri() const noexcept;

    [[nodiscard]] const std::vector<microsoft_entra_signing_key>&
        signing_keys() const noexcept;

    [[nodiscard]] entra_token_validation_result validate_token(
        std::string_view access_token,
        std::chrono::system_clock::time_point now,
        entra_token_identity& identity) const noexcept;

private:
    std::string tenant_id_value;
    std::string audience_value;
    std::string issuer_value;
    std::string jwks_uri_value;
    std::vector<microsoft_entra_signing_key> keys;
    bool initialized = false;
};

}
