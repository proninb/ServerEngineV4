/*
 * Fail-closed placeholder until a non-Windows RS256 backend is selected.
 */
#include "rsa_sha256_verifier.hpp"

namespace cw::server {

rsa_sha256_verify_result verify_rsa_sha256(
    std::span<const std::byte>,
    std::span<const std::byte>,
    std::string_view,
    std::span<const std::byte>) noexcept {

    return rsa_sha256_verify_result::unavailable;
}

}
