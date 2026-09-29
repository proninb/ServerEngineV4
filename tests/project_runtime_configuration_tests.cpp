#include "project/project_configuration_loader.hpp"

#include <filesystem>
#include <fstream>

using namespace cw::server;

int main() {
    const auto directory =
        std::filesystem::temp_directory_path() /
        "server_engine_v4_project_runtime_configuration_tests";

    std::error_code error;

    std::filesystem::remove_all(
        directory,
        error);

    error.clear();

    std::filesystem::create_directories(
        directory,
        error);

    if (error) {
        return 1;
    }

    const auto project_path =
        directory / "project.json";

    {
        std::ofstream output(
            project_path,
            std::ios::binary);

        if (!output) {
            return 2;
        }

        output <<
            R"({"version":"ignored","name":17,"project":{"construction":"ignored"},"preprocessor":[false,42],"ic":"runtime/IC.json"})";

        if (!output) {
            return 3;
        }
    }

    diagnostic_collection diagnostics;
    project_runtime_configuration configuration;

    const auto status =
        load_project_runtime_configuration(
            project_path,
            operation_id{1},
            diagnostics,
            configuration);

    if (!succeeded(status)) {
        return 4;
    }

    if (configuration.ic !=
        std::filesystem::path{
            "runtime/IC.json"}) {

        return 5;
    }

    std::filesystem::remove_all(
        directory,
        error);

    return 0;
}
