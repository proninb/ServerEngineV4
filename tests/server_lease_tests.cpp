#include "license/server_lease_loader.hpp"

#include <chrono>
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

[[nodiscard]] std::chrono::system_clock::time_point utc(
    int year,
    unsigned month,
    unsigned day) {

    return
        std::chrono::sys_days{
            std::chrono::year{year} /
            std::chrono::month{month} /
            std::chrono::day{day}};
}

}

int main() {
    const auto path =
        std::filesystem::current_path() /
        "server_lease_test.json";

    const auto cleanup = [&]() {
        std::error_code error;
        std::filesystem::remove(
            path,
            error);
    };

    server_limits entitlement_limits;
    entitlement_limits.max_connections = 64;

    server_license license;
    license.assign(
        utc(2030, 1, 1),
        entitlement_limits);

    diagnostic_collection diagnostics;
    server_lease lease;

    if (!write_text(
            path,
            "{\n"
            "  \"version\": 1,\n"
            "  \"lease_id\": \"lease-test-1\",\n"
            "  \"not_before\": \"2026-01-01T00:00:00Z\",\n"
            "  \"expires_at\": \"2026-01-02T00:00:00Z\",\n"
            "  \"max_connections\": 32\n"
            "}\n")) {

        std::cerr << "failed to create lease fixture\n";
        return 1;
    }

    auto status =
        load_server_lease(
            path,
            utc(2026, 1, 1),
            license,
            operation_id{1},
            diagnostics,
            lease);

    if (!succeeded(status) ||
        !lease.loaded() ||
        lease.lease_id() != "lease-test-1" ||
        lease.limits().max_connections != 32 ||
        !lease.valid_at(utc(2026, 1, 1))) {

        cleanup();
        std::cerr << "valid server.lease failed\n";
        return 1;
    }

    diagnostics.clear();

    status =
        load_server_lease(
            path,
            utc(2025, 12, 31),
            license,
            operation_id{2},
            diagnostics,
            lease);

    if (status !=
            server_status::server_lease_not_active ||
        lease.loaded()) {

        cleanup();
        std::cerr << "not-active lease failed\n";
        return 1;
    }

    diagnostics.clear();

    status =
        load_server_lease(
            path,
            utc(2026, 1, 2),
            license,
            operation_id{3},
            diagnostics,
            lease);

    if (status !=
            server_status::server_lease_expired ||
        lease.loaded()) {

        cleanup();
        std::cerr << "expired lease failed\n";
        return 1;
    }

    cleanup();
    return 0;
}
