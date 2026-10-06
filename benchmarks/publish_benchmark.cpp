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
#include "project/project_build.hpp"
#include "project/project_rebuild.hpp"
#include "project/runtime/project_runtime.hpp"

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
    publish_profile,
    load,
    load_profile,
    build,
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
    if (value == "publish-profile" || value == "publish-stage") {
        output = benchmark_mode::publish_profile;
        return true;
    }

    if (value == "load") {
        output = benchmark_mode::load;
        return true;
    }
    if (value == "load-profile") {
        output = benchmark_mode::load_profile;
        return true;
    }

    if (value == "build") {
        output = benchmark_mode::build;
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

[[nodiscard]] double ns_to_ms(
    std::uint64_t value) noexcept {

    return static_cast<double>(
        value) /
        1'000'000.0;
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
        << "  ServerEngineV4PublishBenchmark publish-profile <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark publish-stage <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark load    <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark load-profile <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark build   <project.json> <expected-types>\n"
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
    cw::server::project_build_telemetry
        build_telemetry;

    cw::server::project_runtime_telemetry
        runtime_telemetry;
    cw::server::full_construction_telemetry
        publish_telemetry;
    publish_telemetry.detailed_source = std::string_view{argv[1]} != "publish-stage";

    if (mode == benchmark_mode::publish ||
        mode == benchmark_mode::publish_profile) {

        status =
            cw::server::publish_project(
                project_path,
                settings,
                cw::server::operation_id{1},
                diagnostics,
                project,
                mode == benchmark_mode::publish_profile
                    ? &publish_telemetry
                    : nullptr);
    }
    else if (mode == benchmark_mode::build) {
        status =
            cw::server::build_project(
                project_path,
                settings,
                cw::server::operation_id{1},
                diagnostics,
                project,
                &build_telemetry);
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
                project,
                mode == benchmark_mode::load ? nullptr : &runtime_telemetry);
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

    if (!path_exists(artifacts.compiled) ||
        !path_exists(artifacts.runtime)) {
        std::cerr << "compiled.bin or runtime.bin is missing\n";
        return 3;
    }

    if ((mode == benchmark_mode::publish ||
         mode == benchmark_mode::publish_profile) &&
        (path_exists(artifacts.manifest) ||
         path_exists(artifacts.source_save) ||
         path_exists(artifacts.database))) {

        std::cerr
            << "PUBLISH contract violation: "
               "BUILD-acceleration artifact exists\n";

        return 3;
    }

    if ((mode ==
             benchmark_mode::build ||
         mode ==
             benchmark_mode::rebuild) &&
        (!path_exists(artifacts.manifest) ||
         !path_exists(artifacts.source_save) ||
         !path_exists(artifacts.database))) {

        std::cerr
            << "BUILD lineage artifact is missing\n";

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
        << ",runtime_bytes="
        << path_file_size(
            artifacts.runtime)
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
        << compiled.identity_count();

    if (mode == benchmark_mode::publish_profile) {
        std::cout
            << ",cleanup_ms=" << ns_to_ms(publish_telemetry.cleanup_ns)
            << ",configuration_ms=" << ns_to_ms(publish_telemetry.configuration_ns)
            << ",lexical_ms=" << ns_to_ms(publish_telemetry.lexical_ns)
            << ",assign_ms=" << ns_to_ms(publish_telemetry.assign_ns)
            << ",semantic_ms=" << ns_to_ms(publish_telemetry.semantic_ns)
            << ",header_ms=" << ns_to_ms(publish_telemetry.header_ns)
            << ",include_ms=" << ns_to_ms(publish_telemetry.include_ns)
            << ",include_count=" << publish_telemetry.include_count
            << ",include_lexical_ms=" << ns_to_ms(publish_telemetry.include_lexical_ns)
            << ",prepared_include_count=" << publish_telemetry.prepared_include_count
            << ",source_ms=" << ns_to_ms(publish_telemetry.source_ns)
            << ",source_object_ms=" << ns_to_ms(publish_telemetry.source_object_ns)
            << ",source_assignment_ms=" << ns_to_ms(publish_telemetry.source_assignment_ns)
            << ",source_link_ms=" << ns_to_ms(publish_telemetry.source_link_ns)
            << ",source_endpoint_ms=" << ns_to_ms(publish_telemetry.source_endpoint_ns)
            << ",source_member_ms=" << ns_to_ms(publish_telemetry.source_member_ns)
            << ",source_member_lookups=" << publish_telemetry.source_member_lookups
            << ",source_initialization_commit_ms=" << ns_to_ms(publish_telemetry.source_initialization_commit_ns)
            << ",source_link_commit_ms=" << ns_to_ms(publish_telemetry.source_link_commit_ns)
            << ",source_provenance_ms=" << ns_to_ms(publish_telemetry.source_provenance_ns)
            << ",source_decode_ms=" << ns_to_ms(publish_telemetry.source_decode_ns)
            << ",source_intern_ms=" << ns_to_ms(publish_telemetry.source_intern_ns)
            << ",source_token_count=" << publish_telemetry.source_token_count
            << ",source_identifier_count=" << publish_telemetry.source_identifier_count


            << ",source_finalize_ms=" << ns_to_ms(publish_telemetry.source_finalize_ns)
            << ",persistence_ms=" << ns_to_ms(publish_telemetry.persistence_ns)
            << ",compiled_prepare_ms=" << ns_to_ms(publish_telemetry.compiled_prepare_ns)
            << ",compiled_map_ms=" << ns_to_ms(publish_telemetry.compiled_map_ns)
            << ",compiled_encode_ms=" << ns_to_ms(publish_telemetry.compiled_encode_ns)
            << ",compiled_validate_ms=" << ns_to_ms(publish_telemetry.compiled_validate_ns)
            << ",compiled_layout_ms=" << ns_to_ms(publish_telemetry.compiled_layout_ns)
            << ",compiled_type_ms=" << ns_to_ms(publish_telemetry.compiled_type_ns)
            << ",compiled_plan_ms=" << ns_to_ms(publish_telemetry.compiled_plan_ns)
            << ",compiled_runtime_prepare_ms=" << ns_to_ms(publish_telemetry.compiled_runtime_prepare_ns)
            << ",runtime_plan_links_ms=" << ns_to_ms(publish_telemetry.runtime_plan_links_ns)
            << ",runtime_plan_initializations_ms=" << ns_to_ms(publish_telemetry.runtime_plan_initializations_ns)
            << ",runtime_plan_link_slots=" << publish_telemetry.runtime_plan_link_slots
            << ",runtime_plan_live_links=" << publish_telemetry.runtime_plan_live_links
            << ",runtime_plan_initializations=" << publish_telemetry.runtime_plan_initializations
            << ",runtime_plan_link_dereferences=" << publish_telemetry.runtime_plan_link_dereferences
            << ",runtime_plan_initialization_dereferences=" << publish_telemetry.runtime_plan_initialization_dereferences
            << ",runtime_plan_total_dereferences=" << publish_telemetry.runtime_plan_total_dereferences
            << ",runtime_plan_parallel_lanes=" << publish_telemetry.runtime_plan_parallel_lanes
            << ",compiled_remap_ms=" << ns_to_ms(publish_telemetry.compiled_remap_ns)
            << ",compiled_physical_ms=" << ns_to_ms(publish_telemetry.compiled_physical_ns)
            << ",compiled_abi_encode_ms=" << ns_to_ms(publish_telemetry.compiled_abi_encode_ns)
            << ",compiled_type_encode_ms=" << ns_to_ms(publish_telemetry.compiled_type_encode_ns)
            << ",compiled_runtime_encode_ms=" << ns_to_ms(publish_telemetry.compiled_runtime_encode_ns)
            << ",compiled_flush_ms=" << ns_to_ms(publish_telemetry.compiled_flush_ns)
            << ",resident_ms=" << ns_to_ms(publish_telemetry.resident_ns);
    }

    if (mode == benchmark_mode::load_profile ||
        mode == benchmark_mode::audit) {

        std::cout
            << ",runtime_v2="
            << static_cast<unsigned>(
                runtime_telemetry.runtime_v2)
            << ",runtime_v2_persisted_layout="
            << static_cast<unsigned>(
                runtime_telemetry.runtime_v2_persisted_layout)
            << ",runtime_v2_persisted_execution="
            << static_cast<unsigned>(
                runtime_telemetry.runtime_v2_persisted_execution)
            << ",runtime_v2_prepare_ms="
            << ns_to_ms(
                runtime_telemetry.runtime_v2_prepare_ns)
            << ",runtime_v2_metadata_bytes="
            << runtime_telemetry.
                runtime_v2_metadata_bytes
            << ",runtime_v2_relative_reference_records="
            << runtime_telemetry.runtime_v2_prepare.
                relative_references
            << ",runtime_v2_object_api_applications="
            << runtime_telemetry.
                runtime_v2_objects.api_applications
            << ",runtime_v2_program_duplication_profile_valid="
            << static_cast<unsigned>(
                runtime_telemetry.runtime_v2_prepare.
                    program_duplication_profile_valid)
            << ",runtime_v2_program_profile_object_root_apis="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_root_apis
            << ",runtime_v2_program_profile_object_reachable_apis="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_reachable_apis
            << ",runtime_v2_program_profile_object_api_applications="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_api_applications
            << ",runtime_v2_program_profile_total_potential_bytes="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.
                    total_potential_bytes_removed
            << ",runtime_v2_program_absolute_reference_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.absolute_references.
                    physical_records
            << ",runtime_v2_program_absolute_reference_unique="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.absolute_references.
                    unique_programs
            << ",runtime_v2_program_absolute_reference_duplicate_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.absolute_references.
                    duplicate_api_copies
            << ",runtime_v2_program_absolute_reference_duplicate_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.absolute_references.
                    duplicate_physical_records
            << ",runtime_v2_program_absolute_reference_potential_bytes="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.absolute_references.
                    potential_bytes_removed
            << ",runtime_v2_program_absolute_reference_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.absolute_references.
                    object_execution_operations
            << ",runtime_v2_program_absolute_reference_duplicated_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.absolute_references.
                    duplicated_object_execution_operations
            << ",runtime_v2_program_absolute_reference_max_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.absolute_references.
                    max_api_copies
            << ",runtime_v2_program_object_reference_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_references.
                    physical_records
            << ",runtime_v2_program_object_reference_unique="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_references.
                    unique_programs
            << ",runtime_v2_program_object_reference_duplicate_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_references.
                    duplicate_api_copies
            << ",runtime_v2_program_object_reference_duplicate_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_references.
                    duplicate_physical_records
            << ",runtime_v2_program_object_reference_potential_bytes="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_references.
                    potential_bytes_removed
            << ",runtime_v2_program_object_reference_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_references.
                    object_execution_operations
            << ",runtime_v2_program_object_reference_duplicated_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_references.
                    duplicated_object_execution_operations
            << ",runtime_v2_program_object_reference_max_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.object_references.
                    max_api_copies
            << ",runtime_v2_program_store_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.stores.
                    physical_records
            << ",runtime_v2_program_store_unique="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.stores.
                    unique_programs
            << ",runtime_v2_program_store_duplicate_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.stores.
                    duplicate_api_copies
            << ",runtime_v2_program_store_duplicate_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.stores.
                    duplicate_physical_records
            << ",runtime_v2_program_store_potential_bytes="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.stores.
                    potential_bytes_removed
            << ",runtime_v2_program_store_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.stores.
                    object_execution_operations
            << ",runtime_v2_program_store_duplicated_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.stores.
                    duplicated_object_execution_operations
            << ",runtime_v2_program_store_max_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.stores.
                    max_api_copies
            << ",runtime_v2_program_post_store_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.post_stores.
                    physical_records
            << ",runtime_v2_program_post_store_unique="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.post_stores.
                    unique_programs
            << ",runtime_v2_program_post_store_duplicate_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.post_stores.
                    duplicate_api_copies
            << ",runtime_v2_program_post_store_duplicate_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.post_stores.
                    duplicate_physical_records
            << ",runtime_v2_program_post_store_potential_bytes="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.post_stores.
                    potential_bytes_removed
            << ",runtime_v2_program_post_store_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.post_stores.
                    object_execution_operations
            << ",runtime_v2_program_post_store_duplicated_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.post_stores.
                    duplicated_object_execution_operations
            << ",runtime_v2_program_post_store_max_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.post_stores.
                    max_api_copies
            << ",runtime_v2_program_child_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.children.
                    physical_records
            << ",runtime_v2_program_child_unique="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.children.
                    unique_programs
            << ",runtime_v2_program_child_duplicate_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.children.
                    duplicate_api_copies
            << ",runtime_v2_program_child_duplicate_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.children.
                    duplicate_physical_records
            << ",runtime_v2_program_child_potential_bytes="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.children.
                    potential_bytes_removed
            << ",runtime_v2_program_child_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.children.
                    object_execution_operations
            << ",runtime_v2_program_child_duplicated_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.children.
                    duplicated_object_execution_operations
            << ",runtime_v2_program_child_max_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.children.
                    max_api_copies
            << ",runtime_v2_program_repeat_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.repeats.
                    physical_records
            << ",runtime_v2_program_repeat_unique="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.repeats.
                    unique_programs
            << ",runtime_v2_program_repeat_duplicate_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.repeats.
                    duplicate_api_copies
            << ",runtime_v2_program_repeat_duplicate_records="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.repeats.
                    duplicate_physical_records
            << ",runtime_v2_program_repeat_potential_bytes="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.repeats.
                    potential_bytes_removed
            << ",runtime_v2_program_repeat_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.repeats.
                    object_execution_operations
            << ",runtime_v2_program_repeat_duplicated_object_ops="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.repeats.
                    duplicated_object_execution_operations
            << ",runtime_v2_program_repeat_max_api_copies="
            << runtime_telemetry.runtime_v2_prepare.
                program_duplication_profile.repeats.
                    max_api_copies
            << ",runtime_v2_constructor_defaults="
            << runtime_telemetry.
                runtime_v2_constructor_defaults
            << ",runtime_v2_link_count="
            << runtime_telemetry.
                runtime_v2_link_count
            << ",runtime_v2_link_dereferences="
            << runtime_telemetry.
                runtime_v2_link_dereferences
            << ",runtime_v2_link_targets_marked="
            << runtime_telemetry.
                runtime_v2_links.targets_marked
            << ",runtime_v2_links_resolved="
            << runtime_telemetry.
                runtime_v2_links.links_resolved
            << ",runtime_v2_link_recursive_resolutions="
            << runtime_telemetry.
                runtime_v2_links.recursive_resolutions
            << ",runtime_v2_link_dereference_reads="
            << runtime_telemetry.
                runtime_v2_links.dereference_reads
            << ",runtime_v2_pending_link_preserves="
            << runtime_telemetry.
                runtime_v2_objects.pending_link_preserves
            << ",runtime_v2_initialization_count="
            << runtime_telemetry.
                runtime_v2_initialization_count
            << ",runtime_v2_initialization_dereferences="
            << runtime_telemetry.
                runtime_v2_initialization_dereferences
            << ",runtime_v2_initialization_writes="
            << runtime_telemetry.
                runtime_v2_initializations.writes
            << ",runtime_v2_initialization_dereference_reads="
            << runtime_telemetry.
                runtime_v2_initializations.dereference_reads
            << ",shm_layout_ms="
            << ns_to_ms(
                runtime_telemetry.layout_ns)
            << ",shm_create_ms="
            << ns_to_ms(
                runtime_telemetry.shm_create_ns)
            << ",shm_pretouch_ms="
            << ns_to_ms(
                runtime_telemetry.shm_pretouch_ns)
            << ",shm_pretouch_lanes="
            << runtime_telemetry.shm_pretouch_lanes
            << ",runtime_v2_object_lanes="
            << runtime_telemetry.runtime_v2_object_lanes
            << ",runtime_v2_link_mark_lanes="
            << runtime_telemetry.runtime_v2_link_mark_lanes
            << ",materialization_ms="
            << ns_to_ms(
                runtime_telemetry.materialization_ns)
            << ",runtime_bytes="
            << runtime_telemetry.runtime_bytes
            << ",shm_bytes="
            << runtime_telemetry.shm_bytes
            << ",canonical_ms="
            << ns_to_ms(
                runtime_telemetry.phases.canonical_ns)
            << ",links_mark_ms="
            << ns_to_ms(
                runtime_telemetry.phases.links_mark_ns)
            << ",objects_ms="
            << ns_to_ms(
                runtime_telemetry.phases.objects_ns)
            << ",links_materialize_ms="
            << ns_to_ms(
                runtime_telemetry.phases.links_materialize_ns)
            << ",initializations_ms="
            << ns_to_ms(
                runtime_telemetry.phases.initializations_ns);
    }


    if (mode == benchmark_mode::build) {
        const auto ns_ms =
            [](std::uint64_t value) noexcept {
                return static_cast<double>(
                    value) /
                    1'000'000.0;
            };

        std::cout
            << ",build_total_ms="
            << ns_ms(build_telemetry.total_ns)
            << ",sparse_ms="
            << ns_ms(build_telemetry.sparse_reconstruction_ns)
            << ",dense_projection_ms="
            << ns_ms(build_telemetry.dense_projection_ns)
            << ",artifact_materialization_ms="
            << ns_ms(build_telemetry.artifact_materialization_ns)
            << ",runtime_publication_ms="
            << ns_ms(build_telemetry.runtime_publication_ns)
            << ",promotion_ms="
            << ns_ms(build_telemetry.promotion_ns)
            << ",candidate_files="
            << build_telemetry.candidate_files
            << ",changed_files="
            << build_telemetry.changed_files
            << ",affected_files="
            << build_telemetry.affected_files
            << ",affected_roots="
            << build_telemetry.affected_roots
            << ",invalidated_roots="
            << build_telemetry.invalidated_roots
            << ",replay_roots="
            << build_telemetry.replay_roots
            << ",retire_types="
            << build_telemetry.retire_types
            << ",clear_type_definitions="
            << build_telemetry.clear_type_definitions
            << ",retire_objects="
            << build_telemetry.retire_objects
            << ",retire_links="
            << build_telemetry.retire_links
            << ",graph_type_patches="
            << build_telemetry.graph_type_patches
            << ",graph_appended_types="
            << build_telemetry.graph_appended_types
            << ",graph_object_patches="
            << build_telemetry.graph_object_patches
            << ",graph_appended_objects="
            << build_telemetry.graph_appended_objects
            << ",graph_link_patches="
            << build_telemetry.graph_link_patches
            << ",graph_appended_links="
            << build_telemetry.graph_appended_links
            << ",rebuild_fallback="
            << (build_telemetry.rebuild_fallback ? 1 : 0);
    }

    std::cout << '\n';

    return 0;
}
