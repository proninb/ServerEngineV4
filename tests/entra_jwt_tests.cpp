#include "authentication/microsoft_entra.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace cw::server;

[[nodiscard]] bool write_text(
    const std::filesystem::path& path,
    std::string_view text) {

    std::ofstream stream(
        path,
        std::ios::binary |
        std::ios::trunc);

    if (!stream) {
        return false;
    }

    stream.write(
        text.data(),
        static_cast<std::streamsize>(
            text.size()));

    return static_cast<bool>(stream);
}

[[nodiscard]] std::chrono::system_clock::time_point at_seconds(
    std::int64_t seconds) {

    return std::chrono::system_clock::time_point{
        std::chrono::seconds{seconds},
    };
}

[[nodiscard]] bool run_windows_validation() {
    const auto directory =
        std::filesystem::current_path() /
        "entra_jwt_test";

    std::error_code error;

    std::filesystem::remove_all(
        directory,
        error);

    std::filesystem::create_directories(
        directory,
        error);

    if (error) {
        return false;
    }

    const auto cleanup = [&]() {
        std::error_code cleanup_error;
        std::filesystem::remove_all(
            directory,
            cleanup_error);
    };

    const auto cache =
        directory /
        "entra.cache";

    const std::string cache_text =
        "{\n"
        "  \"version\": 1,\n"
        "  \"tenant_id\": \"tenant-1\",\n"
        "  \"audience\": \"api-client-id\",\n"
        "  \"issuer\": \"https://login.microsoftonline.com/tenant-1/v2.0\",\n"
        "  \"jwks_uri\": \"https://login.microsoftonline.com/tenant-1/discovery/v2.0/keys\",\n"
        "  \"retrieved_at\": \"2026-09-28T00:00:00Z\",\n"
        "  \"keys\": [{\n"
        "    \"kid\": \"test-key-1\",\n"
        "    \"kty\": \"RSA\",\n"
        "    \"alg\": \"RS256\",\n"
        "    \"n\": \"q--lCOG6PZ3VL5J9FaHUDyzrHAE2PJQ1SembirfjwXCBetGpeT69d2lFG0Tabd2dZOOJB2Y1RWjfIxej1Mn6stzQBfMxs9L30cOThm-_Gajk2Y36lpTmO58gPJmsZBoq1ILKFYy2Zoyji2QioZmS1fdJR8rDClhhmpxfryOrnRGT0vZ4GQapH4m4SbDqnNFCDALDP4MJgB9UgifEGq6RnxBdReoTzMX5YWjNRHT5IDHxs4MamuBRM3FBSR9jRcRLQld6uD2s084eft8DCQRKqbhw7qCvLfzk2iWFJHIb0FKcnpFqWofOqUiYmB0eXGIlyBN9ntPzbkd1BoFIRHalCw\",\n"
        "    \"e\": \"AQAB\",\n"
        "    \"issuer\": \"https://login.microsoftonline.com/tenant-1/v2.0\"\n"
        "  }]\n"
        "}\n";

    if (!write_text(
            cache,
            cache_text)) {

        cleanup();
        return false;
    }

    external_authentication_configuration configuration;
    configuration.tenant_id =
        "tenant-1";
    configuration.audience =
        "api-client-id";

    microsoft_entra_authentication entra;
    std::string detail;

    if (!succeeded(
            entra.start(
                configuration,
                directory,
                detail)) ||
        !entra.ready()) {

        cleanup();
        return false;
    }

    constexpr std::string_view token =
        "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6InRlc3Qta2V5LTEifQ.eyJhdWQiOiJhcGktY2xpZW50LWlkIiwiaXNzIjoiaHR0cHM6Ly9sb2dpbi5taWNyb3NvZnRvbmxpbmUuY29tL3RlbmFudC0xL3YyLjAiLCJ0aWQiOiJ0ZW5hbnQtMSIsImV4cCI6MjAwMDAwMDAwMCwibmJmIjoxNzAwMDAwMDAwLCJzdWIiOiJzdWJqZWN0LTEiLCJvaWQiOiJvYmplY3QtMSIsImF6cCI6ImNsaWVudC0xIn0.CCEOODMQmnRL6Huhhdd68Pj5UCcg4KXjqTx01UxiLIxFf0K6Upxo7UeiSB1ajFxz7NsROaz97Q30qQZN4DMKhtfqKV0f88oP-kClw-dWZmYMIoJCszmHYD8QGePtIgjiJHakgNNa03YiutW4UZLDoi-m2H6u5I3nGELV7uun1M98mjZznuoMh-Aiqx_QWqCXXSnfcNsX-dg_vbL33ejabFg8tWJGR9J78yhmJdoT5gyxr5z1__wWR2p5LKZyylRDOinLcbcnj_bT3ONOKF-sDH5RkUiVAWp-9NYSFEEl2l72vDyoCbyyLBKKZN_PbA5V7moDkljlv8W6jrTYE9r4sw";

    entra_token_identity identity;

    const auto valid =
        entra.validate_token(
            token,
            at_seconds(1800000000),
            identity);

#ifdef _WIN32
    if (valid !=
            entra_token_validation_result::success ||
        identity.tenant_id != "tenant-1" ||
        identity.subject != "subject-1" ||
        identity.object_id != "object-1" ||
        identity.client_id != "client-1") {

        cleanup();
        return false;
    }

    std::string tampered{token};
    tampered.back() =
        tampered.back() == 'A'
            ? 'B'
            : 'A';

    if (entra.validate_token(
            tampered,
            at_seconds(1800000000),
            identity) !=
        entra_token_validation_result::invalid_signature) {

        cleanup();
        return false;
    }

    if (entra.validate_token(
            token,
            at_seconds(2000000000),
            identity) !=
        entra_token_validation_result::expired) {

        cleanup();
        return false;
    }

    if (entra.validate_token(
            token,
            at_seconds(1600000000),
            identity) !=
        entra_token_validation_result::not_yet_valid) {

        cleanup();
        return false;
    }
#else
    if (valid !=
        entra_token_validation_result::crypto_unavailable) {

        cleanup();
        return false;
    }
#endif

    entra.stop();
    cleanup();

    return true;
}

}

int main() {
    if (!run_windows_validation()) {
        std::cerr << "Microsoft Entra JWT validation failed\n";
        return 1;
    }

    return 0;
}
