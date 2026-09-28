/*
 * Platform cryptographic boundary for RSASSA-PKCS1-v1_5 with SHA-256.
 *
 * JWT parsing/claim validation never depends on a platform crypto API directly.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cw::server {

enum class rsa_sha256_verify_result : std::uint8_t {
    success = 0,
    invalid_key,
    invalid_signature,
    unavailable,
    failure,
};

[[nodiscard]] rsa_sha256_verify_result verify_rsa_sha256(
    std::span<const std::byte> modulus,
    std::span<const std::byte> exponent,
    std::string_view message,
    std::span<const std::byte> signature) noexcept;

}
