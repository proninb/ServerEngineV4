/*
 * FIXED_DIRECT Runtime scale benchmark.
 *
 * Fixture construction, compiled.bin encoding, and compiled_project_view::bind()
 * are measured in setup_ms, separately from the Runtime pipeline. The timed
 * Runtime stages are:
 *
 *   runtime_layout -> fixed SHM -> materialization
 *
 * Scenarios:
 *   objects N : one T { int out; int& in = out; } and N objects
 *   links N   : the same N objects with object i.in -> object i-1.out
 *   indexed_links N : source[N].values[2] -> target[N].in through canonical
 *                     endpoint paths
 *   chain N   : one object whose record contains N reference members chained
 *               to one final int member
 *   many_types N : N distinct records, each with 16 int members plus int&,
 *                  two objects, and one link per record
 */
#include "fixed_shared_memory.hpp"
#include "project/assign/assign_table.hpp"
#include "project/file/file_context.hpp"
#include "project/graph/graph.hpp"
#include "project/persistence/compiled_project.hpp"
#include "project/runtime/fixed_direct_materializer.hpp"
#include "project/runtime/runtime_layout.hpp"
#include "project/runtime/runtime_ic_codec.hpp"
#include "project/runtime/runtime_ic_reset.hpp"
#include "project/runtime/runtime_ic_snapshot.hpp"
#include "project/shm/shm_layout.hpp"
#include "project/shm/shm_runtime_v2.hpp"
#include "project/semantic/identity.hpp"
#include "project/source/source_map.hpp"
#include "project/string/string_table.hpp"

#include <charconv>
#include <algorithm>
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
    indexed_links,
    chain,
    many_types,
};

struct fixture_metadata final {
    type_handle type{};
    type_handle target_type{};
    member_index output_member{};
    member_index input_member{};
    member_index first_reference{};
    object_handle first_object{};
    object_handle previous_object{};
    object_handle last_object{};
    std::uint64_t element_count = 0;
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

    if (value == "indexed_links") {
        output = scenario_kind::indexed_links;
        return true;
    }

    if (value == "chain") {
        output = scenario_kind::chain;
        return true;
    }

    if (value == "many_types") {
        output = scenario_kind::many_types;
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
                            G.identity(
                                previous),
                            output_member,
                        },
                        {
                            object_identity,
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

[[nodiscard]] bool build_many_types_fixture(
    std::size_t count,
    fixture_image& output) noexcept {

    string_table strings;
    identity_space identities{strings};
    graph G;

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

    constexpr std::size_t value_member_count = 16;
    constexpr std::size_t member_count =
        value_member_count + 1;

    string_id member_names[member_count]{};

    for (std::size_t index = 0;
         index < value_member_count;
         ++index) {

        if (!indexed_string(
                strings,
                "field_",
                index,
                member_names[index])) {

            return false;
        }
    }

    if (!succeeded(
            strings.intern(
                "input",
                member_names[value_member_count]))) {

        return false;
    }

    member_record members[member_count]{};
    construction_value construction[member_count]{};

    for (std::size_t index = 0;
         index < value_member_count;
         ++index) {

        members[index] = {
            member_names[index],
            integer,
            graph_member_access::public_access,
        };
    }

    members[value_member_count] = {
        member_names[value_member_count],
        integer_reference,
        graph_member_access::public_access,
    };

    construction[value_member_count] =
        construction_value::member_binding(1);

    for (std::size_t index = 0;
         index < count;
         ++index) {

        string_id type_name;

        if (!indexed_string(
                strings,
                "T",
                index,
                type_name)) {

            return false;
        }

        identity_ref type_identity;

        if (!succeeded(
                identities.resolve(
                    identities.root(),
                    type_name,
                    identity_kind::type,
                    type_identity))) {

            return false;
        }

        type_handle type;

        if (!succeeded(
                G.declare_record(
                    type_identity,
                    graph_record_kind::struct_type,
                    type)) ||
            !succeeded(
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
                member_names[0]);

        const auto input_member =
            G.find_member(
                type,
                member_names[value_member_count]);

        if (!named ||
            !output_member ||
            !input_member) {

            return false;
        }

        string_id first_name;
        string_id second_name;

        if (!indexed_string(
                strings,
                "a",
                index,
                first_name) ||
            !indexed_string(
                strings,
                "b",
                index,
                second_name)) {

            return false;
        }

        identity_ref first_identity;
        identity_ref second_identity;

        if (!succeeded(
                identities.resolve(
                    identities.root(),
                    first_name,
                    identity_kind::object,
                    first_identity)) ||
            !succeeded(
                identities.resolve(
                    identities.root(),
                    second_name,
                    identity_kind::object,
                    second_identity))) {

            return false;
        }

        object_handle first_object;
        object_handle second_object;

        if (!succeeded(
                G.add_object(
                    first_identity,
                    named,
                    first_object)) ||
            !succeeded(
                G.add_object(
                    second_identity,
                    named,
                    second_object))) {

            return false;
        }

        link_handle link;

        if (!succeeded(
                G.add_link(
                    {
                        first_identity,
                        output_member,
                    },
                    {
                        second_identity,
                        input_member,
                    },
                    link))) {

            return false;
        }

        output.metadata.type = type;
        output.metadata.output_member =
            output_member;
        output.metadata.input_member =
            input_member;
        output.metadata.previous_object =
            first_object;
        output.metadata.last_object =
            second_object;
    }

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


[[nodiscard]] bool build_indexed_links_fixture(
    std::size_t count,
    fixture_image& output) noexcept {

    string_table strings;
    identity_space identities{strings};
    graph G;

    identity_ref source_type_identity;
    identity_ref target_type_identity;
    identity_ref source_object_identity;
    identity_ref target_object_identity;

    if (!resolve_named_identity(
            strings,
            identities,
            "IndexedSource",
            identity_kind::type,
            source_type_identity) ||
        !resolve_named_identity(
            strings,
            identities,
            "IndexedTarget",
            identity_kind::type,
            target_type_identity) ||
        !resolve_named_identity(
            strings,
            identities,
            "source",
            identity_kind::object,
            source_object_identity) ||
        !resolve_named_identity(
            strings,
            identities,
            "target",
            identity_kind::object,
            target_object_identity)) {

        return false;
    }

    type_handle source_type;
    type_handle target_type;

    if (!succeeded(
            G.declare_record(
                source_type_identity,
                graph_record_kind::struct_type,
                source_type)) ||
        !succeeded(
            G.declare_record(
                target_type_identity,
                graph_record_kind::struct_type,
                target_type))) {

        return false;
    }

    string_id values_name;
    string_id input_name;

    if (!succeeded(
            strings.intern(
                "values",
                values_name)) ||
        !succeeded(
            strings.intern(
                "in",
                input_name))) {

        return false;
    }

    const auto integer =
        G.intrinsic(
            intrinsic_type::signed_int);

    type_ref values_type;
    type_ref input_type;

    if (!integer ||
        !succeeded(
            G.derive(
                integer,
                derived_type_kind::bounded_array,
                4,
                values_type)) ||
        !succeeded(
            G.derive(
                integer,
                derived_type_kind::lvalue_reference,
                0,
                input_type))) {

        return false;
    }

    const member_record source_members[]{
        {
            values_name,
            values_type,
            graph_member_access::public_access,
        },
    };

    const member_record target_members[]{
        {
            input_name,
            input_type,
            graph_member_access::public_access,
        },
    };

    if (!succeeded(
            G.define_record(
                source_type,
                graph_record_kind::struct_type,
                source_members)) ||
        !succeeded(
            G.define_record(
                target_type,
                graph_record_kind::struct_type,
                target_members))) {

        return false;
    }

    const auto source_named =
        G.named(
            source_type);

    const auto target_named =
        G.named(
            target_type);

    type_ref source_array;
    type_ref target_array;

    if (!source_named ||
        !target_named ||
        !succeeded(
            G.derive(
                source_named,
                derived_type_kind::bounded_array,
                count,
                source_array)) ||
        !succeeded(
            G.derive(
                target_named,
                derived_type_kind::bounded_array,
                count,
                target_array))) {

        return false;
    }

    object_handle source_object;
    object_handle target_object;

    if (!succeeded(
            G.add_object(
                source_object_identity,
                source_array,
                source_object)) ||
        !succeeded(
            G.add_object(
                target_object_identity,
                target_array,
                target_object))) {

        return false;
    }

    const auto values =
        G.find_member(
            source_type,
            values_name);

    const auto input =
        G.find_member(
            target_type,
            input_name);

    if (!values ||
        !input) {

        return false;
    }

    for (std::size_t index = 0;
         index < count;
         ++index) {

        const endpoint_path_step source_steps[]{
            {
                index,
                endpoint_path_step_kind::array_index,
                {},
            },
            {
                values.value(),
                endpoint_path_step_kind::member,
                {},
            },
            {
                2,
                endpoint_path_step_kind::array_index,
                {},
            },
        };

        const endpoint_path_step target_steps[]{
            {
                index,
                endpoint_path_step_kind::array_index,
                {},
            },
            {
                input.value(),
                endpoint_path_step_kind::member,
                {},
            },
        };

        endpoint_path_handle source_path;
        endpoint_path_handle target_path;
        type_ref source_value;
        type_ref target_value;

        if (!succeeded(
                G.intern_endpoint_path(
                    source_array,
                    source_steps,
                    source_path,
                    &source_value)) ||
            !succeeded(
                G.intern_endpoint_path(
                    target_array,
                    target_steps,
                    target_path,
                    &target_value)) ||
            source_value !=
                integer ||
            target_value !=
                input_type) {

            return false;
        }

        link_handle link;

        if (!succeeded(
                G.add_link(
                    {
                        source_object_identity,
                        endpoint_ref::from_path(
                            source_path),
                    },
                    {
                        target_object_identity,
                        endpoint_ref::from_path(
                            target_path),
                    },
                    link))) {

            return false;
        }
    }

    output.metadata.type =
        source_type;

    output.metadata.target_type =
        target_type;

    output.metadata.output_member =
        values;

    output.metadata.input_member =
        input;

    output.metadata.first_object =
        source_object;

    output.metadata.last_object =
        target_object;

    output.metadata.element_count =
        count;

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

    if (scenario ==
        scenario_kind::chain) {

        return build_chain_fixture(
            count,
            output);
    }

    if (scenario ==
        scenario_kind::many_types) {

        return build_many_types_fixture(
            count,
            output);
    }

    if (scenario ==
        scenario_kind::indexed_links) {

        return build_indexed_links_fixture(
            count,
            output);
    }

    return build_object_fixture(
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
        scenario_kind::indexed_links) {

        if (!metadata.target_type ||
            !metadata.first_object ||
            !metadata.last_object ||
            metadata.element_count == 0) {

            return false;
        }

        type_entry target_type;

        runtime_value_layout source_record;
        runtime_value_layout target_record;

        record_offset input_member_offset = 0;
        std::uint64_t source_offset = 0;
        std::uint64_t target_offset = 0;

        if (!project.type(
                metadata.target_type,
                target_type) ||
            !layout.type(
                metadata.type,
                source_record) ||
            !layout.type(
                metadata.target_type,
                target_record) ||
            !layout.member_offset(
                static_cast<std::size_t>(
                    target_type.members.begin) +
                    metadata.input_member.value(),
                input_member_offset) ||
            !layout.object_offset(
                metadata.first_object,
                source_offset) ||
            !layout.object_offset(
                metadata.last_object,
                target_offset)) {

            return false;
        }

        const auto element =
            metadata.element_count - 1;

        if ((source_record.size != 0 &&
             element >
                (std::numeric_limits<std::uint64_t>::max)() /
                    source_record.size) ||
            (target_record.size != 0 &&
             element >
                (std::numeric_limits<std::uint64_t>::max)() /
                    target_record.size)) {

            return false;
        }

        const auto source_element =
            element *
            source_record.size;

        const auto target_element =
            element *
            target_record.size;

        if (source_offset >
                (std::numeric_limits<std::uint64_t>::max)() -
                    source_element ||
            source_offset +
                source_element >
                    (std::numeric_limits<std::uint64_t>::max)() -
                        output_member_offset -
                        2 * sizeof(int) ||
            target_offset >
                (std::numeric_limits<std::uint64_t>::max)() -
                    target_element ||
            target_offset +
                target_element >
                    (std::numeric_limits<std::uint64_t>::max)() -
                        input_member_offset) {

            return false;
        }

        const auto expected =
            memory.address() +
            source_offset +
            source_element +
            output_member_offset +
            2 * sizeof(int);

        std::uintptr_t stored = 0;

        return read_pointer(
                   memory,
                   target_offset +
                       target_element +
                       input_member_offset,
                   stored) &&
            stored ==
                expected;
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



[[nodiscard]] bool validate_runtime_v2(
    scenario_kind scenario,
    const fixture_metadata& metadata,
    const compiled_project_view& project,
    const shm_layout& layout,
    const fixed_shared_memory& memory) noexcept {

    type_entry type;

    if (!project.type(
            metadata.type,
            type)) {

        return false;
    }

    shm_record_offset output_member_offset = 0;

    if (!layout.member_offset(
            static_cast<std::size_t>(
                type.members.begin) +
                metadata.output_member.value(),
            output_member_offset)) {

        return false;
    }


    if (scenario ==
        scenario_kind::indexed_links) {

        if (!metadata.target_type ||
            !metadata.first_object ||
            !metadata.last_object ||
            metadata.element_count == 0) {

            return false;
        }

        type_entry target_type;

        shm_value_layout source_record;
        shm_value_layout target_record;

        shm_record_offset input_member_offset = 0;
        shm_offset source_offset = 0;
        shm_offset target_offset = 0;

        if (!project.type(
                metadata.target_type,
                target_type) ||
            !layout.type(
                metadata.type,
                source_record) ||
            !layout.type(
                metadata.target_type,
                target_record) ||
            !layout.member_offset(
                static_cast<std::size_t>(
                    target_type.members.begin) +
                    metadata.input_member.value(),
                input_member_offset) ||
            !layout.object_offset(
                metadata.first_object,
                source_offset) ||
            !layout.object_offset(
                metadata.last_object,
                target_offset)) {

            return false;
        }

        const auto element =
            metadata.element_count - 1;

        if ((source_record.size != 0 &&
             element >
                (std::numeric_limits<std::uint64_t>::max)() /
                    source_record.size) ||
            (target_record.size != 0 &&
             element >
                (std::numeric_limits<std::uint64_t>::max)() /
                    target_record.size)) {

            return false;
        }

        const auto source_element =
            element *
            source_record.size;

        const auto target_element =
            element *
            target_record.size;

        if (source_offset >
                (std::numeric_limits<std::uint64_t>::max)() -
                    source_element ||
            source_offset +
                source_element >
                    (std::numeric_limits<std::uint64_t>::max)() -
                        output_member_offset -
                        2 * sizeof(int) ||
            target_offset >
                (std::numeric_limits<std::uint64_t>::max)() -
                    target_element ||
            target_offset +
                target_element >
                    (std::numeric_limits<std::uint64_t>::max)() -
                        input_member_offset) {

            return false;
        }

        const auto expected =
            memory.address() +
            source_offset +
            source_element +
            output_member_offset +
            2 * sizeof(int);

        std::uintptr_t stored = 0;

        return read_pointer(
                   memory,
                   target_offset +
                       target_element +
                       input_member_offset,
                   stored) &&
            stored ==
                expected;
    }

    if (scenario ==
        scenario_kind::chain) {

        shm_offset object_offset = 0;
        shm_record_offset reference_offset = 0;

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

    shm_offset last_offset = 0;
    shm_record_offset input_member_offset = 0;

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

    shm_offset previous_offset = 0;

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



[[nodiscard]] int run_runtime_v2_kernel_benchmark(
    scenario_kind scenario,
    std::string_view scenario_name,
    std::size_t count,
    double setup_ms,
    const fixture_image& fixture,
    const compiled_project_view& project,
    const server_abi_configuration& abi) {

    using clock_type =
        std::chrono::steady_clock;

    const auto milliseconds =
        [](auto begin, auto end) {
            return std::chrono::duration<
                double,
                std::milli>{
                    end - begin}
                .count();
        };

    shm_layout layout;

    const auto layout_started =
        clock_type::now();

    const auto layout_result =
        prepare_shm_layout(
            project,
            abi,
            layout);

    const auto layout_finished =
        clock_type::now();

    if (layout_result !=
        shm_layout_result::success) {

        std::cerr
            << "Runtime V2 SHM layout failed: "
            << static_cast<int>(
                layout_result)
            << '\n';

        return 1;
    }

    shm_runtime_v2 runtime;
    shm_runtime_v2_prepare_telemetry
        prepare_telemetry;

    const auto prepare_started =
        clock_type::now();

    const auto prepared =
        prepare_shm_runtime_v2(
            project,
            abi,
            layout,
            runtime,
            &prepare_telemetry);

    const auto prepare_finished =
        clock_type::now();

    if (prepared !=
        shm_runtime_v2_result::success) {

        std::cerr
            << "Runtime V2 prepare failed: "
            << static_cast<int>(
                prepared)
            << '\n';

        return 1;
    }

    std::size_t mapping_size = 0;

    if (!mapping_size_for(
            layout.size(),
            mapping_size)) {

        std::cerr
            << "Cannot page-align Runtime V2 size\n";

        return 1;
    }

    fixed_shared_memory memory;

    const auto name =
        std::string{
            "CW.ServerEngineV4.RuntimeV2Benchmark."} +
        std::to_string(
            process_id());

    constexpr std::uintptr_t fixed_address =
        0x0000020000000000ull;

    const auto shm_started =
        clock_type::now();

    const auto created =
        memory.create(
            name,
            mapping_size,
            fixed_address);

    const auto shm_finished =
        clock_type::now();

    if (created !=
        fixed_shared_memory_result::success) {

        std::cerr
            << "Runtime V2 benchmark SHM create failed: "
            << static_cast<int>(
                created)
            << '\n';

        return 1;
    }

    shm_runtime_v2_execute_telemetry
        canonical_telemetry;

    shm_runtime_v2_execute_telemetry
        object_telemetry;

    shm_runtime_v2_link_telemetry
        link_telemetry;

    shm_runtime_v2_initialization_telemetry
        initialization_telemetry;

    const auto canonical_started =
        clock_type::now();

    const auto canonical =
        materialize_shm_runtime_v2_canonical(
            runtime,
            abi,
            layout,
            memory.bytes(),
            &canonical_telemetry);

    const auto canonical_finished =
        clock_type::now();

    if (canonical !=
        shm_runtime_v2_result::success) {

        std::cerr
            << "Runtime V2 canonical materialization failed: "
            << static_cast<int>(
                canonical)
            << '\n';

        return 1;
    }

    const auto mark_started =
        clock_type::now();

    const auto marked =
        mark_shm_runtime_v2_links(
            runtime,
            abi,
            layout,
            memory.bytes(),
            &link_telemetry);

    const auto mark_finished =
        clock_type::now();

    if (marked !=
        shm_runtime_v2_result::success) {

        std::cerr
            << "Runtime V2 link mark failed: "
            << static_cast<int>(
                marked)
            << '\n';

        return 1;
    }

    const auto objects_started =
        clock_type::now();

    const auto objects =
        materialize_shm_runtime_v2_objects(
            runtime,
            abi,
            layout,
            memory.bytes(),
            &object_telemetry);

    const auto objects_finished =
        clock_type::now();

    if (objects !=
        shm_runtime_v2_result::success) {

        std::cerr
            << "Runtime V2 object materialization failed: "
            << static_cast<int>(
                objects)
            << '\n';

        return 1;
    }

    const auto links_started =
        clock_type::now();

    const auto links =
        materialize_shm_runtime_v2_links(
            runtime,
            abi,
            layout,
            memory.bytes(),
            &link_telemetry);

    const auto links_finished =
        clock_type::now();

    if (links !=
        shm_runtime_v2_result::success) {

        std::cerr
            << "Runtime V2 link materialization failed: "
            << static_cast<int>(
                links)
            << '\n';

        return 1;
    }

    const auto initializations_started =
        clock_type::now();

    const auto initializations =
        materialize_shm_runtime_v2_initializations(
            runtime,
            abi,
            layout,
            memory.bytes(),
            &initialization_telemetry);

    const auto initializations_finished =
        clock_type::now();

    if (initializations !=
        shm_runtime_v2_result::success) {

        std::cerr
            << "Runtime V2 initialization materialization failed: "
            << static_cast<int>(
                initializations)
            << '\n';

        return 1;
    }

    if (!validate_runtime_v2(
            scenario,
            fixture.metadata,
            project,
            layout,
            memory)) {

        std::cerr
            << "Runtime V2 reference validation failed\n";

        return 3;
    }

    const auto layout_ms =
        milliseconds(
            layout_started,
            layout_finished);

    const auto prepare_ms =
        milliseconds(
            prepare_started,
            prepare_finished);

    const auto shm_ms =
        milliseconds(
            shm_started,
            shm_finished);

    const auto canonical_ms =
        milliseconds(
            canonical_started,
            canonical_finished);

    const auto links_mark_ms =
        milliseconds(
            mark_started,
            mark_finished);

    const auto objects_ms =
        milliseconds(
            objects_started,
            objects_finished);

    const auto links_ms =
        milliseconds(
            links_started,
            links_finished);

    const auto initializations_ms =
        milliseconds(
            initializations_started,
            initializations_finished);

    const auto materialize_ms =
        canonical_ms +
        links_mark_ms +
        objects_ms +
        links_ms +
        initializations_ms;

    const auto runtime_total_ms =
        layout_ms +
        prepare_ms +
        shm_ms +
        materialize_ms;

    std::cout
        << "benchmark=v2_kernel"
        << ",scenario="
        << scenario_name
        << ",count="
        << count
        << ",setup_ms="
        << setup_ms
        << ",layout_ms="
        << layout_ms
        << ",prepare_ms="
        << prepare_ms
        << ",shm_create_ms="
        << shm_ms
        << ",canonical_ms="
        << canonical_ms
        << ",links_mark_ms="
        << links_mark_ms
        << ",objects_ms="
        << objects_ms
        << ",links_ms="
        << links_ms
        << ",initializations_ms="
        << initializations_ms
        << ",materialize_ms="
        << materialize_ms
        << ",runtime_total_ms="
        << runtime_total_ms
        << ",runtime_bytes="
        << layout.size()
        << ",mapping_bytes="
        << mapping_size
        << ",compiled_bytes="
        << fixture.bytes.size()
        << ",construction_bytes="
        << runtime.construction_bytes()
        << ",type_apis="
        << prepare_telemetry.type_apis
        << ",inline_leaf_operations="
        << prepare_telemetry.
            inline_leaf_operations
        << ",inline_subtree_operations="
        << prepare_telemetry.
            inline_subtree_operations
        << ",canonical_api_applications="
        << canonical_telemetry.
            api_applications
        << ",object_api_applications="
        << object_telemetry.
            api_applications
        << ",object_child_visits="
        << object_telemetry.child_visits
        << ",constructor_default_writes="
        << object_telemetry.
            constructor_default_writes
        << ",links_prepared="
        << link_telemetry.links_prepared
        << ",links_resolved="
        << link_telemetry.links_resolved
        << ",link_recursive_resolutions="
        << link_telemetry.
            recursive_resolutions
        << ",link_dereference_reads="
        << link_telemetry.
            dereference_reads
        << ",initialization_writes="
        << initialization_telemetry.writes
        << ",initialization_dereference_reads="
        << initialization_telemetry.
            dereference_reads
        << ",types="
        << project.type_count()
        << ",members="
        << project.member_count()
        << ",objects="
        << project.object_count()
        << ",links="
        << project.link_count()
        << ",endpoint_paths="
        << project.endpoint_path_count()
        << ",endpoint_path_steps="
        << project.endpoint_path_step_count()
        << ",peak_ws_bytes="
        << peak_working_set_bytes()
        << '\n';

    return 0;
}


void usage() {
    std::cerr
        << "Usage:\n"
        << "  ServerEngineV4RuntimeBenchmark objects       <count>\n"
        << "  ServerEngineV4RuntimeBenchmark links         <count>\n"
        << "  ServerEngineV4RuntimeBenchmark indexed_links <count>\n"
        << "  ServerEngineV4RuntimeBenchmark chain         <depth>\n"
        << "  ServerEngineV4RuntimeBenchmark many_types    <type-count>\n"
        << "\n"
        << "Final Runtime V2 kernel on the same fixtures:\n"
        << "  ServerEngineV4RuntimeBenchmark <scenario> <count> v2\n"
        << "\n"
        << "Production IC SNAP benchmark:\n"
        << "  ServerEngineV4RuntimeBenchmark <scenario> <count> <iterations> snap_v2_only\n";
}

}

int main(
    int argc,
    char* argv[]) {

    if (argc != 3 &&
        argc != 4 &&
        argc != 5) {

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

    std::size_t ic_iterations = 0;

    if (argc == 5 &&
        !parse_count(
            argv[3],
            ic_iterations)) {

        std::cerr
            << "Invalid IC iteration count\n";

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

    const bool v2_kernel =
        argc == 4 &&
        std::string_view{argv[3]} == "v2";

    if (argc == 4 &&
        !v2_kernel) {

        std::cerr
            << "Unknown Runtime benchmark mode\n";

        return 2;
    }

    if (v2_kernel) {
        const auto setup_ms =
            std::chrono::duration<
                double,
                std::milli>{
                    setup_finished -
                    setup_started}
                .count();

        return run_runtime_v2_kernel_benchmark(
            scenario,
            argv[1],
            count,
            setup_ms,
            fixture,
            project,
            abi);
    }

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

    const bool snap_v2_only =
        argc == 5 &&
        std::string_view{argv[4]} ==
            "snap_v2_only";

    if (argc == 5 &&
        !snap_v2_only) {

        std::cerr
            << "Unknown benchmark mode\n";

        return 2;
    }

    if (snap_v2_only) {
        runtime_binding_index runtime_bindings;

        if (!layout.release_bindings(
                runtime_bindings)) {

            std::cerr
                << "Cannot publish Runtime bindings for IC benchmark\n";

            return 1;
        }

        const auto logical_runtime_size =
            static_cast<std::size_t>(
                layout.size());

        if (logical_runtime_size >
            memory.bytes().size()) {

            std::cerr
                << "Runtime size exceeds SHM mapping\n";

            return 1;
        }

        auto runtime_bytes =
            memory.bytes().first(
                logical_runtime_size);

        std::vector<double> samples;

        try {
            samples.reserve(
                ic_iterations);
        }
        catch (...) {
            std::cerr
                << "SNAP V2 benchmark sample allocation failed\n";

            return 1;
        }

        {
            std::vector<std::byte> warm_image;

            if (snapshot_runtime_ic_binary(
                    project,
                    runtime_bindings,
                    runtime_bytes,
                    warm_image) !=
                runtime_ic_snapshot_result::success) {

                std::cerr
                    << "Runtime IC SNAP warmup failed\n";

                return 1;
            }
        }

        const auto peak_before =
            peak_working_set_bytes();

        std::vector<std::byte> image;
        runtime_ic_snapshot_stats snapshot_stats;

        for (std::size_t iteration = 0;
             iteration < ic_iterations;
             ++iteration) {

            const auto started =
                std::chrono::steady_clock::now();

            std::vector<std::byte> iteration_image;
            runtime_ic_snapshot_stats iteration_stats;

            if (snapshot_runtime_ic_binary(
                    project,
                    runtime_bindings,
                    runtime_bytes,
                    iteration_image,
                    &iteration_stats) !=
                runtime_ic_snapshot_result::success) {

                std::cerr
                    << "Runtime IC SNAP V2 benchmark failed\n";

                return 1;
            }

            const auto finished =
                std::chrono::steady_clock::now();

            samples.push_back(
                std::chrono::duration<double, std::milli>{
                    finished - started}
                    .count());

            snapshot_stats =
                iteration_stats;

            if (iteration + 1 ==
                ic_iterations) {

                image =
                    std::move(
                        iteration_image);
            }
        }

        std::sort(
            samples.begin(),
            samples.end());

        const auto middle =
            samples.size() / 2;

        const auto snapshot_ms =
            samples.size() % 2 != 0
            ? samples[middle]
            : (samples[middle - 1] +
               samples[middle]) /
                2.0;

        runtime_ic_binary_view view;

        if (view.bind(
                image) !=
            runtime_ic_codec_result::success) {

            std::cerr
                << "Runtime IC SNAP image bind failed\n";

            return 1;
        }

        runtime_ic_reset_stats reset_stats;

        if (reset_runtime_ic_binary(
                project,
                runtime_bindings,
                runtime_bytes,
                view,
                &reset_stats) !=
            runtime_ic_reset_result::success ||
            reset_stats.records !=
                snapshot_stats.scalars) {

            std::cerr
                << "Runtime IC SNAP RESET compatibility failed\n";

            return 1;
        }

        const auto peak_after =
            peak_working_set_bytes();

        const auto peak_delta =
            peak_after > peak_before
            ? peak_after - peak_before
            : std::uint64_t{0};

        const auto records_per_second =
            snapshot_ms > 0.0
            ? static_cast<double>(
                  snapshot_stats.scalars) *
                  1000.0 /
                  snapshot_ms
            : 0.0;

        std::cout
            << "scenario="
            << argv[1]
            << ",count="
            << count
            << ",ic_records="
            << snapshot_stats.scalars
            << ",ic_image_bytes="
            << image.size()
            << ",snap_ms="
            << snapshot_ms
            << ",snap_records_per_s="
            << records_per_second
            << ",reset_ok=1"
            << ",peak_ws_before_snap_bytes="
            << peak_before
            << ",peak_ws_after_snap_bytes="
            << peak_after
            << ",peak_ws_delta_bytes="
            << peak_delta
            << '\n';

        return 0;
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
        << "benchmark=legacy_kernel"
        << ",scenario="
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
        << ",endpoint_paths="
        << project.endpoint_path_count()
        << ",endpoint_path_steps="
        << project.endpoint_path_step_count()
        << ",peak_ws_bytes="
        << peak_working_set_bytes()
        << '\n';

    return 0;

}