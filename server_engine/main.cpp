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

int main(int argc, char* argv[]) {
    const std::filesystem::path configuration =
        argc > 1
            ? argv[1]
            : "server.json";

    cw::server::server instance;
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
