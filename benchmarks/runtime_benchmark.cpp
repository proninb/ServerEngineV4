/*
 * FIXED_DIRECT Runtime scale benchmark.
 *
 * Fixture construction and compiled.bin encoding are measured separately from
 * the Runtime pipeline. The timed Runtime stages are:
 *
 *   compiled_project_view -> runtime_layout -> fixed SHM -> materialization
 *
 * Scenarios:
 *   objects N : one T { int out; int& in = out; } and N objects
 *   links N   : the same N objects with object i.in -> object i-1.out
 *   chain N   : one object whose record contains N reference members chained
 *               to one final int member
 */
#include "fixed_shared_memory.hpp"
#include "project/assign/assign_table.hpp"
#include "project/file/file_context.hpp"
#include "project/graph/graph.hpp"
#include "project/persistence/compiled_project.hpp"
#include "project/runtime/fixed_direct_materializer.hpp"
#include "project/runtime/runtime_layout.hpp"
#include "project/semantic/identity.hpp"
#include "project/source/source_map.hpp"
#include "project/string/string_table.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {

using namespace cw::server;

enum class scenario_kind : std::uint8_t {
    objects,
    links,
    chain,
};

struct fixture_metadata final {
    type_handle type{};
    member_index output_member{};
    member_index input_member{};
    member_index first_reference{};
    object_handle first_object{};
    object_handle previous_object{};
    object_handle last_object{};
};

struct fixture_image final {
    std::vector<std::byte> bytes;
    fixture_metadata metadata;
};

[[nodiscard]] bool parse_scenario(
    std::string_view value,
    scenario_kind& output) noexcept {

    if (value == "objects") {
        output = scenario_kind::objects;
        return true;
    }

    if (value == "links") {
        output = scenario_kind::links;
        return true;
    }

    if (value == "chain") {
        output = scenario_kind::chain;
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
        parsed == 0 ||
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

[[nodiscard]] server_abi_configuration
native_abi() noexcept {

    server_abi_configuration abi;

#if defined(_WIN32)
    abi.target =
        abi_target::windows_x64;
#else
    abi.target =
        abi_target::posix_x64;
#endif

    abi.pack = 8;
    return abi;
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

[[nodiscard]] std::uint64_t process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(
        GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(
        getpid());
#endif
}

[[nodiscard]] bool mapping_size_for(
    std::uint64_t logical,
    std::size_t& output) noexcept {

    output = 0;

    const auto page =
        fixed_shared_memory::size_alignment();

    if (page == 0) {
        return false;
    }

    std::uint64_t value =
        logical == 0
        ? static_cast<std::uint64_t>(page)
        : logical;

    const auto remainder =
        value %
        static_cast<std::uint64_t>(page);

    if (remainder != 0) {
        const auto padding =
            static_cast<std::uint64_t>(page) -
            remainder;

        if (value >
            (std::numeric_limits<std::uint64_t>::max)() -
                padding) {

            return false;
        }

        value += padding;
    }

    if (value >
        static_cast<std::uint64_t>(
            (std::numeric_limits<std::size_t>::max)())) {

        return false;
    }

    output =
        static_cast<std::size_t>(
            value);

    return true;
}

[[nodiscard]] bool indexed_string(
    string_table& strings,
    std::string_view prefix,
    std::size_t index,
    string_id& output) noexcept {

    char buffer[64]{};

    if (prefix.size() >=
        sizeof(buffer) - 24) {

        return false;
    }

    std::memcpy(
        buffer,
        prefix.data(),
        prefix.size());

    auto* first =
        buffer +
        prefix.size();

    auto* last =
        buffer +
        sizeof(buffer);

    const auto converted =
        std::to_chars(
            first,
            last,
            index);

    if (converted.ec !=
        std::errc{}) {

        return false;
    }

    return succeeded(
        strings.intern(
            std::string_view{
                buffer,
                static_cast<std::size_t>(
                    converted.ptr -
                    buffer)},
            output));
}

[[nodiscard]] bool resolve_named_identity(
    string_table& strings,
    identity_space& identities,
    std::string_view name,
    identity_kind kind,
    identity_ref& output) noexcept {

    string_id string;

    return succeeded(
               strings.intern(
                   name,
                   string)) &&
        succeeded(
            identities.resolve(
                identities.root(),
                string,
                kind,
                output));
}

[[nodiscard]] bool encode_fixture(
    string_table& strings,
    identity_space& identities,
    graph& G,
    fixture_image& output) noexcept {

    assign_table assigns;
    file_context files;
    source_map sources;

    if (!succeeded(
            sources.finalize(
                files.size(),
                identities,
                G))) {

        return false;
    }

    compiled_project_layout layout;

    if (prepare_compiled_project_layout(
            strings,
            identities,
            G,
            assigns,
            files,
            sources,
            layout) !=
        compiled_project_image_result::success) {

        return false;
    }

    try {
        output.bytes.assign(
            layout.size(),
            std::byte{0});
    }
    catch (...) {
        return false;
    }

    return encode_compiled_project_image(
               strings,
               identities,
               G,
               assigns,
               files,
               sources,
               layout,
               output.bytes) ==
        compiled_project_image_result::success;
}

[[nodiscard]] bool build_object_fixture(
    scenario_kind scenario,
    std::size_t count,
    fixture_image& output) noexcept {

    string_table strings;
    identity_space identities{strings};
    graph G;

    identity_ref type_identity;

    if (!resolve_named_identity(
            strings,
            identities,
            "RuntimeBenchmarkType",
            identity_kind::type,
            type_identity)) {

        return false;
    }

    type_handle type;

    if (!succeeded(
            G.declare_record(
                type_identity,
                graph_record_kind::struct_type,
                type))) {

        return false;
    }

    string_id output_name;
    string_id input_name;

    if (!succeeded(
            strings.intern(
                "out",
                output_name)) ||
        !succeeded(
            strings.intern(
                "in",
                input_name))) {

        return false;
    }

    const auto integer =
        G.intrinsic(
            intrinsic_type::signed_int);

    type_ref integer_reference;

    if (!integer ||
        !succeeded(
            G.derive(
                integer,
                derived_type_kind::lvalue_reference,
                0,
                integer_reference))) {

        return false;
    }

    const member_record members[]{
        {
            output_name,
            integer,
            graph_member_access::public_access,
        },
        {
            input_name,
            integer_reference,
            graph_member_access::public_access,
        },
    };

    const construction_value construction[]{
        {},
        construction_value::member_binding(1),
    };

    if (!succeeded(
            G.define_record(
                type,
                graph_record_kind::struct_type,
                members,
                construction))) {

        return false;
    }

    const auto named =
        G.named(type);

    const auto output_member =
        G.find_member(
            type,
            output_name);

    const auto input_member =
        G.find_member(
            type,
            input_name);

    if (!named ||
        !output_member ||
        !input_member) {

        return false;
    }

    object_handle previous;

    for (std::size_t index = 0;
         index < count;
         ++index) {

        string_id object_name;

        if (!indexed_string(
                strings,
                "o",
                index,
                object_name)) {

            return false;
        }

        identity_ref object_identity;

        if (!succeeded(
                identities.resolve(
                    identities.root(),
                    object_name,
                    identity_kind::object,
                    object_identity))) {

            return false;
        }

        object_handle object;

        if (!succeeded(
                G.add_object(
                    object_identity,
                    named,
                    object))) {

            return false;
        }

        if (index == 0) {
            output.metadata.first_object =
                object;
        }

        if (scenario ==
                scenario_kind::links &&
            previous) {

            link_handle link;

            if (!succeeded(
                    G.add_link(
                        {
                            previous,
                            output_member,
                        },
                        {
                            object,
                            input_member,
                        },
                        link))) {

                return false;
            }
        }

        output.metadata.previous_object =
            previous;

        previous = object;
    }

    output.metadata.type = type;
    output.metadata.output_member =
        output_member;
    output.metadata.input_member =
        input_member;
    output.metadata.last_object =
        previous;

    return encode_fixture(
        strings,
        identities,
        G,
        output);
}

[[nodiscard]] bool build_chain_fixture(
    std::size_t depth,
    fixture_image& output) noexcept {

    if (depth >
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)() - 1)) {

        return false;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;

    identity_ref type_identity;

    if (!resolve_named_identity(
            strings,
            identities,
            "ReferenceChain",
            identity_kind::type,
            type_identity)) {

        return false;
    }

    type_handle type;

    if (!succeeded(
            G.declare_record(
                type_identity,
                graph_record_kind::struct_type,
                type))) {

        return false;
    }

    const auto integer =
        G.intrinsic(
            intrinsic_type::signed_int);

    type_ref integer_reference;

    if (!integer ||
        !succeeded(
            G.derive(
                integer,
                derived_type_kind::lvalue_reference,
                0,
                integer_reference))) {

        return false;
    }

    std::vector<member_record> members;
    std::vector<construction_value> construction;

    try {
        members.resize(
            depth + 1);

        construction.resize(
            depth + 1);
    }
    catch (...) {
        return false;
    }

    string_id first_reference_name;

    for (std::size_t index = 0;
         index < depth;
         ++index) {

        string_id name;

        if (!indexed_string(
                strings,
                "r",
                index,
                name)) {

            return false;
        }

        if (index == 0) {
            first_reference_name = name;
        }

        members[index] = {
            name,
            integer_reference,
            graph_member_access::public_access,
        };

        const auto source_local =
            index + 1;

        construction[index] =
            construction_value::member_binding(
                static_cast<std::uint32_t>(
                    source_local + 1));
    }

    string_id output_name;

    if (!succeeded(
            strings.intern(
                "out",
                output_name))) {

        return false;
    }

    members[depth] = {
        output_name,
        integer,
        graph_member_access::public_access,
    };

    if (!succeeded(
            G.define_record(
                type,
                graph_record_kind::struct_type,
                members,
                construction))) {

        return false;
    }

    identity_ref object_identity;

    if (!resolve_named_identity(
            strings,
            identities,
            "x",
            identity_kind::object,
            object_identity)) {

        return false;
    }

    object_handle object;

    if (!succeeded(
            G.add_object(
                object_identity,
                G.named(type),
                object))) {

        return false;
    }

    const auto first_reference =
        G.find_member(
            type,
            first_reference_name);

    const auto output_member =
        G.find_member(
            type,
            output_name);

    if (!first_reference ||
        !output_member) {

        return false;
    }

    output.metadata.type = type;
    output.metadata.first_reference =
        first_reference;
    output.metadata.output_member =
        output_member;
    output.metadata.first_object =
        object;
    output.metadata.last_object =
        object;

    return encode_fixture(
        strings,
        identities,
        G,
        output);
}

[[nodiscard]] bool build_fixture(
    scenario_kind scenario,
    std::size_t count,
    fixture_image& output) noexcept {

    output = {};

    return scenario ==
        scenario_kind::chain
        ? build_chain_fixture(
            count,
            output)
        : build_object_fixture(
            scenario,
            count,
            output);
}

[[nodiscard]] bool read_pointer(
    const fixed_shared_memory& memory,
    std::uint64_t offset,
    std::uintptr_t& output) noexcept {

    output = 0;

    if (offset >
            memory.size() ||
        sizeof(output) >
            memory.size() -
                static_cast<std::size_t>(
                    offset)) {

        return false;
    }

    std::memcpy(
        &output,
        memory.data() +
            static_cast<std::size_t>(
                offset),
        sizeof(output));

    return true;
}

[[nodiscard]] bool validate_runtime(
    scenario_kind scenario,
    const fixture_metadata& metadata,
    const compiled_project_view& project,
    const runtime_layout& layout,
    const fixed_shared_memory& memory) noexcept {

    type_entry type;

    if (!project.type(
            metadata.type,
            type)) {

        return false;
    }

    record_offset output_member_offset = 0;

    if (!layout.member_offset(
            static_cast<std::size_t>(
                type.members.begin) +
                metadata.output_member.value(),
            output_member_offset)) {

        return false;
    }

    if (scenario ==
        scenario_kind::chain) {

        std::uint64_t object_offset = 0;
        record_offset reference_offset = 0;

        if (!layout.object_offset(
                metadata.first_object,
                object_offset) ||
            !layout.member_offset(
                static_cast<std::size_t>(
                    type.members.begin) +
                    metadata.first_reference.value(),
                reference_offset)) {

            return false;
        }

        std::uintptr_t stored = 0;

        return read_pointer(
                   memory,
                   object_offset +
                       reference_offset,
                   stored) &&
            stored ==
                memory.address() +
                    object_offset +
                    output_member_offset;
    }

    std::uint64_t last_offset = 0;
    record_offset input_member_offset = 0;

    if (!layout.object_offset(
            metadata.last_object,
            last_offset) ||
        !layout.member_offset(
            static_cast<std::size_t>(
                type.members.begin) +
                metadata.input_member.value(),
            input_member_offset)) {

        return false;
    }

    std::uintptr_t stored = 0;

    if (!read_pointer(
            memory,
            last_offset +
                input_member_offset,
            stored)) {

        return false;
    }

    if (scenario ==
        scenario_kind::objects) {

        return stored ==
            memory.address() +
                last_offset +
                output_member_offset;
    }

    std::uint64_t previous_offset = 0;

    if (!metadata.previous_object ||
        !layout.object_offset(
            metadata.previous_object,
            previous_offset)) {

        return false;
    }

    return stored ==
        memory.address() +
            previous_offset +
            output_member_offset;
}

void usage() {
    std::cerr
        << "Usage:\n"
        << "  ServerEngineV4RuntimeBenchmark objects <count>\n"
        << "  ServerEngineV4RuntimeBenchmark links   <count>\n"
        << "  ServerEngineV4RuntimeBenchmark chain   <depth>\n";
}

}

int main(
    int argc,
    char* argv[]) {

    if (argc != 3) {
        usage();
        return 2;
    }

    scenario_kind scenario;

    if (!parse_scenario(
            argv[1],
            scenario)) {

        usage();
        return 2;
    }

    std::size_t count = 0;

    if (!parse_count(
            argv[2],
            count)) {

        std::cerr
            << "Invalid count\n";

        return 2;
    }

    const auto setup_started =
        std::chrono::steady_clock::now();

    fixture_image fixture;

    if (!build_fixture(
            scenario,
            count,
            fixture)) {

        std::cerr
            << "Cannot build Runtime benchmark fixture\n";

        return 1;
    }

    compiled_project_view project;

    if (project.bind(
            fixture.bytes) !=
        compiled_project_image_result::success) {

        std::cerr
            << "Cannot bind Runtime benchmark compiled image\n";

        return 1;
    }

    const auto setup_finished =
        std::chrono::steady_clock::now();

    const auto abi =
        native_abi();

    const auto layout_started =
        std::chrono::steady_clock::now();

    runtime_layout layout;

    if (prepare_runtime_layout(
            project,
            abi,
            layout) !=
        runtime_layout_result::success) {

        std::cerr
            << "Runtime layout failed\n";

        return 1;
    }

    const auto layout_finished =
        std::chrono::steady_clock::now();

    std::size_t mapping_size = 0;

    if (!mapping_size_for(
            layout.size(),
            mapping_size)) {

        std::cerr
            << "Cannot page-align Runtime size\n";

        return 1;
    }

    const auto shm_started =
        std::chrono::steady_clock::now();

    fixed_shared_memory memory;

    const auto name =
        std::string{
            "CW.ServerEngineV4.RuntimeBenchmark."} +
        std::to_string(
            process_id());

    constexpr std::uintptr_t fixed_address =
        0x0000020000000000ull;

    const auto created =
        memory.create(
            name,
            mapping_size,
            fixed_address);

    if (created !=
        fixed_shared_memory_result::success) {

        std::cerr
            << "FIXED_DIRECT benchmark SHM create failed: "
            << static_cast<int>(created)
            << '\n';

        return 1;
    }

    const auto shm_finished =
        std::chrono::steady_clock::now();

    const auto materialize_started =
        std::chrono::steady_clock::now();

    const auto materialized =
        materialize_fixed_direct(
            project,
            layout,
            abi,
            memory.bytes());

    const auto materialize_finished =
        std::chrono::steady_clock::now();

    if (materialized !=
        fixed_direct_materialization_result::success) {

        std::cerr
            << "FIXED_DIRECT materialization failed: "
            << static_cast<int>(materialized)
            << '\n';

        return 1;
    }

    if (!validate_runtime(
            scenario,
            fixture.metadata,
            project,
            layout,
            memory)) {

        std::cerr
            << "Runtime reference validation failed\n";

        return 3;
    }

    const auto milliseconds =
        [](auto begin, auto end) {
            return std::chrono::duration<double, std::milli>{
                end - begin}
                .count();
        };

    const auto setup_ms =
        milliseconds(
            setup_started,
            setup_finished);

    const auto layout_ms =
        milliseconds(
            layout_started,
            layout_finished);

    const auto shm_ms =
        milliseconds(
            shm_started,
            shm_finished);

    const auto materialize_ms =
        milliseconds(
            materialize_started,
            materialize_finished);

    std::cout
        << "scenario="
        << argv[1]
        << ",count="
        << count
        << ",setup_ms="
        << setup_ms
        << ",layout_ms="
        << layout_ms
        << ",shm_create_ms="
        << shm_ms
        << ",materialize_ms="
        << materialize_ms
        << ",runtime_total_ms="
        << layout_ms + shm_ms + materialize_ms
        << ",runtime_bytes="
        << layout.size()
        << ",mapping_bytes="
        << mapping_size
        << ",compiled_bytes="
        << fixture.bytes.size()
        << ",types="
        << project.type_count()
        << ",members="
        << project.member_count()
        << ",objects="
        << project.object_count()
        << ",links="
        << project.link_count()
        << ",peak_ws_bytes="
        << peak_working_set_bytes()
        << '\n';

    return 0;
}
