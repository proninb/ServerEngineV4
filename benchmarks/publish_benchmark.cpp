/*
 * End-to-end Project PUBLISH/REBUILD/LOAD benchmark and explicit artifact audit.
 *
 * This executable bypasses Server communication and measures exactly one
 * lifecycle function per process. The audit mode adds full CRC/semantic
 * verification after LOAD. Each mode has independent timing and process peak
 * working-set measurements.
 */
#include "diagnostics/diagnostic_formatter.hpp"
#include "project/persistence/project_artifact.hpp"
#include "project/project_load.hpp"
#include "project/project_publish.hpp"
#include "project/project_rebuild.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {

enum class benchmark_mode {
    publish,
    load,
    rebuild,
    audit,
};

[[nodiscard]] bool parse_mode(
    std::string_view value,
    benchmark_mode& output) noexcept {

    if (value == "publish") {
        output = benchmark_mode::publish;
        return true;
    }

    if (value == "load") {
        output = benchmark_mode::load;
        return true;
    }

    if (value == "rebuild") {
        output = benchmark_mode::rebuild;
        return true;
    }
    if (value == "audit") {
        output = benchmark_mode::audit;
        return true;
    }

    return false;
}

[[nodiscard]] bool parse_count(
    const char* value,
    std::size_t& output) noexcept {

    if (value == nullptr ||
        *value == '\0') {

        return false;
    }

    char* end = nullptr;

    const auto parsed =
        std::strtoull(
            value,
            &end,
            10);

    if (end == value ||
        *end != '\0' ||
        parsed >
            static_cast<unsigned long long>(
                (std::numeric_limits<
                    std::size_t>::max)())) {

        return false;
    }

    output =
        static_cast<std::size_t>(
            parsed);

    return true;
}

[[nodiscard]] cw::server::server_settings_configuration
make_settings() {

    cw::server::server_settings_configuration settings;

#if defined(_WIN32)
    settings.abi.target =
        cw::server::abi_target::windows_x64;
#else
    settings.abi.target =
        cw::server::abi_target::posix_x64;
#endif

    settings.abi.pack = 8;

    settings.shm.mode =
        cw::server::shm_runtime_mode::fixed_direct;

    settings.shm.name =
        "CW.ServerEngineV4.PublishBenchmark";

    settings.shm.fixed_base_address =
        0x0000010000000000ull;

    settings.files.manifest =
        "project.manifest";

    settings.files.source_save =
        "source.bin";

    settings.files.database =
        "database.bin";

    settings.files.compiled =
        "compiled.bin";

    return settings;
}

[[nodiscard]] std::uint64_t
peak_working_set_bytes() noexcept {

#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};

    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            &counters,
            sizeof(counters))) {

        return 0;
    }

    return static_cast<std::uint64_t>(
        counters.PeakWorkingSetSize);
#else
    rusage usage{};

    if (getrusage(
            RUSAGE_SELF,
            &usage) != 0) {

        return 0;
    }

#if defined(__APPLE__)
    return static_cast<std::uint64_t>(
        usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(
        usage.ru_maxrss) * 1024ull;
#endif
#endif
}

[[nodiscard]] std::uint64_t
path_file_size(
    const std::filesystem::path& path) noexcept {

    std::error_code error;

    const auto value =
        std::filesystem::file_size(
            path,
            error);

    return error
        ? 0
        : static_cast<std::uint64_t>(
            value);
}

[[nodiscard]] bool path_exists(
    const std::filesystem::path& path) noexcept {

    std::error_code error;

    const bool result =
        std::filesystem::exists(
            path,
            error);

    return !error && result;
}

void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  ServerEngineV4PublishBenchmark publish <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark load    <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark rebuild <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark audit   <project.json> <expected-types>\n";
}

}

int main(
    int argc,
    char* argv[]) {

    if (argc != 4) {
        print_usage();
        return 2;
    }

    benchmark_mode mode;

    if (!parse_mode(
            argv[1],
            mode)) {

        print_usage();
        return 2;
    }

    std::size_t expected_types = 0;

    if (!parse_count(
            argv[3],
            expected_types)) {

        std::cerr
            << "Invalid expected type count\n";

        return 2;
    }

    std::error_code path_error;

    const auto project_path =
        std::filesystem::absolute(
            std::filesystem::path{
                argv[2]},
            path_error)
            .lexically_normal();

    if (path_error) {
        std::cerr
            << "Cannot resolve Project path\n";

        return 2;
    }

    const auto settings =
        make_settings();

    cw::server::project_artifact_layout
        artifacts;

    if (cw::server::make_project_artifact_layout(
            project_path,
            settings.files,
            artifacts) !=
        cw::server::
            project_artifact_layout_result::
                success) {

        std::cerr
            << "Cannot construct artifact layout\n";

        return 2;
    }

    cw::server::diagnostic_collection
        diagnostics;

    std::unique_ptr<cw::server::project>
        project;

    const auto started =
        std::chrono::steady_clock::now();

    cw::server::server_status status;

    if (mode ==
        benchmark_mode::publish) {

        status =
            cw::server::publish_project(
                project_path,
                settings,
                cw::server::operation_id{1},
                diagnostics,
                project);
    }
    else if (mode == benchmark_mode::rebuild) {
        status = cw::server::rebuild_project(
            project_path, settings, cw::server::operation_id{1}, diagnostics, project);
    }
    else {
        status =
            cw::server::load_project(
                project_path,
                settings,
                cw::server::operation_id{1},
                diagnostics,
                project);
    }

    if (mode == benchmark_mode::audit && cw::server::succeeded(status) && project) {
        if (project->compiled().verify_contents() !=
            cw::server::compiled_project_image_result::success) {
            std::cerr << "Compiled artifact audit failed\n";
            return 3;
        }
    }

    const auto finished =
        std::chrono::steady_clock::now();

    diagnostics.sort_deterministic();

    if (!cw::server::succeeded(status) ||
        project == nullptr) {

        cw::server::format_diagnostics(
            std::cerr,
            diagnostics);

        std::cerr
            << "Lifecycle operation failed: "
            << static_cast<int>(status)
            << '\n';

        return 1;
    }

    const auto& compiled =
        project->compiled();

    if (compiled.type_count() !=
        expected_types) {

        std::cerr
            << "Unexpected type count: "
            << compiled.type_count()
            << " != "
            << expected_types
            << '\n';

        return 3;
    }

    if (!path_exists(
            artifacts.compiled)) {

        std::cerr
            << "compiled.bin is missing\n";

        return 3;
    }

    if (mode ==
            benchmark_mode::publish &&
        (path_exists(artifacts.manifest) ||
         path_exists(artifacts.source_save) ||
         path_exists(artifacts.database))) {

        std::cerr
            << "PUBLISH contract violation: "
               "BUILD-acceleration artifact exists\n";

        return 3;
    }

    const auto elapsed =
        std::chrono::duration<double, std::milli>{
            finished - started}
            .count();

    const auto peak =
        peak_working_set_bytes();

    std::cout
        << "mode="
        << argv[1]
        << ",total_ms="
        << elapsed
        << ",peak_ws_bytes="
        << peak
        << ",compiled_bytes="
        << path_file_size(
            artifacts.compiled)
        << ",types="
        << compiled.type_count()
        << ",members="
        << compiled.member_count()
        << ",objects="
        << compiled.object_count()
        << ",links="
        << compiled.link_count()
        << ",strings="
        << compiled.string_count()
        << ",identities="
        << compiled.identity_count()
        << '\n';

    return 0;
}
