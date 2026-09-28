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
    std::string_view shm,
    std::string_view target = "windows-x64") {

    return
        "{\n"
        "  \"version\": " + std::string(version) + ",\n"
        "  \"settings\": {\n"
        "    \"abi\": {\n"
        "      \"target\": \"" + std::string(target) + "\",\n"
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
        "  \"authentication\": {\n"
        "    \"mode\": \"none\"\n"
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
                    "8",
                    "    \"shm\": {\n"
                    "      \"mode\": \"fixed_direct\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project\",\n"
                    "      \"fixed_base_address\": \"0x0000010000000000\"\n"
                    "    },\n"));

        if (!succeeded(result.status) ||
            result.value.version != 8 ||
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
                    "8",
                    "    \"shm\": {\n"
                    "      \"mode\": \"fixed_direct\",\n"
                    "      \"name\": \"CW.ServerEngineV4.Project.X86\",\n"
                    "      \"fixed_base_address\": \"0x20000000\"\n"
                    "    },\n",
                    "windows-x86"));

        if (!succeeded(result.status) ||
            result.value.settings.abi.target !=
                abi_target::windows_x86 ||
            result.value.settings.shm.fixed_base_address !=
                0x20000000ull) {

            cleanup();
            std::cerr << "valid windows-x86 target configuration failed\n";
            return 1;
        }
    }

    {
        const auto result =
            load(
                path,
                configuration(
                    "8",
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
                    "8",
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
                    "8",
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
                    "8",
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
                    "8",
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
                    "8",
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
                    "8",
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
                    "8",
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
                "unsupported server configuration version; expected version 8 or 9")) {

            cleanup();
            std::cerr << "schema version validation failed\n";
            return 1;
        }
    }


    {
        auto text =
            configuration(
                "8",
                "    \"shm\": {\n"
                "      \"mode\": \"fixed_direct\",\n"
                "      \"name\": \"CW.ServerEngineV4.Project\",\n"
                "      \"fixed_base_address\": \"0x0000010000000000\"\n"
                "    },\n");

        const auto version_position =
            text.find("\"version\": 8");

        const auto authentication_position =
            text.find("  \"authentication\":");

        if (version_position == std::string::npos ||
            authentication_position == std::string::npos) {

            cleanup();
            std::cerr << "version 9 Server identity fixture construction failed\n";
            return 1;
        }

        text.replace(
            version_position,
            std::string("\"version\": 8").size(),
            "\"version\": 9");

        text.insert(
            authentication_position,
            "  \"server_identity\": {\n"
            "    \"mode\": \"microsoft_entra\",\n"
            "    \"microsoft_entra\": {\n"
            "      \"tenant_id\": \"tenant.example\",\n"
            "      \"client_id\": \"11111111-2222-3333-4444-555555555555\",\n"
            "      \"scope\": \"api://license-service/.default\",\n"
            "      \"certificate_thumbprint\": \"00112233445566778899AABBCCDDEEFF00112233\",\n"
            "      \"certificate_store\": \"current_user\"\n"
            "    }\n"
            "  },\n");

        const auto result =
            load(
                path,
                text);

        if (!succeeded(result.status) ||
            result.value.version != 9 ||
            result.value.server_identity.mode !=
                server_identity_mode::microsoft_entra ||
            !result.value.server_identity.microsoft_entra ||
            result.value.server_identity.microsoft_entra->scope !=
                "api://license-service/.default") {

            cleanup();
            std::cerr << "version 9 Microsoft Entra Server identity configuration failed\n";
            return 1;
        }
    }

    cleanup();
    return 0;
}
