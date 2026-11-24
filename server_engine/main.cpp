/*
 * ServerEngineV4 process entry point.
 *
 * main owns process exit policy only. Diagnostics are formatted by the shared
 * Clang-style diagnostics formatter.
 */
#include "diagnostics/diagnostic_formatter.hpp"
#include "server.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    std::filesystem::path configuration = "server.json";
    bool diagnostics_mode = false, configuration_set = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--diagnostics") diagnostics_mode = true;
        else if (argument == "--help") {
            std::cout << "Usage: ServerEngineV4 [server.json] [--diagnostics]\n";
            return 0;
        } else if (argument.starts_with("--") || configuration_set) {
            std::cerr << "Invalid argument: " << argument << '\n';
            return 2;
        } else { configuration = argv[i]; configuration_set = true; }
    }

    cw::server::server instance;
    instance.enable_operation_diagnostics(diagnostics_mode);
    cw::server::diagnostic_collection diagnostics;

    const auto status =
        instance.start(
            configuration,
            diagnostics);

    diagnostics.sort_deterministic();

    if (!cw::server::succeeded(status)) {
        cw::server::format_diagnostics(
            std::cerr,
            diagnostics);

        return 1;
    }

    std::cout
        << "Server started\n"
        << "Commands: LOAD <project.json>, UNLOAD, SHUTDOWN\n";

    const int result =
        instance.run();

    std::cout
        << "Server stopped\n";

    return result;
}
