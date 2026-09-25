#include "configuration/server_configuration_loader.hpp"
#include "diagnostics/diagnostic_descriptor.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace cw::server;

[[nodiscard]] std::string configuration(
    std::string_view version,
    std::string_view shm) {

    return
        "{\n"
        "  \"version\": " + std::string(version) + ",\n"
        "  \"settings\": {\n"
        "    \"abi\": {\n"
        "      \"target\": \"windows-x64\",\n"
        "      \"pack\": 8\n"
        "    },\n" +
        std::string(shm) +
        "    \"files\": {\n"
        "      \"manifest\": \"project.manifest\",\n"
        "      \"source_save\": \"source.bin\",\n"
        "      \"database\": \"database.bin\",\n"
        "      \"compiled\": \"compiled.bin\"\n"
        "    }\n"
        "  },\n"
        "  \"communication\": {\n"
        "    \"endpoints\": [\n"
        "      {\n"
        "        \"name\": \"console\",\n"
        "        \"transport\": \"console\"\n"
        "      }\n"
        "    ]\n"
        "  }\n"
        "}\n";
}

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

struct load_result final {
    server_status status =
        server_status::invalid_configuration;
    server_configuration value;
    diagnostic_collection diagnostics;
};

[[nodiscard]] load_result load(
    const std::filesystem::path& path,
    std::string_view text) {

    load_result output;

    if (!write_text(path, text)) {
        output.status =
            server_status::io_error;
        return output;
    }

    output.status =
        load_server_configuration(
            path,
            operation_id{1},
            output.diagnostics,
            output.value);

    return output;
}

[[nodiscard]] bool expect_invalid(
    const load_result& result,
    diagnostic_id id,
    std::string_view detail) {

    if (result.status !=
        server_status::invalid_configuration) {

        return false;
    }

    const auto records =
        result.diagnostics.records();

    return
        records.size() == 1 &&
        records[0].id == id &&
        records[0].detail == detail &&
        records[0].location.has_position();
}

} // namespace

int main() {
    const auto path =
        std::filesystem::current_path() /
        "server_configuration_test.json";

    std::error_code error;
    std::filesystem::remove(
        path,
        error);

    const auto cleanup = [&]() {
        std::error_code cleanup_error;
        std::filesystem::remove(
            path,
            cleanup_error);
    };

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    "    \"shm\": {\n"
                    "      \"mode\": \"fixed_direct\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project\",\n"
                    "      \"fixed_base_address\": \"0x0000010000000000\"\n"
                    "    },\n"));

        if (!succeeded(result.status) ||
            result.value.version != 6 ||
            result.value.settings.shm.mode !=
                shm_runtime_mode::fixed_direct ||
            result.value.settings.shm.name !=
                "CW.ServerEngineV4.Project" ||
            result.value.settings.shm.fixed_base_address !=
                0x0000010000000000ull) {

            cleanup();
            std::cerr << "valid fixed_direct configuration failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    "    \"shm\": {\n"
                    "      \"mode\": \"relocatable_transfer\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project\"\n"
                    "    },\n"));

        if (!succeeded(result.status) ||
            result.value.settings.shm.mode !=
                shm_runtime_mode::relocatable_transfer ||
            result.value.settings.shm.name !=
                "CW.ServerEngineV4.Project" ||
            result.value.settings.shm.fixed_base_address != 0) {

            cleanup();
            std::cerr << "valid relocatable_transfer configuration failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    ""));

        if (!expect_invalid(
                result,
                diagnostics::configuration_invalid.id,
                "settings requires abi, shm, and files")) {

            cleanup();
            std::cerr << "missing shm validation failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    "    \"shm\": {\n"
                    "      \"mode\": \"fixed_direct\",\n"
                    "      \"fixed_base_address\": \"0x0000010000000000\"\n"
                    "    },\n"));

        if (!expect_invalid(
                result,
                diagnostics::configuration_invalid.id,
                "settings.shm requires mode and name")) {

            cleanup();
            std::cerr << "missing shm name validation failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    "    \"shm\": {\n"
                    "      \"mode\": \"fixed_direct\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project\"\n"
                    "    },\n"));

        if (!expect_invalid(
                result,
                diagnostics::configuration_invalid.id,
                "settings.shm.fixed_direct requires fixed_base_address")) {

            cleanup();
            std::cerr << "fixed_direct base requirement failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    "    \"shm\": {\n"
                    "      \"mode\": \"relocatable_transfer\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project\",\n"
                    "      \"fixed_base_address\": \"0x0000010000000000\"\n"
                    "    },\n"));

        if (!expect_invalid(
                result,
                diagnostics::configuration_invalid.id,
                "settings.shm.fixed_base_address is valid only for fixed_direct")) {

            cleanup();
            std::cerr << "relocatable_transfer base rejection failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    "    \"shm\": {\n"
                    "      \"mode\": \"relocatable_transfer\",\n"
                    "      \"name\": \"bad/name\"\n"
                    "    },\n"));

        if (!expect_invalid(
                result,
                diagnostics::configuration_invalid.id,
                "settings.shm.name must contain 1-128 characters from A-Z, a-z, 0-9, '.', '_', or '-'")) {

            cleanup();
            std::cerr << "invalid shm name validation failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    "    \"shm\": {\n"
                    "      \"mode\": \"fixed_direct\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project\",\n"
                    "      \"fixed_base_address\": \"100000000\"\n"
                    "    },\n"));

        if (!expect_invalid(
                result,
                diagnostics::configuration_invalid.id,
                "settings.shm.fixed_base_address must use 0x hexadecimal notation")) {

            cleanup();
            std::cerr << "fixed base format validation failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "6",
                    "    \"shm\": {\n"
                    "      \"mode\": \"other\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project\"\n"
                    "    },\n"));

        if (!expect_invalid(
                result,
                diagnostics::configuration_invalid.id,
                "settings.shm.mode must be fixed_direct or relocatable_transfer")) {

            cleanup();
            std::cerr << "invalid shm mode validation failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "5",
                    "    \"shm\": {\n"
                    "      \"mode\": \"relocatable_transfer\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project\"\n"
                    "    },\n"));

        if (!expect_invalid(
                result,
                diagnostics::configuration_unsupported_version.id,
                "unsupported server configuration version; expected version 6")) {

            cleanup();
            std::cerr << "schema version validation failed\n";
            return 1;
        }
    }

    cleanup();
    return 0;
}
