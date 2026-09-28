#include "license/server_license_loader.hpp"
#include "diagnostics/diagnostic_descriptor.hpp"

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
        "server_license_test.json";

    const auto cleanup = [&]() {
        std::error_code error;
        std::filesystem::remove(
            path,
            error);
    };

    diagnostic_collection diagnostics;
    server_license license;

    if (!write_text(
            path,
            "{\n"
            "  \"version\": 1,\n"
            "  \"expires_at\": \"2099-12-31T23:59:59Z\",\n"
            "  \"max_connections\": 32\n"
            "}\n")) {

        std::cerr << "failed to create license fixture\n";
        return 1;
    }

    auto status =
        load_server_license(
            path,
            utc(2026, 1, 1),
            operation_id{1},
            diagnostics,
            license);

    if (!succeeded(status) ||
        !license.loaded() ||
        license.limits().max_connections != 32) {

        cleanup();
        std::cerr << "valid server.license failed\n";
        return 1;
    }

    diagnostics.clear();

    if (!write_text(
            path,
            "{\n"
            "  \"version\": 1,\n"
            "  \"expires_at\": \"2020-01-01T00:00:00Z\",\n"
            "  \"max_connections\": 32\n"
            "}\n")) {

        cleanup();
        return 1;
    }

    status =
        load_server_license(
            path,
            utc(2026, 1, 1),
            operation_id{2},
            diagnostics,
            license);

    if (status != server_status::license_expired ||
        license.loaded() ||
        diagnostics.records().size() != 1 ||
        diagnostics.records()[0].id !=
            diagnostics::license_expired.id) {

        cleanup();
        std::cerr << "expired server.license validation failed\n";
        return 1;
    }

    diagnostics.clear();

    if (!write_text(
            path,
            "{\n"
            "  \"version\": 1,\n"
            "  \"expires_at\": \"2099-12-31T23:59:59Z\",\n"
            "  \"max_connections\": 0\n"
            "}\n")) {

        cleanup();
        return 1;
    }

    status =
        load_server_license(
            path,
            utc(2026, 1, 1),
            operation_id{3},
            diagnostics,
            license);

    if (status != server_status::license_invalid ||
        license.loaded()) {

        cleanup();
        std::cerr << "zero max_connections validation failed\n";
        return 1;
    }

    diagnostics.clear();
    cleanup();

    status =
        load_server_license(
            path,
            utc(2026, 1, 1),
            operation_id{4},
            diagnostics,
            license);

    if (status != server_status::license_read_failed ||
        license.loaded()) {

        std::cerr << "missing server.license validation failed\n";
        return 1;
    }

    cleanup();
    return 0;
}
