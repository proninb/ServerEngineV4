#if defined(CW_SOURCE_TARGET_CONTEXT_AB) || defined(CW_SOURCE_TARGET_CONTEXT_AUDIT)
#include "project/parser/source_target_context_policy.hpp"
#endif
#ifdef CW_SOURCE_LOCALITY_AUDIT
#include "../experiments/source-locality-01/capture.hpp"
#endif
#ifdef CW_SOURCE_TYPE_PREPARE_AB
#include "project/parser/source_type_prepare_policy.hpp"
#endif
#ifdef CW_SOURCE_STRING_SCRATCH_AB
#include "project/parser/source_string_scratch_policy.hpp"
#endif
#ifdef CW_RUNTIME_OBJECT_LANES_AB
#include "project/runtime/object_lane_policy.hpp"
#include <charconv>
#endif
#ifdef CW_RUNTIME_API_RANGES_AB
#include "project/shm/runtime_range_policy.hpp"
#endif
#ifdef CW_CONSTRUCTION_EARLY_RELEASE_AB
#include "project/construction/early_release_policy.hpp"
#endif
#ifdef CW_PUBLISH_NO_SHA256_AB
#include "project/file/publish_hash_policy.hpp"
#endif
#ifdef CW_STRING_INTERN_SLOT_AB
#include "project/string/intern_slot_policy.hpp"
#endif
#ifdef CW_GRAPH_LINK_INDEX_AB
#include "project/graph/link_index_policy.hpp"
#endif
#ifdef CW_GRAPH_LINK_INDEX_AUDIT
#include "../experiments/source-link-index-01/audit.hpp"
#endif
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
#include "project/graph/graph.hpp"
#include "project/semantic/identity.hpp"
#include "project/string/string_table.hpp"
#ifdef CW_GRAPH_MEMBER_INDEX_AUDIT
#include "../experiments/header-graph-index-01/audit.hpp"
#endif
#ifdef CW_GRAPH_MEMBER_INDEX_AB
#include "project/graph/member_index_policy.hpp"
#endif
#ifdef CW_HEADER_STATE_LAZY_AB
#include "project/parser/header_record_index.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#else
#include <sys/resource.h>
#endif

#ifdef CW_RUNTIME_OBJECT_LANES_AB
#include "../experiments/runtime-object-lanes-01/shm_audit.hpp"
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
    graph_resolved_profile,
};

[[nodiscard]] bool parse_mode(
    std::string_view value,
    benchmark_mode& output) noexcept {

    if (value == "publish") {
        output = benchmark_mode::publish;
        return true;
    }
    if (value == "publish-profile" || value == "publish-stage" || value == "publish-timeline") {
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

    if (value == "graph-resolved-profile") {
        output =
            benchmark_mode::
                graph_resolved_profile;
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


[[nodiscard]] int run_graph_resolved_profile(
    std::size_t member_count,
    std::size_t lookup_count) {

    using namespace cw::server;
    using clock_type =
        std::chrono::steady_clock;

    if (member_count == 0 ||
        member_count >
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)()) ||
        lookup_count == 0) {

        std::cerr
            << "Invalid graph-resolved-profile dimensions\n";

        return 2;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;

    string_id type_name;

    if (!succeeded(
            strings.intern(
                "__graph_resolved_profile_type",
                type_name))) {
        return 3;
    }

    identity_ref type_identity;

    if (!succeeded(
            identities.resolve(
                identities.root(),
                type_name,
                identity_kind::type,
                type_identity))) {
        return 3;
    }

    type_handle type;

    if (!succeeded(
            G.declare_record(
                type_identity,
                graph_record_kind::struct_type,
                type))) {
        return 3;
    }

    const auto int_type =
        G.intrinsic(
            intrinsic_type::signed_int);

    std::vector<string_id> names;
    std::vector<member_record> members;

    try {
        names.reserve(member_count);
        members.reserve(member_count);
    }
    catch (...) {
        return 3;
    }

    for (std::size_t index = 0;
         index < member_count;
         ++index) {

        const auto spelling =
            std::string{"member_"} +
            std::to_string(index);

        string_id name;

        if (!succeeded(
                strings.intern(
                    spelling,
                    name))) {
            return 3;
        }

        names.push_back(name);

        members.push_back({
            name,
            int_type,
            graph_member_access::public_access,
            {},
        });
    }

    const auto commit_started =
        clock_type::now();

    if (!succeeded(
            G.define_resolved_record(
                type,
                graph_record_kind::struct_type,
                members))) {
        return 3;
    }

    const auto commit_finished =
        clock_type::now();

    std::uint64_t indexed_probes = 0;
    std::uint64_t indexed_checksum = 0;

    const auto indexed_started =
        clock_type::now();

    for (std::size_t iteration = 0;
         iteration < lookup_count;
         ++iteration) {

        const auto index =
            static_cast<std::size_t>(
                (static_cast<std::uint64_t>(iteration) *
                 11400714819323198485ull) %
                member_count);

        std::uint32_t probes = 0;

        const auto found =
            G.find_member_profiled(
                type,
                names[index],
                probes);

        if (!found ||
            found.value() != index) {
            std::cerr
                << "Indexed member lookup mismatch\n";
            return 3;
        }

        indexed_probes += probes;
        indexed_checksum +=
            static_cast<std::uint64_t>(
                found.value()) + 1;
    }

    const auto indexed_finished =
        clock_type::now();

    const auto values =
        G.members(type);

    std::uint64_t linear_comparisons = 0;
    std::uint64_t linear_checksum = 0;

    const auto linear_started =
        clock_type::now();

    for (std::size_t iteration = 0;
         iteration < lookup_count;
         ++iteration) {

        const auto index =
            static_cast<std::size_t>(
                (static_cast<std::uint64_t>(iteration) *
                 11400714819323198485ull) %
                member_count);

        const auto target =
            names[index];

        std::size_t found =
            values.size();

        for (std::size_t member = 0;
             member < values.size();
             ++member) {

            ++linear_comparisons;

            if (values[member].name ==
                target) {
                found = member;
                break;
            }
        }

        if (found != index) {
            std::cerr
                << "Linear member lookup mismatch\n";
            return 3;
        }

        linear_checksum +=
            static_cast<std::uint64_t>(found) + 1;
    }

    const auto linear_finished =
        clock_type::now();

    if (indexed_checksum != linear_checksum) {
        std::cerr
            << "Lookup checksum mismatch\n";
        return 3;
    }

    const auto milliseconds =
        [](auto duration) noexcept {
            return std::chrono::duration<
                double,
                std::milli>{duration}.count();
        };

    std::cout
        << "mode=graph-resolved-profile"
        << ",members=" << member_count
        << ",lookups=" << lookup_count
        << ",record_commit_ms="
        << milliseconds(
            commit_finished -
            commit_started)
        << ",indexed_lookup_ms="
        << milliseconds(
            indexed_finished -
            indexed_started)
        << ",indexed_probes="
        << indexed_probes
        << ",indexed_avg_probes="
        << static_cast<double>(
               indexed_probes) /
               static_cast<double>(
                   lookup_count)
        << ",linear_lookup_ms="
        << milliseconds(
            linear_finished -
            linear_started)
        << ",linear_comparisons="
        << linear_comparisons
        << ",linear_avg_comparisons="
        << static_cast<double>(
               linear_comparisons) /
               static_cast<double>(
                   lookup_count)
        << ",checksum="
        << indexed_checksum
        << '\n';

    return 0;
}

void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  ServerEngineV4PublishBenchmark publish <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark publish-profile <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark publish-timeline <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark publish-stage <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark load    <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark load-profile <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark build   <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark rebuild <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark audit   <project.json> <expected-types>\n"
        << "  ServerEngineV4PublishBenchmark graph-resolved-profile <members> <lookups>\n";
}

}

int main(
    int argc,
    char* argv[]) {

#ifdef CW_GRAPH_MEMBER_INDEX_AB
    const auto* reference=std::getenv("CW_GRAPH_MEMBER_INDEX_REFERENCE");
    cw::server::detail::member_index_growth_reference=reference&&std::string_view{reference}=="1";
#endif

#ifdef CW_HEADER_STATE_LAZY_AB
    const auto* eager = std::getenv("CW_HEADER_STATE_EAGER");
    cw::server::detail::header_record_state_eager = eager && std::string_view{eager} == "1";
#endif

#ifdef CW_GRAPH_LINK_INDEX_AB
    const auto* link_reference=std::getenv("CW_GRAPH_LINK_INDEX_REFERENCE");
    cw::server::detail::link_index_growth_reference=link_reference&&std::string_view{link_reference}=="1";
#endif
#ifdef CW_STRING_INTERN_SLOT_AB
    const auto* slot_reference=std::getenv("CW_STRING_INTERN_SLOT_REFERENCE");
    cw::server::detail::string_intern_slot_reference=slot_reference&&std::string_view{slot_reference}=="1";
#endif
#ifdef CW_PUBLISH_NO_SHA256_AB
    const auto* hash_reference=std::getenv("CW_PUBLISH_SHA256_REFERENCE");
    cw::server::detail::publish_hash_reference=hash_reference&&std::string_view{hash_reference}=="1";
#endif
#ifdef CW_RUNTIME_API_RANGES_AB
    const auto* range_reference=std::getenv("CW_RUNTIME_RANGES_REFERENCE");
    cw::server::detail::runtime_range_reference=range_reference&&std::string_view{range_reference}=="1";
#endif
#ifdef CW_CONSTRUCTION_EARLY_RELEASE_AB
    const auto* lifetime_reference=std::getenv("CW_EARLY_RELEASE_REFERENCE");
    cw::server::detail::early_release_reference=lifetime_reference&&std::string_view{lifetime_reference}=="1";
#endif
#ifdef CW_RUNTIME_OBJECT_LANES_AB
    if (const auto* text = std::getenv("CW_RUNTIME_OBJECT_WORKERS")) {
        const std::string_view value{text};
        std::size_t limit = 0;
        const auto parsed = std::from_chars(value.data(), value.data()+value.size(), limit);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() ||
            (limit != 4 && limit != 8 && limit != 16 && limit != 24)) {
            std::cerr << "Invalid Object worker limit\n"; return 2;
        }
        cw::server::detail::object_lane_cap = limit;
    }
    if (const auto* text = std::getenv("CW_RUNTIME_OBJECT_PRETOUCH")) {
        if (std::string_view{text} != "0" && std::string_view{text} != "1") return 2;
        cw::server::detail::object_pretouch = std::string_view{text} == "1";
    }
#endif
#ifdef CW_SOURCE_STRING_SCRATCH_AB
    if (const auto* text = std::getenv("CW_SOURCE_STRING_SCRATCH")) {
        if (std::string_view{text} != "0" && std::string_view{text} != "1") return 2;
        cw::server::detail::source_string_scratch_reuse = std::string_view{text} == "1";
    }
#endif
#ifdef CW_SOURCE_TYPE_PREPARE_AB
    if (const auto* text = std::getenv("CW_SOURCE_TYPE_PREPARE")) {
        if (std::string_view{text} != "0" && std::string_view{text} != "1") return 2;
        cw::server::detail::source_type_prepare_cache = std::string_view{text} == "1";
    }
#endif
#ifdef CW_SOURCE_TARGET_CONTEXT_AB
    if (const auto* text = std::getenv("CW_SOURCE_TARGET_CONTEXT")) {
        if (std::string_view{text} != "0" && std::string_view{text} != "1") return 2;
        cw::server::detail::source_target_context = std::string_view{text} == "1";
    }
#endif
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

    if (mode ==
        benchmark_mode::
            graph_resolved_profile) {

        std::size_t member_count = 0;
        std::size_t lookup_count = 0;

        if (!parse_count(
                argv[2],
                member_count) ||
            !parse_count(
                argv[3],
                lookup_count)) {

            std::cerr
                << "Invalid graph-resolved-profile arguments\n";

            return 2;
        }

        return run_graph_resolved_profile(
            member_count,
            lookup_count);
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
    publish_telemetry.phase_only = std::string_view{argv[1]} == "publish-timeline";
    publish_telemetry.detailed_source = std::string_view{argv[1]} == "publish-profile";

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

#ifdef CW_SOURCE_LOCALITY_AUDIT
    // Also retain diagnostic prefixes when an input is unsupported. Consumers
    // must check lifecycle status and root completion before claiming coverage.
    if (const auto* directory = std::getenv("CW_SOURCE_LOCALITY_OUTPUT"))
        source_locality01::capture.dump(std::filesystem::path{directory});
#endif

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

#ifdef CW_RUNTIME_OBJECT_LANES_AB
    std::string shm_audit_sha;
    if (const auto* enabled = std::getenv("CW_RUNTIME_SHM_AUDIT"); enabled && std::string_view{enabled} == "1") {
        // After operation timer and peak WS capture; never used by the timing runner.
        shm_audit_sha = runtime_object_lanes01::sha256(project->shm().bytes().first(
            static_cast<std::size_t>(project->runtime_size())));
    }
#endif
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
        const auto& t = publish_telemetry;
        const auto accounted = t.cleanup_ns + t.context_setup_ns + t.configuration_ns +
            t.lexical_ns + t.assign_ns + t.semantic_ns + t.post_semantic_ns +
            t.persistence_ns + t.post_persistence_ns + t.resident_ns + t.transient_release_ns;
        std::cout
            << ",phase_only=" << t.phase_only
            << ",operation_ms=" << ns_to_ms(t.operation_ns)
            << ",accounted_ms=" << ns_to_ms(accounted)
            << ",unaccounted_ms=" << ns_to_ms(t.operation_ns) - ns_to_ms(accounted)
            << ",external_ms=" << elapsed - ns_to_ms(t.operation_ns)
            << ",context_setup_ms=" << ns_to_ms(t.context_setup_ns)
            << ",post_semantic_ms=" << ns_to_ms(t.post_semantic_ns)
            << ",post_persistence_ms=" << ns_to_ms(t.post_persistence_ns)
            << ",transient_release_ms=" << ns_to_ms(t.transient_release_ns)
            << ",semantic_other_ms=" << ns_to_ms(t.semantic_ns) - ns_to_ms(
                t.header_v2_physical_ns + t.header_ns + t.source_ns + t.source_finalize_ns)
            << ",cleanup_ms=" << ns_to_ms(publish_telemetry.cleanup_ns)
            << ",configuration_ms=" << ns_to_ms(publish_telemetry.configuration_ns)
            << ",lexical_ms=" << ns_to_ms(publish_telemetry.lexical_ns)
            << ",assign_ms=" << ns_to_ms(publish_telemetry.assign_ns)
            << ",semantic_ms=" << ns_to_ms(publish_telemetry.semantic_ns)
            << ",header_ms=" << ns_to_ms(publish_telemetry.header_ns)
            << ",header_v2_selected=" << publish_telemetry.header_v2_selected
            << ",header_v2_model_ms=" << ns_to_ms(publish_telemetry.header_v2_model_ns)
            << ",header_v2_model_files=" << publish_telemetry.header_v2_model_files
            << ",header_v2_model_declarations=" << publish_telemetry.header_v2_model_declarations
            << ",header_v2_model_dispatched=" << publish_telemetry.header_v2_model_dispatched
            << ",header_v2_model_roots=" << publish_telemetry.header_v2_model_roots
            << ",header_v2_model_missing=" << publish_telemetry.header_v2_model_missing
            << ",header_v2_model_decode_failed=" << publish_telemetry.header_v2_model_decode_failed
            << ",header_v2_model_directives=" << publish_telemetry.header_v2_model_directives
            << ",header_v2_model_syntax=" << publish_telemetry.header_v2_model_syntax
            << ",header_v2_model_predefines=" << publish_telemetry.header_v2_model_predefines
            << ",header_v2_model_allocations=" << publish_telemetry.header_v2_model_allocations
            << ",header_v2_model_empty=" << publish_telemetry.header_v2_model_empty
            << ",header_v2_effective_ms=" << ns_to_ms(publish_telemetry.header_v2_effective_ns)
            << ",header_v2_effective_model_ms=" << ns_to_ms(publish_telemetry.header_v2_effective_model_ns)
            << ",header_v2_effective_records=" << publish_telemetry.header_v2_effective_records
            << ",header_v2_effective_fields=" << publish_telemetry.header_v2_effective_fields
            << ",header_v2_effective_constructors=" << publish_telemetry.header_v2_effective_constructors
            << ",header_v2_effective_committed=" << publish_telemetry.header_v2_effective_committed
            << ",header_v2_roots=" << publish_telemetry.header_v2_roots
            << ",header_v2_physical_ms=" << ns_to_ms(publish_telemetry.header_v2_physical_ns)
            << ",header_v2_prepared_files=" << publish_telemetry.header_v2_prepared_files
            << ",header_v2_compact_word_count=" << publish_telemetry.header_v2_compact_word_count
            << ",header_v2_lexer_token_count=" << publish_telemetry.header_v2_lexer_token_count
            << ",header_v2_lexed_files=" << publish_telemetry.header_v2_lexed_files
            << ",header_v2_include_records=" << publish_telemetry.header_v2_include_records
            << ",header_v2_active_includes=" << publish_telemetry.header_v2_active_includes
            << ",header_v2_definitions=" << publish_telemetry.header_v2_definitions
            << ",header_v2_aliases=" << publish_telemetry.header_v2_aliases
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
            << ",header_member_lookups=" << publish_telemetry.header_member_lookups
            << ",header_member_lookup_probes=" << publish_telemetry.header_member_lookup_probes
            << ",source_member_lookups=" << publish_telemetry.source_member_lookups
            << ",source_member_lookup_probes=" << publish_telemetry.source_member_lookup_probes
            << ",source_initialization_commit_ms=" << ns_to_ms(publish_telemetry.source_initialization_commit_ns)
            << ",source_link_commit_ms=" << ns_to_ms(publish_telemetry.source_link_commit_ns)
            << ",source_provenance_ms=" << ns_to_ms(publish_telemetry.source_provenance_ns)
            << ",source_decode_ms=" << ns_to_ms(publish_telemetry.source_decode_ns)
            << ",source_intern_ms=" << ns_to_ms(publish_telemetry.source_intern_ns)
            << ",source_ready_cursor_token_count=" << publish_telemetry.source_ready_cursor_token_count
            << ",source_compact_cursor_token_count=" << publish_telemetry.source_compact_cursor_token_count
            << ",source_compact_word_count=" << publish_telemetry.source_compact_word_count
            << ",source_token_count=" << publish_telemetry.source_token_count
            << ",source_identifier_count=" << publish_telemetry.source_identifier_count
            << ",header_scope_ms=" << ns_to_ms(publish_telemetry.header_scope_ns)
            << ",header_record_ms=" << ns_to_ms(publish_telemetry.header_record_ns)
            << ",header_declared_type_ms=" << ns_to_ms(publish_telemetry.header_declared_type_ns)
            << ",header_type_specifier_ms=" << ns_to_ms(publish_telemetry.header_type_specifier_ns)
            << ",header_declarator_ms=" << ns_to_ms(publish_telemetry.header_declarator_ns)
            << ",header_scope_calls=" << publish_telemetry.header_scope_calls
            << ",header_record_count=" << publish_telemetry.header_record_count
            << ",header_data_members=" << publish_telemetry.header_data_members
            << ",header_special_member_paths=" << publish_telemetry.header_special_member_paths
            << ",header_constructors=" << publish_telemetry.header_constructors
            << ",header_access_labels=" << publish_telemetry.header_access_labels
            << ",header_advance_calls=" << publish_telemetry.header_advance_calls
            << ",header_peek_calls=" << publish_telemetry.header_peek_calls
            << ",header_at_checks=" << publish_telemetry.header_at_checks
            << ",header_input_next_calls=" << publish_telemetry.header_input_next_calls
            << ",header_buffered_advance_calls=" << publish_telemetry.header_buffered_advance_calls
            << ",header_declared_type_calls=" << publish_telemetry.header_declared_type_calls
            << ",header_type_specifier_calls=" << publish_telemetry.header_type_specifier_calls
            << ",header_declarator_calls=" << publish_telemetry.header_declarator_calls
            << ",header_type_identity_lookups=" << publish_telemetry.header_type_identity_lookups
            << ",header_type_identity_scope_steps=" << publish_telemetry.header_type_identity_scope_steps
            << ",header_find_type_calls=" << publish_telemetry.header_find_type_calls
            << ",header_read_type_calls=" << publish_telemetry.header_read_type_calls
            << ",header_dependency_adds=" << publish_telemetry.header_dependency_adds
            << ",header_derive_calls=" << publish_telemetry.header_derive_calls
            << ",header_declare_record_calls=" << publish_telemetry.header_declare_record_calls
            << ",header_define_record_calls=" << publish_telemetry.header_define_record_calls
            << ",source_root_count=" << publish_telemetry.source_root_count
            << ",source_lexer_token_count=" << publish_telemetry.source_lexer_token_count
            << ",source_root_setup_ms=" << ns_to_ms(publish_telemetry.source_root_setup_ns)
            << ",source_overlap_prepared=" << publish_telemetry.source_overlap_prepared
            << ",source_overlap_snapshot_ms=" << ns_to_ms(publish_telemetry.source_overlap_snapshot_ns)
            << ",source_overlap_prepare_ms=" << ns_to_ms(publish_telemetry.source_overlap_prepare_ns)
            << ",source_overlap_wait_ms=" << ns_to_ms(publish_telemetry.source_overlap_wait_ns)

            << ",source_replay_ms=" << ns_to_ms(publish_telemetry.source_replay_ns)
            << ",source_root_finish_ms=" << ns_to_ms(publish_telemetry.source_root_finish_ns)
            << ",source_object_statements=" << publish_telemetry.source_object_statements
            << ",source_value_assignment_statements=" << publish_telemetry.source_value_assignment_statements
            << ",source_link_statements=" << publish_telemetry.source_link_statements
            << ",source_model_direct_assignments=" << publish_telemetry.source_model_direct_assignments
            << ",source_string_assignment_statements=" << publish_telemetry.source_string_assignment_statements
            << ",source_string_assignment_elements=" << publish_telemetry.source_string_assignment_elements
            << ",source_string_assignment_ms=" << ns_to_ms(publish_telemetry.source_string_assignment_ns)
            << ",source_endpoint_count=" << publish_telemetry.source_endpoint_count
            << ",source_endpoint_steps=" << publish_telemetry.source_endpoint_steps
            << ",source_endpoint_direct_members=" << publish_telemetry.source_endpoint_direct_members
            << ",source_endpoint_path_endpoints=" << publish_telemetry.source_endpoint_path_endpoints
            << ",source_endpoint_member_steps=" << publish_telemetry.source_endpoint_member_steps
            << ",source_endpoint_array_steps=" << publish_telemetry.source_endpoint_array_steps
            << ",source_endpoint_dereference_steps=" << publish_telemetry.source_endpoint_dereference_steps
            << ",source_endpoint_base_steps=" << publish_telemetry.source_endpoint_base_steps
            << ",graph_endpoint_paths=" << publish_telemetry.graph_endpoint_paths
            << ",graph_endpoint_path_steps=" << publish_telemetry.graph_endpoint_path_steps
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
            << ",compiled_flush_overlap_ms=" << ns_to_ms(publish_telemetry.compiled_flush_overlap_ns)
            << ",acquisition_change_tokens=" << publish_telemetry.acquisition_change_tokens
            << ",persistence_flush_enabled=" << publish_telemetry.persistence_flush_enabled
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

#ifdef CW_RUNTIME_OBJECT_LANES_AB
    std::cout << ",object_lanes=" << cw::server::detail::last_object_lanes
              << ",object_phase_ms=" << static_cast<double>(cw::server::detail::last_object_phase_ns)/1000000.0
              << ",pretouch_ms=" << static_cast<double>(cw::server::detail::last_pretouch_ns)/1000000.0
              << ",pretouch_bytes=" << cw::server::detail::last_pretouch_bytes;
    if (!shm_audit_sha.empty())
        std::cout << ",shm_audit_sha256=" << shm_audit_sha << ",shm_audit_bytes=" << project->runtime_size();
#endif
#ifdef CW_SOURCE_TARGET_CONTEXT_AB
    std::cout << ",target_context=" << cw::server::detail::source_target_context;
#ifdef CW_SOURCE_TARGET_CONTEXT_AUDIT
    const auto& target_counts = cw::server::detail::target_context_counts;
    std::cout << ",target_lookup_calls=" << target_counts.lookup_calls
        << ",target_name_hits=" << target_counts.name_hits
        << ",target_endpoints=" << target_counts.target_endpoints
        << ",target_type_hits=" << target_counts.type_hits
        << ",rhs_endpoints=" << target_counts.rhs_endpoints
        << ",target_declaration_seeds=" << target_counts.declaration_seeds
        << ",target_root_resets=" << target_counts.root_resets
        << ",target_namespace_resets=" << target_counts.namespace_resets;
#endif
#endif
#ifdef CW_SOURCE_TYPE_PREPARE_AB
    std::cout << ",type_prepare_cache=" << cw::server::detail::source_type_prepare_cache;
#ifdef CW_SOURCE_TYPE_PREPARE_AUDIT
    const auto& resolver_counts = cw::server::detail::source_resolver_counts;
    std::cout << ",resolver_record_calls=" << resolver_counts.record_calls
        << ",resolver_record_named=" << resolver_counts.record_named
        << ",resolver_record_derived=" << resolver_counts.record_derived
        << ",resolver_record_found=" << resolver_counts.record_found
        << ",resolver_record_dereference=" << resolver_counts.record_dereference
        << ",resolver_unique_types=" << resolver_counts.input_types.size()
        << ",resolver_preparation_hits=" << resolver_counts.preparation_hits
        << ",resolver_preparation_misses=" << resolver_counts.preparation_misses
        << ",resolver_member_calls=" << resolver_counts.member_calls
        << ",resolver_member_hits=" << resolver_counts.member_hits
        << ",resolver_member_empty=" << resolver_counts.member_empty
        << ",resolver_member_conflicts=" << resolver_counts.member_conflicts
        << ",resolver_unique_transitions=" << resolver_counts.transition_keys.size()
        << ",resolver_inherited_lookups=" << resolver_counts.inherited_lookups
        << ",resolver_copied_base_steps=" << resolver_counts.copied_base_steps
        << ",resolver_member_steps=" << resolver_counts.member_steps
        << ",resolver_dereference_steps=" << resolver_counts.dereference_steps
        << ",resolver_index_steps=" << resolver_counts.index_steps
        << ",resolver_audit_failed=" << resolver_counts.allocation_failed;
#endif
#endif
#ifdef CW_SOURCE_STRING_SCRATCH_AB
    std::cout << ",string_scratch_reuse=" << cw::server::detail::source_string_scratch_reuse;
#ifdef CW_SOURCE_STRING_SCRATCH_AUDIT
    std::cout << ",scratch_statements=" << cw::server::detail::string_scratch_statements
        << ",scratch_bytes_growths=" << cw::server::detail::string_bytes_growths
        << ",scratch_bytes_requested=" << cw::server::detail::string_bytes_requested
        << ",scratch_paths_growths=" << cw::server::detail::string_paths_growths
        << ",scratch_paths_requested=" << cw::server::detail::string_paths_requested;
#endif
#endif
#ifdef CW_FILE_HASH_AUDIT
    std::cout << ",sha256_calls=" << cw::server::detail::file_hash_calls.load()
              << ",sha256_bytes=" << cw::server::detail::file_hash_bytes.load();
#endif
#ifdef CW_GRAPH_LINK_INDEX_AUDIT
    cw::server::link_index_audit::print(std::cout);
#endif
#ifdef CW_GRAPH_MEMBER_INDEX_AUDIT
    cw::server::member_index_audit::print(std::cout);
#endif
    std::cout << '\n';

    return 0;
}
