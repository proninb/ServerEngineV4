/*
 * Microsoft Entra JWT access-token validation.
 */
#include "entra_jwt.hpp"

#include "crypto/rsa_sha256_verifier.hpp"
#include "../json/json_parser.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cw::server {
namespace {

[[nodiscard]] bool decode_base64url(
    std::string_view input,
    std::vector<std::byte>& output) {

    output.clear();

    if (input.empty() ||
        input.size() % 4 == 1) {

        return false;
    }

    output.reserve(
        (input.size() * 3) / 4 + 3);

    std::uint32_t accumulator = 0;
    unsigned bits = 0;

    for (char ch : input) {
        std::uint32_t value = 0;

        if (ch >= 'A' && ch <= 'Z') {
            value =
                static_cast<std::uint32_t>(
                    ch - 'A');
        } else if (ch >= 'a' && ch <= 'z') {
            value =
                26u +
                static_cast<std::uint32_t>(
                    ch - 'a');
        } else if (ch >= '0' && ch <= '9') {
            value =
                52u +
                static_cast<std::uint32_t>(
                    ch - '0');
        } else if (ch == '-') {
            value = 62u;
        } else if (ch == '_') {
            value = 63u;
        } else {
            return false;
        }

        accumulator =
            (accumulator << 6u) |
            value;

        bits += 6u;

        if (bits >= 8u) {
            bits -= 8u;

            output.push_back(
                static_cast<std::byte>(
                    (accumulator >> bits) &
                    0xffu));
        }
    }

    if (bits != 0u) {
        const auto mask =
            (std::uint32_t{1} << bits) -
            1u;

        if ((accumulator & mask) != 0u) {
            output.clear();
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool decode_base64url_text(
    std::string_view input,
    std::string& output) {

    std::vector<std::byte> bytes;

    if (!decode_base64url(
            input,
            bytes)) {

        return false;
    }

    output.assign(
        reinterpret_cast<const char*>(
            bytes.data()),
        bytes.size());

    return true;
}

struct jwt_header final {
    std::string alg;
    std::string kid;
    bool alg_seen = false;
    bool kid_seen = false;
};

struct jwt_claims final {
    std::string issuer;
    std::string audience;
    std::string tenant_id;
    std::string subject;
    std::string object_id;
    std::string client_id;

    std::int64_t expires = 0;
    std::int64_t not_before = 0;

    bool issuer_seen = false;
    bool audience_seen = false;
    bool tenant_seen = false;
    bool expires_seen = false;
    bool not_before_seen = false;
    bool subject_seen = false;
    bool object_id_seen = false;
    bool azp_seen = false;
    bool appid_seen = false;
};

class flat_object_handler final : public json_event_handler {
public:
    enum class kind : std::uint8_t {
        header,
        claims,
    };

    flat_object_handler(
        kind parse_kind,
        jwt_header* header,
        jwt_claims* claims)
        : parse_kind(parse_kind),
          header(header),
          claims(claims) {
    }

    void object_begin() override {
        ++depth;

        if (depth == 1) {
            root_started = true;
        }

        current_key.clear();
    }

    void object_end() override {
        if (depth == 0) {
            valid_value = false;
            return;
        }

        if (depth == 1) {
            root_completed = true;
        }

        --depth;
        current_key.clear();
    }

    void array_begin() override {
        ++depth;
        current_key.clear();
    }

    void array_end() override {
        if (depth == 0) {
            valid_value = false;
            return;
        }

        --depth;
        current_key.clear();
    }

    void key(
        std::string_view key) override {

        if (depth == 1) {
            current_key.assign(
                key.data(),
                key.size());
        } else {
            current_key.clear();
        }
    }

    void value(
        json_value_view value) override {

        if (!valid_value ||
            depth != 1 ||
            current_key.empty()) {

            return;
        }

        if (parse_kind ==
            kind::header) {

            read_header(
                value);
        } else {
            read_claim(
                value);
        }

        current_key.clear();
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_value &&
               root_started &&
               root_completed &&
               depth == 0;
    }

private:
    void read_header(
        json_value_view value) {

        if (current_key == "alg") {
            if (header->alg_seen ||
                !value.get(header->alg)) {

                valid_value = false;
                return;
            }

            header->alg_seen = true;
            return;
        }

        if (current_key == "kid") {
            if (header->kid_seen ||
                !value.get(header->kid)) {

                valid_value = false;
                return;
            }

            header->kid_seen = true;
        }
    }

    void read_claim(
        json_value_view value) {

        if (current_key == "iss") {
            read_string(
                value,
                claims->issuer,
                claims->issuer_seen);
            return;
        }

        if (current_key == "aud") {
            read_string(
                value,
                claims->audience,
                claims->audience_seen);
            return;
        }

        if (current_key == "tid") {
            read_string(
                value,
                claims->tenant_id,
                claims->tenant_seen);
            return;
        }

        if (current_key == "sub") {
            read_string(
                value,
                claims->subject,
                claims->subject_seen);
            return;
        }

        if (current_key == "oid") {
            read_string(
                value,
                claims->object_id,
                claims->object_id_seen);
            return;
        }

        if (current_key == "azp") {
            read_string(
                value,
                claims->client_id,
                claims->azp_seen);
            return;
        }

        if (current_key == "appid") {
            if (claims->appid_seen ||
                claims->azp_seen ||
                !value.get(
                    claims->client_id)) {

                valid_value = false;
                return;
            }

            claims->appid_seen = true;
            return;
        }

        if (current_key == "exp") {
            if (claims->expires_seen ||
                !value.get(
                    claims->expires)) {

                valid_value = false;
                return;
            }

            claims->expires_seen = true;
            return;
        }

        if (current_key == "nbf") {
            if (claims->not_before_seen ||
                !value.get(
                    claims->not_before)) {

                valid_value = false;
                return;
            }

            claims->not_before_seen = true;
        }
    }

    void read_string(
        json_value_view value,
        std::string& output,
        bool& seen) {

        if (seen ||
            !value.get(output)) {

            valid_value = false;
            return;
        }

        seen = true;
    }

    kind parse_kind;
    jwt_header* header = nullptr;
    jwt_claims* claims = nullptr;

    std::size_t depth = 0;
    bool root_started = false;
    bool root_completed = false;
    bool valid_value = true;
    std::string current_key;
};

[[nodiscard]] bool parse_header(
    std::string_view encoded,
    jwt_header& header) {

    std::string text;

    if (!decode_base64url_text(
            encoded,
            text)) {

        return false;
    }

    flat_object_handler handler{
        flat_object_handler::kind::header,
        &header,
        nullptr,
    };

    const auto parsed =
        parse_json(
            text,
            handler);

    return parsed.ok() &&
           handler.valid();
}

[[nodiscard]] bool parse_claims(
    std::string_view encoded,
    jwt_claims& claims) {

    std::string text;

    if (!decode_base64url_text(
            encoded,
            text)) {

        return false;
    }

    flat_object_handler handler{
        flat_object_handler::kind::claims,
        nullptr,
        &claims,
    };

    const auto parsed =
        parse_json(
            text,
            handler);

    return parsed.ok() &&
           handler.valid();
}

[[nodiscard]] const microsoft_entra_signing_key*
find_key(
    const std::vector<microsoft_entra_signing_key>& keys,
    std::string_view kid) noexcept {

    for (const auto& key : keys) {
        if (key.kid == kid) {
            return &key;
        }
    }

    return nullptr;
}

}

entra_token_validation_result
validate_microsoft_entra_access_token(
    std::string_view access_token,
    std::string_view tenant_id,
    std::string_view audience,
    std::string_view issuer,
    const std::vector<microsoft_entra_signing_key>& signing_keys,
    std::chrono::system_clock::time_point now,
    entra_token_identity& identity) noexcept {

    identity = {};

    try {
        const auto first_dot =
            access_token.find('.');

        if (first_dot ==
            std::string_view::npos) {

            return entra_token_validation_result::invalid_format;
        }

        const auto second_dot =
            access_token.find(
                '.',
                first_dot + 1);

        if (second_dot ==
                std::string_view::npos ||
            access_token.find(
                '.',
                second_dot + 1) !=
                std::string_view::npos) {

            return entra_token_validation_result::invalid_format;
        }

        const auto header_segment =
            access_token.substr(
                0,
                first_dot);

        const auto payload_segment =
            access_token.substr(
                first_dot + 1,
                second_dot - first_dot - 1);

        const auto signature_segment =
            access_token.substr(
                second_dot + 1);

        if (header_segment.empty() ||
            payload_segment.empty() ||
            signature_segment.empty()) {

            return entra_token_validation_result::invalid_format;
        }

        jwt_header header;

        if (!parse_header(
                header_segment,
                header)) {

            return entra_token_validation_result::invalid_format;
        }

        if (!header.alg_seen ||
            header.alg != "RS256") {

            return entra_token_validation_result::unsupported_algorithm;
        }

        if (!header.kid_seen ||
            header.kid.empty()) {

            return entra_token_validation_result::missing_claim;
        }

        const auto* key =
            find_key(
                signing_keys,
                header.kid);

        if (key == nullptr) {
            return entra_token_validation_result::key_not_found;
        }

        if (key->kty != "RSA" ||
            (!key->alg.empty() &&
             key->alg != "RS256")) {

            return entra_token_validation_result::invalid_signing_key;
        }

        std::vector<std::byte> modulus;
        std::vector<std::byte> exponent;
        std::vector<std::byte> signature;

        if (!decode_base64url(
                key->n,
                modulus) ||
            !decode_base64url(
                key->e,
                exponent) ||
            !decode_base64url(
                signature_segment,
                signature)) {

            return entra_token_validation_result::invalid_format;
        }

        const auto signing_input =
            access_token.substr(
                0,
                second_dot);

        const auto verified =
            verify_rsa_sha256(
                modulus,
                exponent,
                signing_input,
                signature);

        switch (verified) {
        case rsa_sha256_verify_result::success:
            break;

        case rsa_sha256_verify_result::invalid_key:
            return entra_token_validation_result::invalid_signing_key;

        case rsa_sha256_verify_result::invalid_signature:
            return entra_token_validation_result::invalid_signature;

        case rsa_sha256_verify_result::unavailable:
            return entra_token_validation_result::crypto_unavailable;

        case rsa_sha256_verify_result::failure:
            return entra_token_validation_result::invalid_signature;
        }

        jwt_claims claims;

        if (!parse_claims(
                payload_segment,
                claims)) {

            return entra_token_validation_result::invalid_format;
        }

        if (!claims.issuer_seen ||
            !claims.audience_seen ||
            !claims.tenant_seen ||
            !claims.expires_seen) {

            return entra_token_validation_result::missing_claim;
        }

        if (claims.issuer != issuer) {
            return entra_token_validation_result::invalid_issuer;
        }

        if (!key->issuer.empty() &&
            key->issuer != claims.issuer) {

            return entra_token_validation_result::invalid_issuer;
        }

        if (claims.audience != audience) {
            return entra_token_validation_result::invalid_audience;
        }

        if (claims.tenant_id != tenant_id) {
            return entra_token_validation_result::invalid_tenant;
        }

        const auto now_seconds =
            std::chrono::duration_cast<
                std::chrono::seconds>(
                    now.time_since_epoch())
                .count();

        if (now_seconds >=
            claims.expires) {

            return entra_token_validation_result::expired;
        }

        if (claims.not_before_seen &&
            now_seconds <
                claims.not_before) {

            return entra_token_validation_result::not_yet_valid;
        }

        identity.tenant_id =
            std::move(
                claims.tenant_id);

        identity.subject =
            std::move(
                claims.subject);

        identity.object_id =
            std::move(
                claims.object_id);

        identity.client_id =
            std::move(
                claims.client_id);

        return entra_token_validation_result::success;
    }
    catch (...) {
        identity = {};
        return entra_token_validation_result::invalid_format;
    }
}

entra_token_validation_result
microsoft_entra_authentication::validate_token(
    std::string_view access_token,
    std::chrono::system_clock::time_point now,
    entra_token_identity& identity) const noexcept {

    if (!initialized) {
        identity = {};
        return entra_token_validation_result::provider_not_ready;
    }

    return validate_microsoft_entra_access_token(
        access_token,
        tenant_id_value,
        audience_value,
        issuer_value,
        keys,
        now,
        identity);
}

}
