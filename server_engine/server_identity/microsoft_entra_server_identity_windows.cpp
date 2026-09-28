#include "microsoft_entra_server_identity.hpp"

#include "../json/json_parser.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

class cert_store_owner final {
public:
    ~cert_store_owner() {
        if (value != nullptr) {
            CertCloseStore(value, 0);
        }
    }

    HCERTSTORE value = nullptr;
};

class cert_context_owner final {
public:
    ~cert_context_owner() {
        if (value != nullptr) {
            CertFreeCertificateContext(value);
        }
    }

    PCCERT_CONTEXT value = nullptr;
};

class ncrypt_key_owner final {
public:
    ~ncrypt_key_owner() {
        if (free_value &&
            value != 0) {

            NCryptFreeObject(value);
        }
    }

    NCRYPT_KEY_HANDLE value = 0;
    bool free_value = false;
};

class winhttp_owner final {
public:
    ~winhttp_owner() {
        if (value != nullptr) {
            WinHttpCloseHandle(value);
        }
    }

    HINTERNET value = nullptr;
};

[[nodiscard]] bool valid_tenant(
    std::string_view value) noexcept {

    if (value.empty()) {
        return false;
    }

    for (const auto ch : value) {
        const bool valid =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' ||
            ch == '.';

        if (!valid) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] int hex_value(
    char ch) noexcept {

    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }

    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }

    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }

    return -1;
}

[[nodiscard]] bool decode_sha1_thumbprint(
    std::string_view text,
    std::array<std::uint8_t, 20>& output) noexcept {

    if (text.size() !=
        output.size() * 2) {

        return false;
    }

    for (std::size_t index = 0;
         index < output.size();
         ++index) {

        const auto high =
            hex_value(text[index * 2]);

        const auto low =
            hex_value(text[index * 2 + 1]);

        if (high < 0 ||
            low < 0) {

            return false;
        }

        output[index] =
            static_cast<std::uint8_t>(
                (high << 4) | low);
    }

    return true;
}

[[nodiscard]] bool sha256(
    const std::uint8_t* data,
    std::size_t size,
    std::array<std::uint8_t, 32>& output) noexcept {

    if (size >
        std::numeric_limits<ULONG>::max()) {

        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;

    if (BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0) != 0) {

        return false;
    }

    DWORD object_size = 0;
    DWORD returned = 0;

    if (BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(
                &object_size),
            sizeof(object_size),
            &returned,
            0) != 0) {

        BCryptCloseAlgorithmProvider(
            algorithm,
            0);
        return false;
    }

    std::vector<std::uint8_t>
        object(object_size);

    BCRYPT_HASH_HANDLE hash = nullptr;

    if (BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            object_size,
            nullptr,
            0,
            0) != 0) {

        BCryptCloseAlgorithmProvider(
            algorithm,
            0);
        return false;
    }

    auto status =
        BCryptHashData(
            hash,
            const_cast<PUCHAR>(
                reinterpret_cast<const UCHAR*>(
                    data)),
            static_cast<ULONG>(
                size),
            0);

    if (status == 0) {
        status =
            BCryptFinishHash(
                hash,
                output.data(),
                static_cast<ULONG>(
                    output.size()),
                0);
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(
        algorithm,
        0);

    return status == 0;
}

[[nodiscard]] std::string base64url(
    const std::uint8_t* data,
    std::size_t size) {

    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";

    std::string output;
    output.reserve(
        (size * 4 + 2) / 3);

    std::size_t index = 0;

    while (index + 3 <= size) {
        const auto value =
            (static_cast<std::uint32_t>(data[index]) << 16) |
            (static_cast<std::uint32_t>(data[index + 1]) << 8) |
            static_cast<std::uint32_t>(data[index + 2]);

        output.push_back(
            alphabet[(value >> 18) & 0x3Fu]);
        output.push_back(
            alphabet[(value >> 12) & 0x3Fu]);
        output.push_back(
            alphabet[(value >> 6) & 0x3Fu]);
        output.push_back(
            alphabet[value & 0x3Fu]);

        index += 3;
    }

    const auto remaining =
        size - index;

    if (remaining == 1) {
        const auto value =
            static_cast<std::uint32_t>(
                data[index]) << 16;

        output.push_back(
            alphabet[(value >> 18) & 0x3Fu]);
        output.push_back(
            alphabet[(value >> 12) & 0x3Fu]);
    }
    else if (remaining == 2) {
        const auto value =
            (static_cast<std::uint32_t>(data[index]) << 16) |
            (static_cast<std::uint32_t>(data[index + 1]) << 8);

        output.push_back(
            alphabet[(value >> 18) & 0x3Fu]);
        output.push_back(
            alphabet[(value >> 12) & 0x3Fu]);
        output.push_back(
            alphabet[(value >> 6) & 0x3Fu]);
    }

    return output;
}

[[nodiscard]] std::string base64url(
    std::string_view value) {

    return base64url(
        reinterpret_cast<const std::uint8_t*>(
            value.data()),
        value.size());
}

[[nodiscard]] std::string json_escape(
    std::string_view value) {

    static constexpr char hex[] =
        "0123456789ABCDEF";

    std::string output;
    output.reserve(value.size());

    for (const auto ch : value) {
        const auto byte =
            static_cast<unsigned char>(
                ch);

        switch (ch) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (byte < 0x20u) {
                output += "\\u00";
                output.push_back(
                    hex[(byte >> 4) & 0x0Fu]);
                output.push_back(
                    hex[byte & 0x0Fu]);
            }
            else {
                output.push_back(ch);
            }
            break;
        }
    }

    return output;
}

[[nodiscard]] std::string form_encode(
    std::string_view value) {

    static constexpr char hex[] =
        "0123456789ABCDEF";

    std::string output;

    for (const auto ch : value) {
        const auto byte =
            static_cast<unsigned char>(
                ch);

        const bool unreserved =
            (byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '-' ||
            byte == '.' ||
            byte == '_' ||
            byte == '~';

        if (unreserved) {
            output.push_back(
                static_cast<char>(
                    byte));
            continue;
        }

        output.push_back('%');
        output.push_back(
            hex[(byte >> 4) & 0x0Fu]);
        output.push_back(
            hex[byte & 0x0Fu]);
    }

    return output;
}

[[nodiscard]] bool acquire_certificate(
    const microsoft_entra_server_identity_configuration& configuration,
    cert_store_owner& store,
    cert_context_owner& certificate,
    std::string& detail) {

    std::array<std::uint8_t, 20>
        thumbprint{};

    if (!decode_sha1_thumbprint(
            configuration.certificate_thumbprint,
            thumbprint)) {

        detail =
            "Server identity certificate_thumbprint must be exactly 40 hexadecimal SHA-1 characters";

        return false;
    }

    const DWORD location =
        configuration.certificate_store ==
                server_identity_certificate_store::current_user
            ? CERT_SYSTEM_STORE_CURRENT_USER
            : CERT_SYSTEM_STORE_LOCAL_MACHINE;

    store.value =
        CertOpenStore(
            CERT_STORE_PROV_SYSTEM_W,
            X509_ASN_ENCODING |
                PKCS_7_ASN_ENCODING,
            0,
            location |
                CERT_STORE_OPEN_EXISTING_FLAG |
                CERT_STORE_READONLY_FLAG,
            L"MY");

    if (store.value == nullptr) {
        detail =
            "Cannot open Windows MY certificate store for Server identity";
        return false;
    }

    CRYPT_HASH_BLOB hash_blob;
    hash_blob.cbData =
        static_cast<DWORD>(
            thumbprint.size());
    hash_blob.pbData =
        thumbprint.data();

    certificate.value =
        CertFindCertificateInStore(
            store.value,
            X509_ASN_ENCODING |
                PKCS_7_ASN_ENCODING,
            0,
            CERT_FIND_SHA1_HASH,
            &hash_blob,
            nullptr);

    if (certificate.value == nullptr) {
        detail =
            "Server identity certificate was not found by thumbprint";
        return false;
    }

    return true;
}

[[nodiscard]] bool acquire_private_key(
    PCCERT_CONTEXT certificate,
    ncrypt_key_owner& key,
    std::string& detail) {

    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE raw = 0;
    DWORD key_spec = 0;
    BOOL must_free = FALSE;

    if (!CryptAcquireCertificatePrivateKey(
            certificate,
            CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG |
                CRYPT_ACQUIRE_SILENT_FLAG,
            nullptr,
            &raw,
            &key_spec,
            &must_free)) {

        detail =
            "Server identity certificate has no accessible CNG private key";

        return false;
    }

    if (key_spec != CERT_NCRYPT_KEY_SPEC) {
        if (must_free) {
            NCryptFreeObject(
                static_cast<NCRYPT_KEY_HANDLE>(
                    raw));
        }

        detail =
            "Server identity certificate private key is not a CNG key";

        return false;
    }

    key.value =
        static_cast<NCRYPT_KEY_HANDLE>(
            raw);

    key.free_value =
        must_free != FALSE;

    return true;
}

[[nodiscard]] bool create_client_assertion(
    const microsoft_entra_server_identity_configuration& configuration,
    std::chrono::system_clock::time_point now,
    PCCERT_CONTEXT certificate,
    NCRYPT_KEY_HANDLE key,
    std::string& output,
    std::string& detail) {

    std::array<std::uint8_t, 32>
        certificate_hash{};

    if (!sha256(
            certificate->pbCertEncoded,
            certificate->cbCertEncoded,
            certificate_hash)) {

        detail =
            "Cannot compute Server identity certificate SHA-256 thumbprint";
        return false;
    }

    std::array<std::uint8_t, 16>
        random{};

    if (BCryptGenRandom(
            nullptr,
            random.data(),
            static_cast<ULONG>(
                random.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {

        detail =
            "Cannot generate Microsoft Entra client assertion jti";
        return false;
    }

    const std::string endpoint =
        "https://login.microsoftonline.com/" +
        configuration.tenant_id +
        "/oauth2/v2.0/token";

    const auto now_seconds =
        std::chrono::duration_cast<
            std::chrono::seconds>(
                now.time_since_epoch())
            .count();

    const auto assertion_expiration =
        now_seconds + 600;

    const std::string header =
        "{\"alg\":\"PS256\",\"typ\":\"JWT\",\"x5t#S256\":\"" +
        base64url(
            certificate_hash.data(),
            certificate_hash.size()) +
        "\"}";

    const std::string payload =
        "{\"aud\":\"" +
        json_escape(endpoint) +
        "\",\"iss\":\"" +
        json_escape(
            configuration.client_id) +
        "\",\"sub\":\"" +
        json_escape(
            configuration.client_id) +
        "\",\"jti\":\"" +
        base64url(
            random.data(),
            random.size()) +
        "\",\"nbf\":" +
        std::to_string(
            now_seconds) +
        ",\"exp\":" +
        std::to_string(
            assertion_expiration) +
        "}";

    const std::string signing_input =
        base64url(header) +
        "." +
        base64url(payload);

    std::array<std::uint8_t, 32>
        signing_hash{};

    if (!sha256(
            reinterpret_cast<const std::uint8_t*>(
                signing_input.data()),
            signing_input.size(),
            signing_hash)) {

        detail =
            "Cannot hash Microsoft Entra client assertion";
        return false;
    }

    BCRYPT_PSS_PADDING_INFO padding{
        BCRYPT_SHA256_ALGORITHM,
        32,
    };

    DWORD signature_size = 0;

    auto status =
        NCryptSignHash(
            key,
            &padding,
            signing_hash.data(),
            static_cast<DWORD>(
                signing_hash.size()),
            nullptr,
            0,
            &signature_size,
            NCRYPT_PAD_PSS_FLAG);

    if (status != ERROR_SUCCESS ||
        signature_size == 0) {

        detail =
            "Cannot size Microsoft Entra PS256 client assertion signature";
        return false;
    }

    std::vector<std::uint8_t>
        signature(signature_size);

    status =
        NCryptSignHash(
            key,
            &padding,
            signing_hash.data(),
            static_cast<DWORD>(
                signing_hash.size()),
            signature.data(),
            signature_size,
            &signature_size,
            NCRYPT_PAD_PSS_FLAG);

    if (status != ERROR_SUCCESS) {
        detail =
            "Cannot sign Microsoft Entra PS256 client assertion";
        return false;
    }

    signature.resize(
        signature_size);

    output =
        signing_input +
        "." +
        base64url(
            signature.data(),
            signature.size());

    return true;
}

enum class response_field : std::uint8_t {
    none = 0,
    access_token,
    token_type,
    expires_in,
    error,
    error_description,
    ignore,
};

struct response_state final {
    bool root_started = false;
    bool root_completed = false;
    bool valid = true;
    response_field field = response_field::none;
    std::string access_token;
    std::string token_type;
    std::uint32_t expires_in = 0;
    std::string error;
    std::string error_description;
};

class response_handler final
    : public json_event_handler {
public:
    explicit response_handler(
        response_state& state)
        : state(state) {
    }

    void object_begin() override {
        if (!state.valid) {
            return;
        }

        if (!state.root_started) {
            state.root_started = true;
            return;
        }

        state.valid = false;
    }

    void object_end() override {
        if (!state.valid ||
            !state.root_started ||
            state.root_completed) {

            state.valid = false;
            return;
        }

        state.root_completed = true;
    }

    void array_begin() override {
        state.valid = false;
    }

    void array_end() override {
        state.valid = false;
    }

    void key(
        std::string_view key) override {

        if (key == "access_token") {
            state.field =
                response_field::access_token;
        }
        else if (key == "token_type") {
            state.field =
                response_field::token_type;
        }
        else if (key == "expires_in") {
            state.field =
                response_field::expires_in;
        }
        else if (key == "error") {
            state.field =
                response_field::error;
        }
        else if (key == "error_description") {
            state.field =
                response_field::error_description;
        }
        else {
            state.field =
                response_field::ignore;
        }
    }

    void value(
        json_value_view value) override {

        switch (state.field) {
        case response_field::access_token:
            state.valid =
                value.get(
                    state.access_token);
            break;

        case response_field::token_type:
            state.valid =
                value.get(
                    state.token_type);
            break;

        case response_field::expires_in:
            state.valid =
                value.get(
                    state.expires_in);
            break;

        case response_field::error:
            state.valid =
                value.get(
                    state.error);
            break;

        case response_field::error_description:
            state.valid =
                value.get(
                    state.error_description);
            break;

        case response_field::ignore:
            break;

        case response_field::none:
            state.valid = false;
            break;
        }

        state.field =
            response_field::none;
    }

private:
    response_state& state;
};

[[nodiscard]] bool request_token(
    const microsoft_entra_server_identity_configuration& configuration,
    std::string_view assertion,
    std::string& response,
    DWORD& status_code,
    std::string& detail) {

    if (!valid_tenant(
            configuration.tenant_id)) {

        detail =
            "Microsoft Entra tenant_id contains unsupported characters";

        return false;
    }

    const std::wstring path =
        L"/" +
        std::wstring(
            configuration.tenant_id.begin(),
            configuration.tenant_id.end()) +
        L"/oauth2/v2.0/token";

    const std::string body =
        "grant_type=client_credentials"
        "&client_id=" +
        form_encode(
            configuration.client_id) +
        "&scope=" +
        form_encode(
            configuration.scope) +
        "&client_assertion_type=" +
        form_encode(
            "urn:ietf:params:oauth:client-assertion-type:jwt-bearer") +
        "&client_assertion=" +
        form_encode(
            assertion);

    if (body.size() >
        std::numeric_limits<DWORD>::max()) {

        detail =
            "Microsoft Entra token request is too large";

        return false;
    }

    winhttp_owner session;
    session.value =
        WinHttpOpen(
            L"ServerEngineV4/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);

    if (session.value == nullptr) {
        detail =
            "Cannot initialize WinHTTP for Microsoft Entra";
        return false;
    }

    WinHttpSetTimeouts(
        session.value,
        10000,
        10000,
        10000,
        10000);

    winhttp_owner connection;
    connection.value =
        WinHttpConnect(
            session.value,
            L"login.microsoftonline.com",
            INTERNET_DEFAULT_HTTPS_PORT,
            0);

    if (connection.value == nullptr) {
        detail =
            "Cannot connect to Microsoft Entra token endpoint";
        return false;
    }

    winhttp_owner request;
    request.value =
        WinHttpOpenRequest(
            connection.value,
            L"POST",
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);

    if (request.value == nullptr) {
        detail =
            "Cannot create Microsoft Entra token request";
        return false;
    }

    DWORD redirect_policy =
        WINHTTP_OPTION_REDIRECT_POLICY_NEVER;

    WinHttpSetOption(
        request.value,
        WINHTTP_OPTION_REDIRECT_POLICY,
        &redirect_policy,
        sizeof(redirect_policy));

    static constexpr wchar_t headers[] =
        L"Content-Type: application/x-www-form-urlencoded\\r\\n";

    if (!WinHttpSendRequest(
            request.value,
            headers,
            static_cast<DWORD>(-1),
            const_cast<char*>(
                body.data()),
            static_cast<DWORD>(
                body.size()),
            static_cast<DWORD>(
                body.size()),
            0) ||
        !WinHttpReceiveResponse(
            request.value,
            nullptr)) {

        detail =
            "Microsoft Entra token request failed";
        return false;
    }

    DWORD status_size =
        sizeof(status_code);

    if (!WinHttpQueryHeaders(
            request.value,
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {

        detail =
            "Cannot read Microsoft Entra token HTTP status";
        return false;
    }

    response.clear();

    for (;;) {
        DWORD available = 0;

        if (!WinHttpQueryDataAvailable(
                request.value,
                &available)) {

            detail =
                "Cannot read Microsoft Entra token response";
            return false;
        }

        if (available == 0) {
            break;
        }

        const auto offset =
            response.size();

        response.resize(
            offset + available);

        DWORD read = 0;

        if (!WinHttpReadData(
                request.value,
                response.data() + offset,
                available,
                &read)) {

            detail =
                "Cannot read Microsoft Entra token response";
            return false;
        }

        response.resize(
            offset + read);
    }

    return true;
}

}

server_status acquire_microsoft_entra_server_identity(
    const microsoft_entra_server_identity_configuration& configuration,
    std::chrono::system_clock::time_point now,
    microsoft_entra_server_identity_token& output,
    std::string& detail) noexcept {

    output = {};
    detail.clear();

    try {
        cert_store_owner store;
        cert_context_owner certificate;

        if (!acquire_certificate(
                configuration,
                store,
                certificate,
                detail)) {

            return server_status::server_identity_start_failed;
        }

        ncrypt_key_owner private_key;

        if (!acquire_private_key(
                certificate.value,
                private_key,
                detail)) {

            return server_status::server_identity_start_failed;
        }

        std::string assertion;

        if (!create_client_assertion(
                configuration,
                now,
                certificate.value,
                private_key.value,
                assertion,
                detail)) {

            return server_status::server_identity_start_failed;
        }

        std::string response;
        DWORD status_code = 0;

        if (!request_token(
                configuration,
                assertion,
                response,
                status_code,
                detail)) {

            return server_status::server_identity_start_failed;
        }

        response_state state;
        response_handler handler{state};

        const auto parsed =
            parse_json(
                response,
                handler);

        if (!parsed.ok() ||
            !state.valid ||
            !state.root_started ||
            !state.root_completed) {

            detail =
                "Microsoft Entra token endpoint returned invalid JSON";

            return server_status::server_identity_start_failed;
        }

        if (status_code < 200 ||
            status_code >= 300) {

            detail =
                "Microsoft Entra token endpoint rejected Server identity";

            if (!state.error.empty()) {
                detail += ": ";
                detail += state.error;
            }

            return server_status::server_identity_start_failed;
        }

        if (state.access_token.empty() ||
            state.token_type != "Bearer" ||
            state.expires_in == 0) {

            detail =
                "Microsoft Entra token response is missing access_token, Bearer token_type, or expires_in";

            return server_status::server_identity_start_failed;
        }

        output.access_token =
            std::move(
                state.access_token);

        output.expires_at =
            now +
            std::chrono::seconds{
                state.expires_in};

        return server_status::success;
    }
    catch (...) {
        output = {};
        detail =
            "Microsoft Entra Server identity initialization failed";

        return server_status::server_identity_start_failed;
    }
}

}
