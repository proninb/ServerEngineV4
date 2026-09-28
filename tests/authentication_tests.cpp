#include "authentication/authentication.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
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

[[nodiscard]] bool verify_none() {
    authentication_service authentication;

    authentication_configuration configuration;
    configuration.mode = authentication_mode::none;

    if (!succeeded(
            authentication.start(
                configuration,
                std::filesystem::current_path())) ||
        !authentication.ready() ||
        authentication.mode() != authentication_mode::none) {

        return false;
    }

    authentication.stop();
    return !authentication.ready();
}

[[nodiscard]] bool verify_contract_unsupported() {
    authentication_service authentication;

    authentication_configuration configuration;
    configuration.mode =
        authentication_mode::contract;

    return authentication.start(
               configuration,
               std::filesystem::current_path()) ==
               server_status::unsupported &&
           !authentication.ready();
}

[[nodiscard]] bool verify_microsoft_entra_cache() {
    const auto directory =
        std::filesystem::current_path() /
        "authentication_entra_test";

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

    const auto cache =
        directory /
        "entra.cache";

    const auto cleanup = [&]() {
        std::error_code cleanup_error;
        std::filesystem::remove_all(
            directory,
            cleanup_error);
    };

    if (!write_text(
            cache,
            "{\n"
            "  \"version\": 1,\n"
            "  \"tenant_id\": \"tenant-1\",\n"
            "  \"audience\": \"api-client-id\",\n"
            "  \"issuer\": \"https://login.microsoftonline.com/tenant-1/v2.0\",\n"
            "  \"jwks_uri\": \"https://login.microsoftonline.com/tenant-1/discovery/v2.0/keys\",\n"
            "  \"retrieved_at\": \"2026-09-28T00:00:00Z\",\n"
            "  \"keys\": [\n"
            "    {\n"
            "      \"kid\": \"key-1\",\n"
            "      \"kty\": \"RSA\",\n"
            "      \"alg\": \"RS256\",\n"
            "      \"n\": \"AQAB\",\n"
            "      \"e\": \"AQAB\",\n"
            "      \"issuer\": \"https://login.microsoftonline.com/tenant-1/v2.0\"\n"
            "    }\n"
            "  ]\n"
            "}\n")) {

        cleanup();
        return false;
    }

    authentication_configuration configuration;
    configuration.mode =
        authentication_mode::external;

    configuration.external.emplace();
    configuration.external->tenant_id =
        "tenant-1";
    configuration.external->audience =
        "api-client-id";

    authentication_service authentication;

    const auto status =
        authentication.start(
            configuration,
            directory);

    const bool valid =
        succeeded(status) &&
        authentication.ready() &&
        authentication.mode() ==
            authentication_mode::external &&
        authentication.microsoft_entra().ready() &&
        authentication.microsoft_entra().signing_keys().size() == 1;

    authentication.stop();
    cleanup();

    return valid &&
           !authentication.ready();
}

[[nodiscard]] bool verify_missing_cache_fails_closed() {
    const auto directory =
        std::filesystem::current_path() /
        "authentication_entra_missing_test";

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

    authentication_configuration configuration;
    configuration.mode =
        authentication_mode::external;

    configuration.external.emplace();
    configuration.external->tenant_id =
        "tenant-1";
    configuration.external->audience =
        "api-client-id";

    authentication_service authentication;

    const auto status =
        authentication.start(
            configuration,
            directory);

    std::filesystem::remove_all(
        directory,
        error);

    return status ==
               server_status::authentication_start_failed &&
           !authentication.ready() &&
           !authentication.detail().empty();
}

}

int main() {
    if (!verify_none()) {
        std::cerr << "authentication mode=none lifecycle failed\n";
        return 1;
    }

    if (!verify_contract_unsupported()) {
        std::cerr << "authentication mode=contract must remain fail-closed\n";
        return 1;
    }

    if (!verify_microsoft_entra_cache()) {
        std::cerr << "Microsoft Entra cached startup failed\n";
        return 1;
    }

    if (!verify_missing_cache_fails_closed()) {
        std::cerr << "Microsoft Entra missing cache must fail closed\n";
        return 1;
    }

    return 0;
}
