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
                        first_object,
                        output_member,
                    },
                    {
                        second_object,
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
                        source_object,
                        endpoint_ref::from_path(
                            source_path),
                    },
                    {
                        target_object,
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



struct reset_profile_record final {
    std::uint32_t object_begin = 0;
    std::uint16_t object_count = 0;
    std::uint16_t member_count = 0;
    std::uint32_t member_begin = 0;
    intrinsic_type type = intrinsic_type::none;
    std::uint8_t size = 0;
    std::uint16_t reserved = 0;
    const std::byte* value = nullptr;
};

struct reset_profile_target final {
    runtime_offset offset = 0;
    std::uint32_t record = 0;
    std::uint8_t size = 0;
    std::uint8_t reserved[3]{};
};

static_assert(sizeof(reset_profile_target) == 16);

[[nodiscard]] bool profile_reset_phases(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    const runtime_ic_binary_view& image,
    std::vector<std::string_view>& components,
    std::vector<reset_profile_record>& records,
    std::vector<reset_profile_target>& targets,
    double& decode_ms,
    double& resolve_ms,
    double& apply_ms) noexcept {

    components.clear();
    records.clear();
    targets.clear();

    std::size_t component_count = 0;

    for (std::size_t index = 0;
         index < image.record_count();
         ++index) {

        runtime_ic_binary_record_view record;

        if (!image.record(
                index,
                record)) {

            return false;
        }

        component_count +=
            static_cast<std::size_t>(
                record.object_count) +
            static_cast<std::size_t>(
                record.member_count);
    }

    try {
        components.reserve(
            component_count);

        records.reserve(
            image.record_count());

        targets.reserve(
            image.record_count());
    }
    catch (...) {
        return false;
    }

    const auto decode_started =
        std::chrono::steady_clock::now();

    for (std::size_t index = 0;
         index < image.record_count();
         ++index) {

        runtime_ic_binary_record_view record;

        if (!image.record(
                index,
                record) ||
            record.value.empty() ||
            record.value.size() >
                runtime_ic_scalar_value{}.
                    bytes.size()) {

            return false;
        }

        const auto object_begin =
            components.size();

        for (std::uint32_t component = 0;
             component < record.object_count;
             ++component) {

            const auto id =
                image.component(
                    record.object_begin +
                    component);

            const auto text =
                image.string(id);

            if (id == 0 ||
                text.empty()) {

                return false;
            }

            components.push_back(
                text);
        }

        const auto member_begin =
            components.size();

        for (std::uint32_t component = 0;
             component < record.member_count;
             ++component) {

            const auto id =
                image.component(
                    record.member_begin +
                    component);

            const auto text =
                image.string(id);

            if (id == 0 ||
                text.empty()) {

                return false;
            }

            components.push_back(
                text);
        }

        if (object_begin >
                (std::numeric_limits<std::uint32_t>::max)() ||
            member_begin >
                (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        records.push_back(
            {
                static_cast<std::uint32_t>(
                    object_begin),
                record.object_count,
                record.member_count,
                static_cast<std::uint32_t>(
                    member_begin),
                record.type,
                static_cast<std::uint8_t>(
                    record.value.size()),
                0,
                record.value.data(),
            });
    }

    const auto decode_finished =
        std::chrono::steady_clock::now();

    const auto resolve_started =
        decode_finished;

    for (std::size_t index = 0;
         index < records.size();
         ++index) {

        const auto& record =
            records[index];

        runtime_ic_scalar_target target;

        if (resolve_runtime_ic_scalar(
                project,
                bindings,
                static_cast<std::uint64_t>(
                    runtime.size()),
                {
                    {
                        components.data() +
                            record.object_begin,
                        record.object_count,
                    },
                    {
                        components.data() +
                            record.member_begin,
                        record.member_count,
                    },
                },
                target) !=
            runtime_ic_result::success) {

            return false;
        }

        if (target.type !=
                record.type ||
            target.size !=
                record.size) {

            return false;
        }

        targets.push_back(
            {
                target.offset,
                static_cast<std::uint32_t>(
                    index),
                target.size,
                {},
            });
    }

    const auto resolve_finished =
        std::chrono::steady_clock::now();

    const auto apply_started =
        resolve_finished;

    for (const auto& target :
         targets) {

        const auto& record =
            records[target.record];

        std::memcpy(
            runtime.data() +
                static_cast<std::size_t>(
                    target.offset),
            record.value,
            target.size);
    }

    const auto apply_finished =
        std::chrono::steady_clock::now();

    const auto milliseconds =
        [](auto begin, auto end) noexcept {
            return std::chrono::duration<double, std::milli>{
                end - begin}
                .count();
        };

    decode_ms =
        milliseconds(
            decode_started,
            decode_finished);

    resolve_ms =
        milliseconds(
            resolve_started,
            resolve_finished);

    apply_ms =
        milliseconds(
            apply_started,
            apply_finished);

    return true;
}



enum class grouped_member_search_result : std::uint8_t {
    none = 0,
    found,
    ambiguous,
    invalid,
};

struct grouped_member_result final {
    runtime_offset offset = 0;
    type_ref type{};
};

struct grouped_resolve_state final {
    runtime_offset offset = 0;
    type_ref type{};
    bool constant = false;
};

struct grouped_resolve_stats final {
    std::uint64_t object_resolves = 0;
    std::uint64_t member_steps = 0;
    std::uint64_t reused_member_steps = 0;
};

[[nodiscard]] bool grouped_add_offset(
    runtime_offset left,
    runtime_offset right,
    runtime_offset& output) noexcept {

    if (left >
        (std::numeric_limits<runtime_offset>::max)() -
            right) {

        return false;
    }

    output = left + right;
    return true;
}

[[nodiscard]] bool grouped_strip_cv(
    const compiled_project_view& project,
    type_ref& type,
    bool& constant) noexcept {

    for (std::size_t step = 0;
         step <= project.derived_type_count();
         ++step) {

        if (type.kind() !=
            type_ref_kind::derived) {

            return true;
        }

        derived_type_record derived;

        if (!project.derived(
                type,
                derived)) {

            return false;
        }

        switch (derived.kind) {
        case derived_type_kind::const_qualified:
            constant = true;
            type = derived.child;
            continue;

        case derived_type_kind::volatile_qualified:
            type = derived.child;
            continue;

        case derived_type_kind::pointer:
        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference:
        case derived_type_kind::bounded_array:
        case derived_type_kind::unbounded_array:
            return true;
        }
    }

    return false;
}

[[nodiscard]] grouped_member_search_result
grouped_find_member_recursive(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    type_handle type,
    string_id name,
    std::size_t depth,
    grouped_member_result& output) noexcept {

    output = {};

    if (!type ||
        depth > project.type_count()) {

        return grouped_member_search_result::
            invalid;
    }

    type_entry entry;

    if (!project.type(
            type,
            entry) ||
        !entry.defined()) {

        return grouped_member_search_result::
            invalid;
    }

    if (const auto member =
            project.find_member(
                type,
                name);
        member) {

        member_record record;

        if (!project.member(
                type,
                member,
                record)) {

            return grouped_member_search_result::
                invalid;
        }

        const auto global =
            static_cast<std::size_t>(
                entry.members.begin) +
            member.value();

        record_offset member_offset = 0;

        if (!bindings.member_offset(
                global,
                member_offset)) {

            return grouped_member_search_result::
                invalid;
        }

        output.offset =
            static_cast<runtime_offset>(
                member_offset);

        output.type = record.type;

        return grouped_member_search_result::
            found;
    }

    grouped_member_search_result state =
        grouped_member_search_result::none;

    grouped_member_result selected;

    for (std::uint32_t local = 0;
         local < entry.bases.count;
         ++local) {

        const auto global =
            static_cast<std::size_t>(
                entry.bases.begin) +
            local;

        base_record base;
        record_offset base_offset = 0;

        if (!project.base_at(
                global,
                base) ||
            base.virtual_base() ||
            !bindings.base_offset(
                global,
                base_offset)) {

            return grouped_member_search_result::
                invalid;
        }

        grouped_member_result candidate;

        const auto found =
            grouped_find_member_recursive(
                project,
                bindings,
                base.type,
                name,
                depth + 1,
                candidate);

        if (found ==
                grouped_member_search_result::
                    invalid ||
            found ==
                grouped_member_search_result::
                    ambiguous) {

            return found;
        }

        if (found !=
            grouped_member_search_result::
                found) {

            continue;
        }

        runtime_offset combined = 0;

        if (!grouped_add_offset(
                static_cast<runtime_offset>(
                    base_offset),
                candidate.offset,
                combined)) {

            return grouped_member_search_result::
                invalid;
        }

        candidate.offset = combined;

        if (state ==
            grouped_member_search_result::
                found) {

            return grouped_member_search_result::
                ambiguous;
        }

        selected = candidate;
        state =
            grouped_member_search_result::
                found;
    }

    if (state ==
        grouped_member_search_result::found) {

        output = selected;
    }

    return state;
}

[[nodiscard]] bool grouped_same_components(
    std::span<const std::string_view> left,
    std::span<const std::string_view> right) noexcept {

    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0;
         index < left.size();
         ++index) {

        if (left[index] != right[index]) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::size_t grouped_common_prefix(
    std::span<const std::string_view> left,
    std::span<const std::string_view> right) noexcept {

    const auto count =
        (std::min)(
            left.size(),
            right.size());

    std::size_t index = 0;

    while (index < count &&
           left[index] == right[index]) {

        ++index;
    }

    return index;
}

[[nodiscard]] bool grouped_resolve_object(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::string_view> components,
    grouped_resolve_state& output) noexcept {

    output = {};

    if (components.empty()) {
        return false;
    }

    auto parent =
        project.identity_root();

    if (!parent) {
        return false;
    }

    object_handle object;

    for (std::size_t index = 0;
         index < components.size();
         ++index) {

        const auto name =
            project.find_string(
                components[index]);

        if (!name) {
            return false;
        }

        const auto last =
            index + 1 ==
            components.size();

        const auto identity =
            project.find_identity(
                parent,
                name,
                last
                    ? identity_kind::object
                    : identity_kind::
                        namespace_scope);

        if (!identity) {
            return false;
        }

        if (last) {
            object =
                project.find_object(
                    identity);

            if (!object) {
                return false;
            }
        } else {
            parent = identity;
        }
    }

    object_entry entry;

    if (!project.object(
            object,
            entry)) {

        return false;
    }

    runtime_offset offset = 0;

    if (!bindings.object_offset(
            object,
            offset)) {

        return false;
    }

    output.offset = offset;
    output.type = entry.type;
    output.constant = false;

    return true;
}

[[nodiscard]] bool profile_grouped_resolve(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    std::span<const std::string_view> components,
    std::span<const reset_profile_record> records,
    std::vector<reset_profile_target>& targets,
    grouped_resolve_stats& stats,
    double& resolve_ms,
    double& apply_ms) noexcept {

    targets.clear();
    stats = {};

    try {
        if (targets.capacity() <
            records.size()) {

            targets.reserve(
                records.size());
        }
    }
    catch (...) {
        return false;
    }

    std::span<const std::string_view>
        previous_object;

    std::span<const std::string_view>
        previous_members;

    grouped_resolve_state object_state;

    std::vector<grouped_resolve_state>
        prefix_states;

    try {
        prefix_states.reserve(16);
    }
    catch (...) {
        return false;
    }

    const auto resolve_started =
        std::chrono::steady_clock::now();

    for (std::size_t record_index = 0;
         record_index < records.size();
         ++record_index) {

        const auto& record =
            records[record_index];

        if (record.object_begin >
                components.size() ||
            record.object_count >
                components.size() -
                    record.object_begin ||
            record.member_begin >
                components.size() ||
            record.member_count >
                components.size() -
                    record.member_begin) {

            return false;
        }

        const std::span<const std::string_view>
            object{
                components.data() +
                    record.object_begin,
                record.object_count,
            };

        const std::span<const std::string_view>
            members{
                components.data() +
                    record.member_begin,
                record.member_count,
            };

        const auto same_object =
            !previous_object.empty() &&
            grouped_same_components(
                previous_object,
                object);

        if (!same_object) {
            if (!grouped_resolve_object(
                    project,
                    bindings,
                    object,
                    object_state)) {

                return false;
            }

            ++stats.object_resolves;

            prefix_states.clear();

            try {
                prefix_states.push_back(
                    object_state);
            }
            catch (...) {
                return false;
            }

            previous_members = {};
        }

        const auto common =
            same_object
            ? grouped_common_prefix(
                  previous_members,
                  members)
            : std::size_t{0};

        stats.reused_member_steps +=
            common;

        if (prefix_states.size() <
            common + 1) {

            return false;
        }

        prefix_states.resize(
            common + 1);

        for (std::size_t member_index = common;
             member_index < members.size();
             ++member_index) {

            auto state =
                prefix_states.back();

            if (!grouped_strip_cv(
                    project,
                    state.type,
                    state.constant) ||
                state.type.kind() !=
                    type_ref_kind::named ||
                state.type.payload() == 0) {

                return false;
            }

            const auto owner =
                project.type_at(
                    static_cast<std::size_t>(
                        state.type.payload() - 1));

            const auto name =
                project.find_string(
                    members[member_index]);

            if (!owner ||
                !name) {

                return false;
            }

            grouped_member_result member;

            const auto found =
                grouped_find_member_recursive(
                    project,
                    bindings,
                    owner,
                    name,
                    0,
                    member);

            if (found !=
                grouped_member_search_result::
                    found) {

                return false;
            }

            runtime_offset location = 0;

            if (!grouped_add_offset(
                    state.offset,
                    member.offset,
                    location) ||
                location >=
                    static_cast<runtime_offset>(
                        runtime.size())) {

                return false;
            }

            state.offset = location;
            state.type = member.type;

            try {
                prefix_states.push_back(
                    state);
            }
            catch (...) {
                return false;
            }

            ++stats.member_steps;
        }

        auto target_state =
            prefix_states.back();

        if (!grouped_strip_cv(
                project,
                target_state.type,
                target_state.constant) ||
            target_state.type.kind() !=
                type_ref_kind::intrinsic) {

            return false;
        }

        const auto intrinsic =
            static_cast<intrinsic_type>(
                target_state.type.payload());

        std::uint8_t size = 0;

        if (!bindings.intrinsic_size(
                intrinsic,
                size) ||
            size == 0 ||
            size != record.size ||
            intrinsic != record.type ||
            target_state.offset >
                static_cast<runtime_offset>(
                    runtime.size()) ||
            size >
                static_cast<runtime_offset>(
                    runtime.size()) -
                    target_state.offset) {

            return false;
        }

        if (record_index >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        try {
            targets.push_back(
                {
                    target_state.offset,
                    static_cast<std::uint32_t>(
                        record_index),
                    size,
                    {},
                });
        }
        catch (...) {
            return false;
        }

        previous_object = object;
        previous_members = members;
    }

    const auto resolve_finished =
        std::chrono::steady_clock::now();

    const auto apply_started =
        resolve_finished;

    for (const auto& target :
         targets) {

        const auto& record =
            records[target.record];

        std::memcpy(
            runtime.data() +
                static_cast<std::size_t>(
                    target.offset),
            record.value,
            target.size);
    }

    const auto apply_finished =
        std::chrono::steady_clock::now();

    const auto milliseconds =
        [](auto begin, auto end) noexcept {
            return std::chrono::duration<double, std::milli>{
                end - begin}
                .count();
        };

    resolve_ms =
        milliseconds(
            resolve_started,
            resolve_finished);

    apply_ms =
        milliseconds(
            apply_started,
            apply_finished);

    return true;
}



struct object_resolve_profile_group final {
    std::uint32_t component_begin = 0;
    std::uint16_t component_count = 0;
    std::uint16_t reserved = 0;
};

struct object_resolve_profile_stats final {
    std::uint64_t groups = 0;
    std::uint64_t components = 0;
};

[[nodiscard]] bool build_object_resolve_profile_groups(
    std::span<const std::string_view> components,
    std::span<const reset_profile_record> records,
    std::vector<object_resolve_profile_group>& groups,
    object_resolve_profile_stats& stats) noexcept {

    groups.clear();
    stats = {};

    std::span<const std::string_view>
        previous;

    try {
        groups.reserve(
            records.size());
    }
    catch (...) {
        return false;
    }

    for (const auto& record :
         records) {

        if (record.object_begin >
                components.size() ||
            record.object_count == 0 ||
            record.object_count >
                components.size() -
                    record.object_begin) {

            return false;
        }

        const std::span<const std::string_view>
            object{
                components.data() +
                    record.object_begin,
                record.object_count,
            };

        if (!previous.empty() &&
            grouped_same_components(
                previous,
                object)) {

            continue;
        }

        try {
            groups.push_back(
                {
                    record.object_begin,
                    record.object_count,
                    0,
                });
        }
        catch (...) {
            return false;
        }

        stats.components +=
            record.object_count;

        previous = object;
    }

    stats.groups =
        groups.size();

    return !groups.empty();
}

[[nodiscard]] bool profile_object_resolve_phases(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::string_view> components,
    std::span<const object_resolve_profile_group> groups,
    std::vector<string_id>& names,
    std::vector<identity_ref>& identities,
    std::vector<object_handle>& objects,
    std::vector<object_entry>& object_records,
    std::vector<runtime_offset>& offsets,
    double& find_string_ms,
    double& find_identity_ms,
    double& find_object_ms,
    double& object_record_ms,
    double& binding_ms) noexcept {

    names.clear();
    identities.clear();
    objects.clear();
    object_records.clear();
    offsets.clear();

    std::size_t name_count = 0;

    for (const auto& group :
         groups) {

        if (group.component_begin >
                components.size() ||
            group.component_count >
                components.size() -
                    group.component_begin) {

            return false;
        }

        if (name_count >
            (std::numeric_limits<std::size_t>::max)() -
                group.component_count) {

            return false;
        }

        name_count +=
            group.component_count;
    }

    try {
        names.resize(
            name_count);

        identities.resize(
            groups.size());

        objects.resize(
            groups.size());

        object_records.resize(
            groups.size());

        offsets.resize(
            groups.size());
    }
    catch (...) {
        return false;
    }

    const auto milliseconds =
        [](auto begin, auto end) noexcept {
            return std::chrono::duration<double, std::milli>{
                end - begin}
                .count();
        };

    const auto string_started =
        std::chrono::steady_clock::now();

    std::size_t name_index = 0;

    for (const auto& group :
         groups) {

        for (std::uint32_t local = 0;
             local < group.component_count;
             ++local) {

            const auto name =
                project.find_string(
                    components[
                        group.component_begin +
                        local]);

            if (!name) {
                return false;
            }

            names[name_index++] =
                name;
        }
    }

    const auto string_finished =
        std::chrono::steady_clock::now();

    const auto identity_started =
        string_finished;

    name_index = 0;

    for (std::size_t group_index = 0;
         group_index < groups.size();
         ++group_index) {

        const auto& group =
            groups[group_index];

        auto parent =
            project.identity_root();

        if (!parent) {
            return false;
        }

        identity_ref final_identity;

        for (std::uint32_t local = 0;
             local < group.component_count;
             ++local) {

            const auto last =
                local + 1 ==
                group.component_count;

            const auto identity =
                project.find_identity(
                    parent,
                    names[name_index++],
                    last
                        ? identity_kind::object
                        : identity_kind::
                            namespace_scope);

            if (!identity) {
                return false;
            }

            if (last) {
                final_identity =
                    identity;
            } else {
                parent =
                    identity;
            }
        }

        identities[group_index] =
            final_identity;
    }

    const auto identity_finished =
        std::chrono::steady_clock::now();

    const auto object_started =
        identity_finished;

    for (std::size_t index = 0;
         index < groups.size();
         ++index) {

        const auto object =
            project.find_object(
                identities[index]);

        if (!object) {
            return false;
        }

        objects[index] =
            object;
    }

    const auto object_finished =
        std::chrono::steady_clock::now();

    const auto record_started =
        object_finished;

    for (std::size_t index = 0;
         index < groups.size();
         ++index) {

        if (!project.object(
                objects[index],
                object_records[index])) {

            return false;
        }
    }

    const auto record_finished =
        std::chrono::steady_clock::now();

    const auto binding_started =
        record_finished;

    for (std::size_t index = 0;
         index < groups.size();
         ++index) {

        if (!bindings.object_offset(
                objects[index],
                offsets[index])) {

            return false;
        }
    }

    const auto binding_finished =
        std::chrono::steady_clock::now();

    find_string_ms =
        milliseconds(
            string_started,
            string_finished);

    find_identity_ms =
        milliseconds(
            identity_started,
            identity_finished);

    find_object_ms =
        milliseconds(
            object_started,
            object_finished);

    object_record_ms =
        milliseconds(
            record_started,
            record_finished);

    binding_ms =
        milliseconds(
            binding_started,
            binding_finished);

    return true;
}



struct translated_grouped_stats final {
    std::uint64_t object_resolves = 0;
    std::uint64_t member_steps = 0;
    std::uint64_t reused_member_steps = 0;
};

[[nodiscard]] bool build_ic_string_translation(
    const compiled_project_view& project,
    const runtime_ic_binary_view& image,
    std::vector<string_id>& translation) noexcept {

    translation.clear();

    try {
        translation.resize(
            image.string_count() + 1);
    }
    catch (...) {
        return false;
    }

    for (std::size_t id = 1;
         id <= image.string_count();
         ++id) {

        const auto text =
            image.string(
                static_cast<std::uint32_t>(
                    id));

        if (text.empty()) {
            return false;
        }

        const auto translated =
            project.find_string(
                text);

        if (!translated) {
            return false;
        }

        translation[id] =
            translated;
    }

    return true;
}

[[nodiscard]] bool same_ic_component_range(
    const runtime_ic_binary_view& image,
    std::uint32_t left_begin,
    std::uint16_t left_count,
    std::uint32_t right_begin,
    std::uint16_t right_count) noexcept {

    if (left_count != right_count) {
        return false;
    }

    for (std::uint32_t index = 0;
         index < left_count;
         ++index) {

        if (image.component(
                left_begin + index) !=
            image.component(
                right_begin + index)) {

            return false;
        }
    }

    return true;
}

[[nodiscard]] std::size_t common_ic_component_prefix(
    const runtime_ic_binary_view& image,
    std::uint32_t left_begin,
    std::uint16_t left_count,
    std::uint32_t right_begin,
    std::uint16_t right_count) noexcept {

    const auto count =
        (std::min)(
            static_cast<std::size_t>(
                left_count),
            static_cast<std::size_t>(
                right_count));

    std::size_t index = 0;

    while (index < count &&
           image.component(
               left_begin +
               static_cast<std::uint32_t>(
                   index)) ==
           image.component(
               right_begin +
               static_cast<std::uint32_t>(
                   index))) {

        ++index;
    }

    return index;
}

[[nodiscard]] bool translated_resolve_object(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    const runtime_ic_binary_view& image,
    std::span<const string_id> translation,
    std::uint32_t component_begin,
    std::uint16_t component_count,
    grouped_resolve_state& output) noexcept {

    output = {};

    if (component_count == 0) {
        return false;
    }

    auto parent =
        project.identity_root();

    if (!parent) {
        return false;
    }

    object_handle object;

    for (std::uint32_t index = 0;
         index < component_count;
         ++index) {

        const auto local_id =
            image.component(
                component_begin +
                index);

        if (local_id == 0 ||
            local_id >=
                translation.size()) {

            return false;
        }

        const auto name =
            translation[local_id];

        if (!name) {
            return false;
        }

        const auto last =
            index + 1 ==
            component_count;

        const auto identity =
            project.find_identity(
                parent,
                name,
                last
                    ? identity_kind::object
                    : identity_kind::
                        namespace_scope);

        if (!identity) {
            return false;
        }

        if (last) {
            object =
                project.find_object(
                    identity);

            if (!object) {
                return false;
            }
        } else {
            parent = identity;
        }
    }

    object_entry entry;
    runtime_offset offset = 0;

    if (!project.object(
            object,
            entry) ||
        !bindings.object_offset(
            object,
            offset)) {

        return false;
    }

    output.offset = offset;
    output.type = entry.type;
    output.constant = false;

    return true;
}

[[nodiscard]] bool profile_translated_grouped_resolve(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    const runtime_ic_binary_view& image,
    std::span<const string_id> translation,
    std::vector<reset_profile_target>& targets,
    translated_grouped_stats& stats,
    double& resolve_ms,
    double& apply_ms) noexcept {

    targets.clear();
    stats = {};

    try {
        if (targets.capacity() <
            image.record_count()) {

            targets.reserve(
                image.record_count());
        }
    }
    catch (...) {
        return false;
    }

    runtime_ic_binary_record_view
        previous_record;

    bool have_previous = false;

    grouped_resolve_state object_state;

    std::vector<grouped_resolve_state>
        prefix_states;

    try {
        prefix_states.reserve(16);
    }
    catch (...) {
        return false;
    }

    const auto resolve_started =
        std::chrono::steady_clock::now();

    for (std::size_t record_index = 0;
         record_index < image.record_count();
         ++record_index) {

        runtime_ic_binary_record_view record;

        if (!image.record(
                record_index,
                record) ||
            record.object_count == 0 ||
            record.value.empty() ||
            record.value.size() >
                runtime_ic_scalar_value{}.
                    bytes.size()) {

            return false;
        }

        const auto same_object =
            have_previous &&
            same_ic_component_range(
                image,
                previous_record.object_begin,
                previous_record.object_count,
                record.object_begin,
                record.object_count);

        if (!same_object) {
            if (!translated_resolve_object(
                    project,
                    bindings,
                    image,
                    translation,
                    record.object_begin,
                    record.object_count,
                    object_state)) {

                return false;
            }

            ++stats.object_resolves;

            prefix_states.clear();

            try {
                prefix_states.push_back(
                    object_state);
            }
            catch (...) {
                return false;
            }
        }

        const auto common =
            same_object
            ? common_ic_component_prefix(
                  image,
                  previous_record.member_begin,
                  previous_record.member_count,
                  record.member_begin,
                  record.member_count)
            : std::size_t{0};

        stats.reused_member_steps +=
            common;

        if (prefix_states.size() <
            common + 1) {

            return false;
        }

        prefix_states.resize(
            common + 1);

        for (std::size_t member_index = common;
             member_index <
                 record.member_count;
             ++member_index) {

            auto state =
                prefix_states.back();

            if (!grouped_strip_cv(
                    project,
                    state.type,
                    state.constant) ||
                state.type.kind() !=
                    type_ref_kind::named ||
                state.type.payload() == 0) {

                return false;
            }

            const auto owner =
                project.type_at(
                    static_cast<std::size_t>(
                        state.type.payload() - 1));

            const auto local_id =
                image.component(
                    record.member_begin +
                    static_cast<std::uint32_t>(
                        member_index));

            if (!owner ||
                local_id == 0 ||
                local_id >=
                    translation.size()) {

                return false;
            }

            const auto name =
                translation[local_id];

            if (!name) {
                return false;
            }

            grouped_member_result member;

            const auto found =
                grouped_find_member_recursive(
                    project,
                    bindings,
                    owner,
                    name,
                    0,
                    member);

            if (found !=
                grouped_member_search_result::
                    found) {

                return false;
            }

            runtime_offset location = 0;

            if (!grouped_add_offset(
                    state.offset,
                    member.offset,
                    location) ||
                location >=
                    static_cast<runtime_offset>(
                        runtime.size())) {

                return false;
            }

            state.offset = location;
            state.type = member.type;

            try {
                prefix_states.push_back(
                    state);
            }
            catch (...) {
                return false;
            }

            ++stats.member_steps;
        }

        auto target_state =
            prefix_states.back();

        if (!grouped_strip_cv(
                project,
                target_state.type,
                target_state.constant) ||
            target_state.type.kind() !=
                type_ref_kind::intrinsic) {

            return false;
        }

        const auto intrinsic =
            static_cast<intrinsic_type>(
                target_state.type.payload());

        std::uint8_t size = 0;

        if (!bindings.intrinsic_size(
                intrinsic,
                size) ||
            size == 0 ||
            size !=
                record.value.size() ||
            intrinsic !=
                record.type ||
            target_state.offset >
                static_cast<runtime_offset>(
                    runtime.size()) ||
            size >
                static_cast<runtime_offset>(
                    runtime.size()) -
                    target_state.offset) {

            return false;
        }

        if (record_index >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        try {
            targets.push_back(
                {
                    target_state.offset,
                    static_cast<std::uint32_t>(
                        record_index),
                    size,
                    {},
                });
        }
        catch (...) {
            return false;
        }

        previous_record = record;
        have_previous = true;
    }

    const auto resolve_finished =
        std::chrono::steady_clock::now();

    const auto apply_started =
        resolve_finished;

    for (const auto& target :
         targets) {

        runtime_ic_binary_record_view record;

        if (!image.record(
                target.record,
                record) ||
            record.value.size() !=
                target.size) {

            return false;
        }

        std::memcpy(
            runtime.data() +
                static_cast<std::size_t>(
                    target.offset),
            record.value.data(),
            target.size);
    }

    const auto apply_finished =
        std::chrono::steady_clock::now();

    const auto milliseconds =
        [](auto begin, auto end) noexcept {
            return std::chrono::duration<double, std::milli>{
                end - begin}
                .count();
        };

    resolve_ms =
        milliseconds(
            resolve_started,
            resolve_finished);

    apply_ms =
        milliseconds(
            apply_started,
            apply_finished);

    return true;
}



struct direct_snap_string_slot final {
    std::uint32_t hash = 0;
    std::uint32_t id = 0;
};

struct direct_snap_plan final {
    std::vector<std::string_view> strings;
    std::vector<direct_snap_string_slot> index;
    std::uint64_t string_bytes = 0;
    std::uint64_t value_bytes = 0;
    std::uint32_t component_count = 0;
    std::size_t image_size = 0;
};

[[nodiscard]] std::uint32_t direct_snap_hash(
    std::string_view value) noexcept {

    std::uint32_t hash = 2166136261u;

    for (const auto character :
         value) {

        hash ^=
            static_cast<std::uint8_t>(
                character);

        hash *= 16777619u;
    }

    return hash == 0
        ? 1u
        : hash;
}

[[nodiscard]] std::size_t direct_snap_next_power_of_two(
    std::size_t value) noexcept {

    if (value <= 8) {
        return 8;
    }

    --value;

    for (std::size_t shift = 1;
         shift < sizeof(value) * 8;
         shift <<= 1) {

        value |= value >> shift;
    }

    if (value ==
        (std::numeric_limits<std::size_t>::max)()) {

        return 0;
    }

    return value + 1;
}

[[nodiscard]] std::uint64_t direct_snap_align8(
    std::uint64_t value) noexcept {

    return (value + 7u) &
        ~std::uint64_t{7u};
}

[[nodiscard]] bool direct_snap_add_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    if (left >
        (std::numeric_limits<std::uint64_t>::max)() -
            right) {

        return false;
    }

    output = left + right;
    return true;
}

[[nodiscard]] bool direct_snap_mul_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {

    if (left != 0 &&
        right >
            (std::numeric_limits<std::uint64_t>::max)() /
                left) {

        return false;
    }

    output = left * right;
    return true;
}

[[nodiscard]] std::uint32_t direct_snap_find_string(
    const direct_snap_plan& plan,
    std::string_view value) noexcept {

    if (plan.index.empty()) {
        return 0;
    }

    const auto hash =
        direct_snap_hash(
            value);

    const auto mask =
        plan.index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    for (std::size_t probe = 0;
         probe < plan.index.size();
         ++probe) {

        const auto& slot =
            plan.index[position];

        if (slot.id == 0) {
            return 0;
        }

        if (slot.hash == hash &&
            slot.id <= plan.strings.size() &&
            plan.strings[
                slot.id - 1] == value) {

            return slot.id;
        }

        position =
            (position + 1) &
            mask;
    }

    return 0;
}

[[nodiscard]] bool direct_snap_append_string(
    direct_snap_plan& plan,
    std::string_view value) noexcept {

    if (value.empty()) {
        return false;
    }

    if (direct_snap_find_string(
            plan,
            value) != 0) {

        return true;
    }

    if (plan.strings.size() >=
        (std::numeric_limits<std::uint32_t>::max)()) {

        return false;
    }

    const auto next_bytes =
        plan.string_bytes +
        value.size();

    if (next_bytes >
        (std::numeric_limits<std::uint32_t>::max)()) {

        return false;
    }

    const auto id =
        static_cast<std::uint32_t>(
            plan.strings.size() + 1);

    try {
        plan.strings.push_back(
            value);
    }
    catch (...) {
        return false;
    }

    const auto hash =
        direct_snap_hash(
            value);

    const auto mask =
        plan.index.size() - 1;

    auto position =
        static_cast<std::size_t>(
            hash) &
        mask;

    for (std::size_t probe = 0;
         probe < plan.index.size();
         ++probe) {

        auto& slot =
            plan.index[position];

        if (slot.id == 0) {
            slot.hash = hash;
            slot.id = id;
            plan.string_bytes =
                next_bytes;
            return true;
        }

        position =
            (position + 1) &
            mask;
    }

    return false;
}

[[nodiscard]] bool prepare_direct_snap_plan(
    std::span<const runtime_ic_record_source> records,
    direct_snap_plan& plan) noexcept {

    plan = {};

    if (records.empty() ||
        records.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

        return false;
    }

    std::uint64_t component_count = 0;
    std::uint64_t value_bytes = 0;

    for (const auto& record :
         records) {

        if (record.path.object.empty() ||
            record.value.type ==
                intrinsic_type::none ||
            record.value.size == 0 ||
            record.value.size >
                record.value.bytes.size()) {

            return false;
        }

        component_count +=
            record.path.object.size();

        component_count +=
            record.path.members.size();

        value_bytes +=
            record.value.size;

        if (component_count >
                (std::numeric_limits<std::uint32_t>::max)() ||
            value_bytes >
                (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }
    }

    const auto desired_slots =
        component_count >
            (std::numeric_limits<std::size_t>::max)() / 2
        ? std::size_t{0}
        : direct_snap_next_power_of_two(
            static_cast<std::size_t>(
                component_count) * 2);

    if (desired_slots == 0) {
        return false;
    }

    try {
        plan.index.assign(
            desired_slots,
            {});

        plan.strings.reserve(
            static_cast<std::size_t>(
                component_count));
    }
    catch (...) {
        return false;
    }

    for (const auto& record :
         records) {

        for (const auto component :
             record.path.object) {

            if (!direct_snap_append_string(
                    plan,
                    component)) {

                return false;
            }
        }

        for (const auto component :
             record.path.members) {

            if (!direct_snap_append_string(
                    plan,
                    component)) {

                return false;
            }
        }
    }

    std::uint64_t string_record_bytes = 0;
    std::uint64_t component_bytes = 0;
    std::uint64_t record_bytes = 0;

    if (!direct_snap_mul_u64(
            plan.strings.size(),
            runtime_ic_binary_string_record_size,
            string_record_bytes) ||
        !direct_snap_mul_u64(
            component_count,
            sizeof(std::uint32_t),
            component_bytes) ||
        !direct_snap_mul_u64(
            records.size(),
            runtime_ic_binary_record_size,
            record_bytes)) {

        return false;
    }

    const auto components_offset =
        direct_snap_align8(
            runtime_ic_binary_header_size +
            string_record_bytes);

    const auto records_offset =
        direct_snap_align8(
            components_offset +
            component_bytes);

    const auto string_bytes_offset =
        direct_snap_align8(
            records_offset +
            record_bytes);

    const auto values_offset =
        direct_snap_align8(
            string_bytes_offset +
            plan.string_bytes);

    std::uint64_t total = 0;

    if (!direct_snap_add_u64(
            values_offset,
            value_bytes,
            total) ||
        total >
            (std::numeric_limits<std::size_t>::max)()) {

        return false;
    }

    plan.component_count =
        static_cast<std::uint32_t>(
            component_count);

    plan.value_bytes =
        value_bytes;

    plan.image_size =
        static_cast<std::size_t>(
            total);

    return true;
}

[[nodiscard]] bool encode_direct_snap_components(
    std::span<const runtime_ic_record_source> records,
    const direct_snap_plan& plan,
    std::vector<std::uint32_t>& ids,
    std::vector<std::uint32_t>& record_component_begin) noexcept {

    ids.clear();
    record_component_begin.clear();

    try {
        ids.reserve(
            plan.component_count);

        record_component_begin.reserve(
            records.size() * 2);
    }
    catch (...) {
        return false;
    }

    for (const auto& record :
         records) {

        if (ids.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        record_component_begin.push_back(
            static_cast<std::uint32_t>(
                ids.size()));

        for (const auto component :
             record.path.object) {

            const auto id =
                direct_snap_find_string(
                    plan,
                    component);

            if (id == 0) {
                return false;
            }

            ids.push_back(
                id);
        }

        if (ids.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        record_component_begin.push_back(
            static_cast<std::uint32_t>(
                ids.size()));

        for (const auto component :
             record.path.members) {

            const auto id =
                direct_snap_find_string(
                    plan,
                    component);

            if (id == 0) {
                return false;
            }

            ids.push_back(
                id);
        }
    }

    return ids.size() ==
        plan.component_count;
}



struct string_id_snap_record final {
    std::uint32_t object_begin = 0;
    std::uint16_t object_count = 0;
    std::uint16_t member_count = 0;
    std::uint32_t member_begin = 0;
};

struct string_id_snap_source final {
    std::vector<string_id> components;
    std::vector<string_id_snap_record> records;
};

struct string_id_snap_plan final {
    std::vector<std::uint32_t> g_to_ic;
    std::vector<string_id> strings;
    std::uint64_t string_bytes = 0;
    std::uint32_t component_count = 0;
};

[[nodiscard]] bool build_string_id_snap_source(
    const compiled_project_view& project,
    std::span<const runtime_ic_record_source> records,
    string_id_snap_source& output) noexcept {

    output.components.clear();
    output.records.clear();

    std::size_t component_count = 0;

    for (const auto& record :
         records) {

        if (record.path.object.empty()) {
            return false;
        }

        if (component_count >
            (std::numeric_limits<std::size_t>::max)() -
                record.path.object.size() -
                record.path.members.size()) {

            return false;
        }

        component_count +=
            record.path.object.size() +
            record.path.members.size();
    }

    try {
        output.components.reserve(
            component_count);

        output.records.reserve(
            records.size());
    }
    catch (...) {
        return false;
    }

    for (const auto& record :
         records) {

        if (output.components.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        const auto object_begin =
            static_cast<std::uint32_t>(
                output.components.size());

        for (const auto text :
             record.path.object) {

            const auto id =
                project.find_string(
                    text);

            if (!id) {
                return false;
            }

            output.components.push_back(
                id);
        }

        if (output.components.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        const auto member_begin =
            static_cast<std::uint32_t>(
                output.components.size());

        for (const auto text :
             record.path.members) {

            const auto id =
                project.find_string(
                    text);

            if (!id) {
                return false;
            }

            output.components.push_back(
                id);
        }

        if (record.path.object.size() >
                (std::numeric_limits<std::uint16_t>::max)() ||
            record.path.members.size() >
                (std::numeric_limits<std::uint16_t>::max)()) {

            return false;
        }

        output.records.push_back(
            {
                object_begin,
                static_cast<std::uint16_t>(
                    record.path.object.size()),
                static_cast<std::uint16_t>(
                    record.path.members.size()),
                member_begin,
            });
    }

    return true;
}

[[nodiscard]] bool prepare_string_id_snap_plan(
    const compiled_project_view& project,
    const string_id_snap_source& source,
    string_id_snap_plan& plan) noexcept {

    plan.g_to_ic.clear();
    plan.strings.clear();
    plan.string_bytes = 0;
    plan.component_count = 0;

    try {
        plan.g_to_ic.resize(
            project.string_count() + 1,
            0);

        plan.strings.reserve(
            source.components.size());
    }
    catch (...) {
        return false;
    }

    for (const auto id :
         source.components) {

        const auto slot =
            id.value();

        if (slot == 0 ||
            slot >=
                plan.g_to_ic.size()) {

            return false;
        }

        if (plan.g_to_ic[slot] != 0) {
            continue;
        }

        if (plan.strings.size() >=
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        const auto text =
            project.string(
                id);

        if (text.empty()) {
            return false;
        }

        const auto next_bytes =
            plan.string_bytes +
            text.size();

        if (next_bytes >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        const auto local =
            static_cast<std::uint32_t>(
                plan.strings.size() + 1);

        plan.g_to_ic[slot] =
            local;

        plan.strings.push_back(
            id);

        plan.string_bytes =
            next_bytes;
    }

    if (source.components.size() >
        (std::numeric_limits<std::uint32_t>::max)()) {

        return false;
    }

    plan.component_count =
        static_cast<std::uint32_t>(
            source.components.size());

    return true;
}

[[nodiscard]] bool emit_string_id_snap_components(
    const string_id_snap_source& source,
    const string_id_snap_plan& plan,
    std::vector<std::uint32_t>& output) noexcept {

    output.clear();

    try {
        output.resize(
            source.components.size());
    }
    catch (...) {
        return false;
    }

    for (std::size_t index = 0;
         index < source.components.size();
         ++index) {

        const auto slot =
            source.components[index].value();

        if (slot == 0 ||
            slot >=
                plan.g_to_ic.size()) {

            return false;
        }

        const auto local =
            plan.g_to_ic[slot];

        if (local == 0) {
            return false;
        }

        output[index] =
            local;
    }

    return true;
}


struct compact_reset_entry final {
    runtime_offset offset = 0;
    std::uint32_t record = 0;
    std::uint8_t size = 0;
    std::uint8_t reserved[3]{};
};

static_assert(sizeof(compact_reset_entry) == 16);

[[nodiscard]] bool profile_binary_path(
    const runtime_ic_binary_view& image,
    const runtime_ic_binary_record_view& record,
    std::vector<std::string_view>& object,
    std::vector<std::string_view>& members) noexcept {

    object.clear();
    members.clear();

    try {
        if (object.capacity() <
            record.object_count) {

            object.reserve(
                record.object_count);
        }

        if (members.capacity() <
            record.member_count) {

            members.reserve(
                record.member_count);
        }

        for (std::uint32_t index = 0;
             index < record.object_count;
             ++index) {

            const auto id =
                image.component(
                    record.object_begin +
                    index);

            const auto component =
                image.string(id);

            if (id == 0 ||
                component.empty()) {

                return false;
            }

            object.push_back(
                component);
        }

        for (std::uint32_t index = 0;
             index < record.member_count;
             ++index) {

            const auto id =
                image.component(
                    record.member_begin +
                    index);

            const auto component =
                image.string(id);

            if (id == 0 ||
                component.empty()) {

                return false;
            }

            members.push_back(
                component);
        }
    }
    catch (...) {
        return false;
    }

    return true;
}

[[nodiscard]] bool profile_compact_reset(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    const runtime_ic_binary_view& image,
    std::vector<compact_reset_entry>& plan,
    double& plan_ms,
    double& apply_ms) noexcept {

    plan.clear();

    try {
        if (plan.capacity() <
            image.record_count()) {

            plan.reserve(
                image.record_count());
        }
    }
    catch (...) {
        return false;
    }

    std::vector<std::string_view> object;
    std::vector<std::string_view> members;

    const auto plan_started =
        std::chrono::steady_clock::now();

    for (std::size_t index = 0;
         index < image.record_count();
         ++index) {

        runtime_ic_binary_record_view record;

        if (!image.record(
                index,
                record) ||
            !profile_binary_path(
                image,
                record,
                object,
                members) ||
            record.value.empty() ||
            record.value.size() >
                runtime_ic_scalar_value{}.
                    bytes.size()) {

            return false;
        }

        runtime_ic_scalar_target target;

        if (resolve_runtime_ic_scalar(
                project,
                bindings,
                static_cast<std::uint64_t>(
                    runtime.size()),
                {
                    object,
                    members,
                },
                target) !=
            runtime_ic_result::success) {

            return false;
        }

        if (target.type !=
                record.type ||
            target.size !=
                record.value.size()) {

            return false;
        }

        if (index >
            (std::numeric_limits<std::uint32_t>::max)()) {

            return false;
        }

        try {
            plan.push_back(
                {
                    target.offset,
                    static_cast<std::uint32_t>(
                        index),
                    target.size,
                    {},
                });
        }
        catch (...) {
            return false;
        }
    }

    const auto plan_finished =
        std::chrono::steady_clock::now();

    const auto apply_started =
        plan_finished;

    for (const auto& entry :
         plan) {

        runtime_ic_binary_record_view record;

        if (!image.record(
                entry.record,
                record) ||
            record.value.size() !=
                entry.size) {

            return false;
        }

        std::memcpy(
            runtime.data() +
                static_cast<std::size_t>(
                    entry.offset),
            record.value.data(),
            entry.size);
    }

    const auto apply_finished =
        std::chrono::steady_clock::now();

    const auto milliseconds =
        [](auto begin, auto end) noexcept {
            return std::chrono::duration<double, std::milli>{
                end - begin}
                .count();
        };

    plan_ms =
        milliseconds(
            plan_started,
            plan_finished);

    apply_ms =
        milliseconds(
            apply_started,
            apply_finished);

    return true;
}


void usage() {
    std::cerr
        << "Usage:\n"
        << "  ServerEngineV4RuntimeBenchmark objects <count> [ic-iterations]\n"
        << "  ServerEngineV4RuntimeBenchmark links         <count> [ic-iterations]\n"
        << "  ServerEngineV4RuntimeBenchmark indexed_links <count> [ic-iterations]\n"
        << "  ServerEngineV4RuntimeBenchmark chain         <depth> [ic-iterations]\n"
        << "  ServerEngineV4RuntimeBenchmark many_types <type-count> [ic-iterations]\n";
}

}

int main(
    int argc,
    char* argv[]) {

    if (argc != 3 && argc != 4 && argc != 5) {

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

    std::size_t ic_iterations = 7;

    if (argc == 4 &&
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
        std::vector<double> samples;

        try {
            samples.reserve(
                ic_iterations);
        }
        catch (...) {
            std::cerr
                << "SNAP V2 standalone sample allocation failed\n";

            return 1;
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
                    << "Runtime IC SNAP V2 standalone failed\n";

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

        if (samples.empty()) {
            std::cerr
                << "Runtime IC SNAP V2 standalone has no samples\n";

            return 1;
        }

        auto ordered =
            samples;

        std::sort(
            ordered.begin(),
            ordered.end());

        const auto middle =
            ordered.size() / 2;

        const auto snap_v2_only_ms =
            ordered.size() % 2 != 0
            ? ordered[middle]
            : (ordered[middle - 1] +
               ordered[middle]) /
                2.0;

        runtime_ic_binary_view view;

        if (view.bind(
                image) !=
            runtime_ic_codec_result::success) {

            std::cerr
                << "Runtime IC SNAP V2 standalone image bind failed\n";

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
                << "Runtime IC SNAP V2 standalone RESET failed\n";

            return 1;
        }

        const auto peak_after =
            peak_working_set_bytes();

        const auto peak_delta =
            peak_after > peak_before
            ? peak_after - peak_before
            : std::uint64_t{0};

        const auto records_per_second =
            snap_v2_only_ms > 0.0
            ? static_cast<double>(
                  snapshot_stats.scalars) *
                  1000.0 /
                  snap_v2_only_ms
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
            << ",snap_v2_only_ms="
            << snap_v2_only_ms
            << ",snap_v2_only_records_per_s="
            << records_per_second
            << ",snap_v2_only_reset_ok=1"
            << ",peak_ws_before_snap_bytes="
            << peak_before
            << ",peak_ws_after_snap_bytes="
            << peak_after
            << ",peak_ws_delta_bytes="
            << peak_delta
            << '\n';

        return 0;
    }

    runtime_ic_snapshot warm_snapshot;

    if (snapshot_runtime_ic_project(
            project,
            runtime_bindings,
            runtime_bytes,
            warm_snapshot) !=
        runtime_ic_snapshot_result::success) {

        std::cerr
            << "Runtime IC SNAP warmup failed\n";

        return 1;
    }

    runtime_ic_binary_plan warm_plan;

    if (prepare_runtime_ic_binary(
            warm_snapshot.records(),
            warm_plan) !=
        runtime_ic_codec_result::success) {

        std::cerr
            << "Runtime IC binary prepare warmup failed\n";

        return 1;
    }

    std::vector<std::byte> warm_ic(
        warm_plan.size(),
        std::byte{0});

    if (encode_runtime_ic_binary(
            warm_snapshot.records(),
            warm_plan,
            warm_ic) !=
        runtime_ic_codec_result::success) {

        std::cerr
            << "Runtime IC binary encode warmup failed\n";

        return 1;
    }

    runtime_ic_binary_view warm_view;

    if (warm_view.bind(
            warm_ic) !=
        runtime_ic_codec_result::success) {

        std::cerr
            << "Runtime IC binary bind warmup failed\n";

        return 1;
    }

    if (reset_runtime_ic_binary(
            project,
            runtime_bindings,
            runtime_bytes,
            warm_view) !=
        runtime_ic_reset_result::success) {

        std::cerr
            << "Runtime IC RESET warmup failed\n";

        return 1;
    }

    std::vector<double> snap_traverse_samples;
    std::vector<double> snap_prepare_samples;
    std::vector<double> snap_allocate_samples;
    std::vector<double> snap_encode_samples;
    std::vector<double> snap_total_samples;
    std::vector<double> snap_v2_traverse_samples;
    std::vector<double> snap_v2_prepare_samples;
    std::vector<double> snap_v2_allocate_samples;
    std::vector<double> snap_v2_encode_samples;
    std::vector<double> snap_v2_total_samples;
    std::vector<double> snap_direct_plan_samples;
    std::vector<double> snap_direct_components_samples;
    std::vector<double> snap_direct_total_samples;
    std::vector<double> snap_string_id_plan_samples;
    std::vector<double> snap_string_id_components_samples;
    std::vector<double> snap_string_id_total_samples;
    std::vector<double> reset_samples;
    std::vector<double> reset_decode_samples;
    std::vector<double> reset_resolve_samples;
    std::vector<double> reset_profile_apply_samples;
    std::vector<double> reset_profile_total_samples;
    std::vector<double> reset_grouped_resolve_samples;
    std::vector<double> reset_grouped_apply_samples;
    std::vector<double> reset_grouped_total_samples;
    std::vector<double> object_find_string_samples;
    std::vector<double> object_find_identity_samples;
    std::vector<double> object_find_object_samples;
    std::vector<double> object_record_samples;
    std::vector<double> object_binding_samples;
    std::vector<double> object_resolve_profile_samples;
    std::vector<double> string_translation_samples;
    std::vector<double> translated_grouped_resolve_samples;
    std::vector<double> translated_grouped_apply_samples;
    std::vector<double> translated_grouped_total_samples;
    std::vector<double> reset_compact_plan_samples;
    std::vector<double> reset_compact_apply_samples;
    std::vector<double> reset_compact_total_samples;

    try {
        snap_traverse_samples.reserve(ic_iterations);
        snap_prepare_samples.reserve(ic_iterations);
        snap_allocate_samples.reserve(ic_iterations);
        snap_encode_samples.reserve(ic_iterations);
        snap_total_samples.reserve(ic_iterations);
        snap_v2_traverse_samples.reserve(ic_iterations);
        snap_v2_prepare_samples.reserve(ic_iterations);
        snap_v2_allocate_samples.reserve(ic_iterations);
        snap_v2_encode_samples.reserve(ic_iterations);
        snap_v2_total_samples.reserve(ic_iterations);
        snap_direct_plan_samples.reserve(ic_iterations);
        snap_direct_components_samples.reserve(ic_iterations);
        snap_direct_total_samples.reserve(ic_iterations);
        snap_string_id_plan_samples.reserve(ic_iterations);
        snap_string_id_components_samples.reserve(ic_iterations);
        snap_string_id_total_samples.reserve(ic_iterations);
        reset_samples.reserve(ic_iterations);
        reset_decode_samples.reserve(ic_iterations);
        reset_resolve_samples.reserve(ic_iterations);
        reset_profile_apply_samples.reserve(ic_iterations);
        reset_profile_total_samples.reserve(ic_iterations);
        reset_grouped_resolve_samples.reserve(ic_iterations);
        reset_grouped_apply_samples.reserve(ic_iterations);
        reset_grouped_total_samples.reserve(ic_iterations);
        object_find_string_samples.reserve(ic_iterations);
        object_find_identity_samples.reserve(ic_iterations);
        object_find_object_samples.reserve(ic_iterations);
        object_record_samples.reserve(ic_iterations);
        object_binding_samples.reserve(ic_iterations);
        object_resolve_profile_samples.reserve(ic_iterations);
        string_translation_samples.reserve(ic_iterations);
        translated_grouped_resolve_samples.reserve(ic_iterations);
        translated_grouped_apply_samples.reserve(ic_iterations);
        translated_grouped_total_samples.reserve(ic_iterations);
        reset_compact_plan_samples.reserve(ic_iterations);
        reset_compact_apply_samples.reserve(ic_iterations);
        reset_compact_total_samples.reserve(ic_iterations);
    }
    catch (...) {
        std::cerr
            << "Cannot allocate IC benchmark samples\n";

        return 1;
    }

    std::vector<std::byte> reset_image;
    std::size_t ic_record_count = 0;
    std::size_t ic_image_bytes = 0;
    std::uint64_t reset_value_bytes = 0;

    const auto elapsed_ms =
        [](auto begin, auto end) noexcept {
            return std::chrono::duration<double, std::milli>{
                end - begin}
                .count();
        };

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        const auto total_started =
            std::chrono::steady_clock::now();

        const auto traverse_started =
            total_started;

        runtime_ic_snapshot snapshot;

        if (snapshot_runtime_ic_project(
                project,
                runtime_bindings,
                runtime_bytes,
                snapshot) !=
            runtime_ic_snapshot_result::success) {

            std::cerr
                << "Runtime IC SNAP traversal failed\n";

            return 1;
        }

        const auto traverse_finished =
            std::chrono::steady_clock::now();

        runtime_ic_binary_plan plan;

        if (prepare_runtime_ic_binary(
                snapshot.records(),
                plan) !=
            runtime_ic_codec_result::success) {

            std::cerr
                << "Runtime IC binary prepare failed\n";

            return 1;
        }

        const auto prepare_finished =
            std::chrono::steady_clock::now();

        const auto allocation_started =
            prepare_finished;

        std::vector<std::byte> image(
            plan.size(),
            std::byte{0});

        const auto allocation_finished =
            std::chrono::steady_clock::now();

        if (encode_runtime_ic_binary(
                snapshot.records(),
                plan,
                image) !=
            runtime_ic_codec_result::success) {

            std::cerr
                << "Runtime IC binary encode failed\n";

            return 1;
        }

        const auto encode_finished =
            std::chrono::steady_clock::now();

        snap_traverse_samples.push_back(
            elapsed_ms(
                traverse_started,
                traverse_finished));

        snap_prepare_samples.push_back(
            elapsed_ms(
                traverse_finished,
                prepare_finished));

        snap_allocate_samples.push_back(
            elapsed_ms(
                allocation_started,
                allocation_finished));

        snap_encode_samples.push_back(
            elapsed_ms(
                allocation_finished,
                encode_finished));

        snap_total_samples.push_back(
            elapsed_ms(
                total_started,
                encode_finished));

        ic_record_count =
            snapshot.records().size();

        ic_image_bytes =
            image.size();

        if (iteration + 1 ==
            ic_iterations) {

            reset_image =
                std::move(image);
        }
    }

    string_id_snap_source string_id_source;

    // Untimed conversion only models the future traversal contract:
    // traversal would emit current-G string_id directly instead of text.
    if (!build_string_id_snap_source(
            project,
            warm_snapshot.records(),
            string_id_source)) {

        std::cerr
            << "Cannot build string-id SNAP source\n";

        return 1;
    }

    std::uint64_t string_id_snap_workspace_bytes = 0;
    std::uint64_t string_id_snap_strings = 0;
    std::uint64_t string_id_snap_components = 0;

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        const auto started =
            std::chrono::steady_clock::now();

        string_id_snap_plan id_plan;

        if (!prepare_string_id_snap_plan(
                project,
                string_id_source,
                id_plan)) {

            std::cerr
                << "String-id SNAP plan failed\n";

            return 1;
        }

        const auto plan_finished =
            std::chrono::steady_clock::now();

        std::vector<std::uint32_t>
            id_components;

        if (!emit_string_id_snap_components(
                string_id_source,
                id_plan,
                id_components)) {

            std::cerr
                << "String-id SNAP component emission failed\n";

            return 1;
        }

        const auto components_finished =
            std::chrono::steady_clock::now();

        snap_string_id_plan_samples.push_back(
            elapsed_ms(
                started,
                plan_finished));

        snap_string_id_components_samples.push_back(
            elapsed_ms(
                plan_finished,
                components_finished));

        snap_string_id_total_samples.push_back(
            elapsed_ms(
                started,
                components_finished));

        string_id_snap_strings =
            id_plan.strings.size();

        string_id_snap_components =
            id_components.size();

        string_id_snap_workspace_bytes =
            static_cast<std::uint64_t>(
                id_plan.g_to_ic.capacity()) *
                sizeof(std::uint32_t) +
            static_cast<std::uint64_t>(
                id_plan.strings.capacity()) *
                sizeof(string_id) +
            static_cast<std::uint64_t>(
                id_components.capacity()) *
                sizeof(std::uint32_t);
    }

    std::uint64_t direct_snap_workspace_bytes = 0;
    std::uint64_t direct_snap_strings = 0;
    std::uint64_t direct_snap_components = 0;

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        runtime_ic_snapshot direct_snapshot;

        const auto total_started =
            std::chrono::steady_clock::now();

        if (snapshot_runtime_ic_project(
                project,
                runtime_bindings,
                runtime_bytes,
                direct_snapshot) !=
            runtime_ic_snapshot_result::success) {

            std::cerr
                << "Direct SNAP profile traversal failed\n";

            return 1;
        }

        const auto plan_started =
            std::chrono::steady_clock::now();

        direct_snap_plan direct_plan;

        if (!prepare_direct_snap_plan(
                direct_snapshot.records(),
                direct_plan)) {

            std::cerr
                << "Direct SNAP plan failed\n";

            return 1;
        }

        const auto plan_finished =
            std::chrono::steady_clock::now();

        std::vector<std::uint32_t>
            component_ids;

        std::vector<std::uint32_t>
            component_begins;

        if (!encode_direct_snap_components(
                direct_snapshot.records(),
                direct_plan,
                component_ids,
                component_begins)) {

            std::cerr
                << "Direct SNAP component encode failed\n";

            return 1;
        }

        const auto components_finished =
            std::chrono::steady_clock::now();

        snap_direct_plan_samples.push_back(
            elapsed_ms(
                plan_started,
                plan_finished));

        snap_direct_components_samples.push_back(
            elapsed_ms(
                plan_finished,
                components_finished));

        snap_direct_total_samples.push_back(
            elapsed_ms(
                total_started,
                components_finished));

        direct_snap_strings =
            direct_plan.strings.size();

        direct_snap_components =
            component_ids.size();

        direct_snap_workspace_bytes =
            static_cast<std::uint64_t>(
                direct_plan.strings.capacity()) *
                sizeof(std::string_view) +
            static_cast<std::uint64_t>(
                direct_plan.index.capacity()) *
                sizeof(direct_snap_string_slot) +
            static_cast<std::uint64_t>(
                component_ids.capacity()) *
                sizeof(std::uint32_t) +
            static_cast<std::uint64_t>(
                component_begins.capacity()) *
                sizeof(std::uint32_t);
    }

    std::size_t snap_v2_image_bytes = 0;
    bool snap_v2_binary_equal = false;
    bool snap_v2_reset_ok = false;
    std::vector<std::byte> snap_v2_last_image;

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        const auto total_started =
            std::chrono::steady_clock::now();

        runtime_ic_native_snapshot snapshot_v2;

        if (snapshot_runtime_ic_project_native(
                project,
                runtime_bindings,
                runtime_bytes,
                snapshot_v2) !=
            runtime_ic_snapshot_result::success) {

            std::cerr
                << "Runtime IC SNAP V2 traversal failed\n";

            return 1;
        }

        const auto traverse_finished =
            std::chrono::steady_clock::now();

        runtime_ic_binary_plan plan_v2;

        if (prepare_runtime_ic_binary_native(
                project,
                snapshot_v2.source(),
                plan_v2) !=
            runtime_ic_codec_result::success) {

            std::cerr
                << "Runtime IC SNAP V2 prepare failed\n";

            return 1;
        }

        const auto prepare_finished =
            std::chrono::steady_clock::now();

        std::vector<std::byte> image_v2(
            plan_v2.size(),
            std::byte{0});

        const auto allocate_finished =
            std::chrono::steady_clock::now();

        if (encode_runtime_ic_binary_native(
                project,
                snapshot_v2.source(),
                plan_v2,
                image_v2) !=
            runtime_ic_codec_result::success) {

            std::cerr
                << "Runtime IC SNAP V2 encode failed\n";

            return 1;
        }

        const auto encode_finished =
            std::chrono::steady_clock::now();

        if (snapshot_v2.source().records.size() !=
                ic_record_count ||
            image_v2.size() !=
                ic_image_bytes) {

            std::cerr
                << "Runtime IC SNAP V2 shape mismatch\n";

            return 1;
        }

        snap_v2_traverse_samples.push_back(
            elapsed_ms(
                total_started,
                traverse_finished));

        snap_v2_prepare_samples.push_back(
            elapsed_ms(
                traverse_finished,
                prepare_finished));

        snap_v2_allocate_samples.push_back(
            elapsed_ms(
                prepare_finished,
                allocate_finished));

        snap_v2_encode_samples.push_back(
            elapsed_ms(
                allocate_finished,
                encode_finished));

        snap_v2_total_samples.push_back(
            elapsed_ms(
                total_started,
                encode_finished));

        snap_v2_image_bytes =
            image_v2.size();

        if (iteration + 1 ==
            ic_iterations) {

            snap_v2_last_image =
                std::move(image_v2);
        }
    }

    snap_v2_binary_equal =
        snap_v2_last_image == reset_image;

    if (!snap_v2_binary_equal) {
        std::cerr
            << "Runtime IC SNAP V2 binary mismatch\n";

        return 1;
    }

    runtime_ic_binary_view snap_v2_view;

    if (snap_v2_view.bind(
            snap_v2_last_image) !=
        runtime_ic_codec_result::success) {

        std::cerr
            << "Runtime IC SNAP V2 image bind failed\n";

        return 1;
    }

    runtime_ic_reset_stats snap_v2_reset_stats;

    if (reset_runtime_ic_binary(
            project,
            runtime_bindings,
            runtime_bytes,
            snap_v2_view,
            &snap_v2_reset_stats) !=
            runtime_ic_reset_result::success ||
        snap_v2_reset_stats.records !=
            ic_record_count) {

        std::cerr
            << "Runtime IC SNAP V2 RESET compatibility failed\n";

        return 1;
    }

    snap_v2_reset_ok = true;

std::vector<double> snap_v2_api_samples;

    try {
        snap_v2_api_samples.reserve(
            ic_iterations);
    }
    catch (...) {
        std::cerr
            << "Runtime IC SNAP V2 API sample allocation failed\n";

        return 1;
    }

    std::vector<std::byte> snap_v2_api_image;
    runtime_ic_snapshot_stats snap_v2_api_stats;

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        const auto started =
            std::chrono::steady_clock::now();

        std::vector<std::byte> image;

        if (snapshot_runtime_ic_binary(
                project,
                runtime_bindings,
                runtime_bytes,
                image,
                &snap_v2_api_stats) !=
            runtime_ic_snapshot_result::success) {

            std::cerr
                << "Runtime IC production binary SNAP failed\n";

            return 1;
        }

        const auto finished =
            std::chrono::steady_clock::now();

        snap_v2_api_samples.push_back(
            elapsed_ms(
                started,
                finished));

        if (iteration + 1 ==
            ic_iterations) {

            snap_v2_api_image =
                std::move(image);
        }
    }

    const auto snap_v2_api_binary_equal =
        snap_v2_api_image ==
            reset_image;

    if (!snap_v2_api_binary_equal ||
        snap_v2_api_stats.scalars !=
            ic_record_count) {

        std::cerr
            << "Runtime IC production binary SNAP mismatch\n";

        return 1;
    }

    runtime_ic_binary_view snap_v2_api_view;

    if (snap_v2_api_view.bind(
            snap_v2_api_image) !=
        runtime_ic_codec_result::success) {

        std::cerr
            << "Runtime IC production binary SNAP bind failed\n";

        return 1;
    }

    runtime_ic_reset_stats snap_v2_api_reset_stats;

    if (reset_runtime_ic_binary(
            project,
            runtime_bindings,
            runtime_bytes,
            snap_v2_api_view,
            &snap_v2_api_reset_stats) !=
            runtime_ic_reset_result::success ||
        snap_v2_api_reset_stats.records !=
            ic_record_count) {

        std::cerr
            << "Runtime IC production binary SNAP RESET failed\n";

        return 1;
    }

    const bool snap_v2_api_reset_ok = true;

    runtime_ic_binary_view reset_view;

    if (reset_view.bind(
            reset_image) !=
        runtime_ic_codec_result::success) {

        std::cerr
            << "Runtime IC RESET image bind failed\n";

        return 1;
    }

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        const auto started =
            std::chrono::steady_clock::now();

        runtime_ic_reset_stats stats;

        if (reset_runtime_ic_binary(
                project,
                runtime_bindings,
                runtime_bytes,
                reset_view,
                &stats) !=
            runtime_ic_reset_result::success) {

            std::cerr
                << "Runtime IC RESET benchmark failed\n";

            return 1;
        }

        const auto finished =
            std::chrono::steady_clock::now();

        reset_samples.push_back(
            elapsed_ms(
                started,
                finished));

        if (stats.records !=
            ic_record_count) {

            std::cerr
                << "Runtime IC RESET record count mismatch\n";

            return 1;
        }

        reset_value_bytes =
            stats.bytes;
    }

    const auto median =
        [](std::vector<double> values) {
            std::sort(
                values.begin(),
                values.end());

            const auto middle =
                values.size() / 2;

            if ((values.size() & 1u) != 0) {
                return values[middle];
            }

            return
                (values[middle - 1] +
                 values[middle]) /
                2.0;
        };

    const auto snap_traverse_ms =
        median(
            snap_traverse_samples);

    const auto snap_prepare_ms =
        median(
            snap_prepare_samples);

    const auto snap_encode_ms =
        median(
            snap_encode_samples);

    const auto snap_total_ms =
        median(
            snap_total_samples);

    std::vector<std::string_view>
        reset_profile_components;

    std::vector<reset_profile_record>
        reset_profile_records;

    std::vector<reset_profile_target>
        reset_profile_targets;

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        double decode_ms = 0.0;
        double resolve_ms = 0.0;
        double apply_ms = 0.0;

        const auto started =
            std::chrono::steady_clock::now();

        if (!profile_reset_phases(
                project,
                runtime_bindings,
                runtime_bytes,
                reset_view,
                reset_profile_components,
                reset_profile_records,
                reset_profile_targets,
                decode_ms,
                resolve_ms,
                apply_ms)) {

            std::cerr
                << "RESET phase profile failed\n";

            return 1;
        }

        const auto finished =
            std::chrono::steady_clock::now();

        reset_decode_samples.push_back(
            decode_ms);

        reset_resolve_samples.push_back(
            resolve_ms);

        reset_profile_apply_samples.push_back(
            apply_ms);

        reset_profile_total_samples.push_back(
            elapsed_ms(
                started,
                finished));
    }

    std::vector<reset_profile_target>
        grouped_targets;

    grouped_resolve_stats
        grouped_stats;

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        double grouped_resolve_ms = 0.0;
        double grouped_apply_ms = 0.0;

        const auto started =
            std::chrono::steady_clock::now();

        if (!profile_grouped_resolve(
                project,
                runtime_bindings,
                runtime_bytes,
                reset_profile_components,
                reset_profile_records,
                grouped_targets,
                grouped_stats,
                grouped_resolve_ms,
                grouped_apply_ms)) {

            std::cerr
                << "Grouped RESET profile failed\n";

            return 1;
        }

        const auto finished =
            std::chrono::steady_clock::now();

        reset_grouped_resolve_samples.push_back(
            grouped_resolve_ms);

        reset_grouped_apply_samples.push_back(
            grouped_apply_ms);

        reset_grouped_total_samples.push_back(
            elapsed_ms(
                started,
                finished));
    }

    std::vector<object_resolve_profile_group>
        object_profile_groups;

    object_resolve_profile_stats
        object_profile_stats;

    if (!build_object_resolve_profile_groups(
            reset_profile_components,
            reset_profile_records,
            object_profile_groups,
            object_profile_stats)) {

        std::cerr
            << "Cannot build object resolve profile groups\n";

        return 1;
    }

    std::vector<string_id>
        object_profile_names;

    std::vector<identity_ref>
        object_profile_identities;

    std::vector<object_handle>
        object_profile_objects;

    std::vector<object_entry>
        object_profile_records;

    std::vector<runtime_offset>
        object_profile_offsets;

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        double find_string_ms = 0.0;
        double find_identity_ms = 0.0;
        double find_object_ms = 0.0;
        double object_record_ms = 0.0;
        double binding_ms = 0.0;

        const auto started =
            std::chrono::steady_clock::now();

        if (!profile_object_resolve_phases(
                project,
                runtime_bindings,
                reset_profile_components,
                object_profile_groups,
                object_profile_names,
                object_profile_identities,
                object_profile_objects,
                object_profile_records,
                object_profile_offsets,
                find_string_ms,
                find_identity_ms,
                find_object_ms,
                object_record_ms,
                binding_ms)) {

            std::cerr
                << "Object resolve phase profile failed\n";

            return 1;
        }

        const auto finished =
            std::chrono::steady_clock::now();

        object_find_string_samples.push_back(
            find_string_ms);

        object_find_identity_samples.push_back(
            find_identity_ms);

        object_find_object_samples.push_back(
            find_object_ms);

        object_record_samples.push_back(
            object_record_ms);

        object_binding_samples.push_back(
            binding_ms);

        object_resolve_profile_samples.push_back(
            elapsed_ms(
                started,
                finished));
    }

    std::vector<string_id>
        ic_string_translation;

    std::vector<reset_profile_target>
        translated_targets;

    translated_grouped_stats
        translated_stats;

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        const auto translation_started =
            std::chrono::steady_clock::now();

        if (!build_ic_string_translation(
                project,
                reset_view,
                ic_string_translation)) {

            std::cerr
                << "IC string translation failed\n";

            return 1;
        }

        const auto translation_finished =
            std::chrono::steady_clock::now();

        double translated_resolve_ms = 0.0;
        double translated_apply_ms = 0.0;

        const auto translated_started =
            translation_finished;

        if (!profile_translated_grouped_resolve(
                project,
                runtime_bindings,
                runtime_bytes,
                reset_view,
                ic_string_translation,
                translated_targets,
                translated_stats,
                translated_resolve_ms,
                translated_apply_ms)) {

            std::cerr
                << "Translated grouped RESET profile failed\n";

            return 1;
        }

        const auto translated_finished =
            std::chrono::steady_clock::now();

        string_translation_samples.push_back(
            elapsed_ms(
                translation_started,
                translation_finished));

        translated_grouped_resolve_samples.push_back(
            translated_resolve_ms);

        translated_grouped_apply_samples.push_back(
            translated_apply_ms);

        translated_grouped_total_samples.push_back(
            elapsed_ms(
                translation_started,
                translated_finished));
    }

    std::vector<compact_reset_entry>
        compact_plan;

    try {
        compact_plan.reserve(
            reset_view.record_count());
    }
    catch (...) {
        std::cerr
            << "Cannot allocate compact RESET plan\n";

        return 1;
    }

    for (std::size_t iteration = 0;
         iteration < ic_iterations;
         ++iteration) {

        double compact_plan_ms = 0.0;
        double compact_apply_ms = 0.0;

        const auto started =
            std::chrono::steady_clock::now();

        if (!profile_compact_reset(
                project,
                runtime_bindings,
                runtime_bytes,
                reset_view,
                compact_plan,
                compact_plan_ms,
                compact_apply_ms)) {

            std::cerr
                << "Compact RESET profile failed\n";

            return 1;
        }

        const auto finished =
            std::chrono::steady_clock::now();

        reset_compact_plan_samples.push_back(
            compact_plan_ms);

        reset_compact_apply_samples.push_back(
            compact_apply_ms);

        reset_compact_total_samples.push_back(
            elapsed_ms(
                started,
                finished));
    }

    const auto snap_allocate_ms =
        median(
            snap_allocate_samples);

    const auto snap_direct_plan_ms =
        median(
            snap_direct_plan_samples);

    const auto snap_direct_components_ms =
        median(
            snap_direct_components_samples);

    const auto snap_direct_total_ms =
        median(
            snap_direct_total_samples);

    const auto snap_string_id_plan_ms =
        median(
            snap_string_id_plan_samples);

    const auto snap_string_id_components_ms =
        median(
            snap_string_id_components_samples);

    const auto snap_string_id_total_ms =
        median(
            snap_string_id_total_samples);

    const auto snap_v2_api_ms =
        median(
            snap_v2_api_samples);

    const auto snap_v2_api_records_per_second =
        snap_v2_api_ms > 0.0
        ? static_cast<double>(
              ic_record_count) *
              1000.0 /
              snap_v2_api_ms
        : 0.0;

    const auto snap_v2_traverse_ms =
        median(
            snap_v2_traverse_samples);

    const auto snap_v2_prepare_ms =
        median(
            snap_v2_prepare_samples);

    const auto snap_v2_allocate_ms =
        median(
            snap_v2_allocate_samples);

    const auto snap_v2_encode_ms =
        median(
            snap_v2_encode_samples);

    const auto snap_v2_total_ms =
        median(
            snap_v2_total_samples);

    const auto snap_v2_records_per_second =
        snap_v2_total_ms > 0.0
        ? static_cast<double>(
              ic_record_count) *
              1000.0 /
              snap_v2_total_ms
        : 0.0;

    const auto reset_ms =
        median(
            reset_samples);

    const auto reset_decode_ms =
        median(
            reset_decode_samples);

    const auto reset_resolve_ms =
        median(
            reset_resolve_samples);

    const auto reset_profile_apply_ms =
        median(
            reset_profile_apply_samples);

    const auto reset_profile_total_ms =
        median(
            reset_profile_total_samples);

    const auto reset_component_bytes =
        static_cast<std::uint64_t>(
            reset_profile_components.capacity()) *
        sizeof(std::string_view);

    const auto reset_record_bytes =
        static_cast<std::uint64_t>(
            reset_profile_records.capacity()) *
        sizeof(reset_profile_record);

    const auto reset_target_bytes =
        static_cast<std::uint64_t>(
            reset_profile_targets.capacity()) *
        sizeof(reset_profile_target);

    const auto reset_grouped_resolve_ms =
        median(
            reset_grouped_resolve_samples);

    const auto reset_grouped_apply_ms =
        median(
            reset_grouped_apply_samples);

    const auto reset_grouped_total_ms =
        median(
            reset_grouped_total_samples);

    const auto object_find_string_ms =
        median(
            object_find_string_samples);

    const auto object_find_identity_ms =
        median(
            object_find_identity_samples);

    const auto object_find_object_ms =
        median(
            object_find_object_samples);

    const auto object_record_ms =
        median(
            object_record_samples);

    const auto object_binding_ms =
        median(
            object_binding_samples);

    const auto object_resolve_profile_ms =
        median(
            object_resolve_profile_samples);

    const auto string_translation_ms =
        median(
            string_translation_samples);

    const auto translated_grouped_resolve_ms =
        median(
            translated_grouped_resolve_samples);

    const auto translated_grouped_apply_ms =
        median(
            translated_grouped_apply_samples);

    const auto translated_grouped_total_ms =
        median(
            translated_grouped_total_samples);

    const auto translation_bytes =
        static_cast<std::uint64_t>(
            ic_string_translation.capacity()) *
        sizeof(string_id);

    const auto reset_compact_plan_ms =
        median(
            reset_compact_plan_samples);

    const auto reset_compact_apply_ms =
        median(
            reset_compact_apply_samples);

    const auto reset_compact_ms =
        median(
            reset_compact_total_samples);

    const auto compact_plan_bytes =
        static_cast<std::uint64_t>(
            compact_plan.size()) *
        sizeof(compact_reset_entry);

    const auto snap_records_per_second =
        snap_total_ms > 0.0
        ? static_cast<double>(
              ic_record_count) *
              1000.0 /
              snap_total_ms
        : 0.0;

    const auto reset_records_per_second =
        reset_ms > 0.0
        ? static_cast<double>(
              ic_record_count) *
              1000.0 /
              reset_ms
        : 0.0;

    const auto reset_compact_records_per_second =
        reset_compact_ms > 0.0
        ? static_cast<double>(
              ic_record_count) *
              1000.0 /
              reset_compact_ms
        : 0.0;

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
        << ",endpoint_paths="
        << project.endpoint_path_count()
        << ",endpoint_path_steps="
        << project.endpoint_path_step_count()
        << ",peak_ws_bytes="
        << peak_working_set_bytes()
        << ",ic_iterations="
        << ic_iterations
        << ",ic_records="
        << ic_record_count
        << ",ic_image_bytes="
        << ic_image_bytes
        << ",ic_value_bytes="
        << reset_value_bytes
        << ",snap_traverse_ms="
        << snap_traverse_ms
        << ",snap_prepare_ms="
        << snap_prepare_ms
        << ",snap_allocate_ms="
        << snap_allocate_ms
        << ",snap_encode_ms="
        << snap_encode_ms
        << ",snap_total_ms="
        << snap_total_ms
        << ",snap_v2_traverse_ms="
        << snap_v2_traverse_ms
        << ",snap_v2_prepare_ms="
        << snap_v2_prepare_ms
        << ",snap_v2_allocate_ms="
        << snap_v2_allocate_ms
        << ",snap_v2_encode_ms="
        << snap_v2_encode_ms
        << ",snap_v2_total_ms="
        << snap_v2_total_ms
        << ",snap_v2_records_per_s="
        << snap_v2_records_per_second
        << ",snap_v2_image_bytes="
        << snap_v2_image_bytes
        << ",snap_v2_binary_equal="
        << (snap_v2_binary_equal ? 1 : 0)
        << ",snap_v2_reset_ok="
        << (snap_v2_reset_ok ? 1 : 0)
        << ",snap_v2_api_ms="
        << snap_v2_api_ms
        << ",snap_v2_api_records_per_s="
        << snap_v2_api_records_per_second
        << ",snap_v2_api_binary_equal="
        << (snap_v2_api_binary_equal ? 1 : 0)
        << ",snap_v2_api_reset_ok="
        << (snap_v2_api_reset_ok ? 1 : 0)
        << ",snap_direct_plan_ms="
        << snap_direct_plan_ms
        << ",snap_direct_components_ms="
        << snap_direct_components_ms
        << ",snap_direct_total_ms="
        << snap_direct_total_ms
        << ",snap_direct_strings="
        << direct_snap_strings
        << ",snap_direct_components="
        << direct_snap_components
        << ",snap_direct_workspace_bytes="
        << direct_snap_workspace_bytes
        << ",snap_string_id_plan_ms="
        << snap_string_id_plan_ms
        << ",snap_string_id_components_ms="
        << snap_string_id_components_ms
        << ",snap_string_id_total_ms="
        << snap_string_id_total_ms
        << ",snap_string_id_strings="
        << string_id_snap_strings
        << ",snap_string_id_components="
        << string_id_snap_components
        << ",snap_string_id_workspace_bytes="
        << string_id_snap_workspace_bytes
        << ",snap_records_per_s="
        << snap_records_per_second
        << ",reset_ms="
        << reset_ms
        << ",reset_records_per_s="
        << reset_records_per_second
        << ",reset_decode_ms="
        << reset_decode_ms
        << ",reset_resolve_ms="
        << reset_resolve_ms
        << ",reset_profile_apply_ms="
        << reset_profile_apply_ms
        << ",reset_profile_total_ms="
        << reset_profile_total_ms
        << ",reset_component_bytes="
        << reset_component_bytes
        << ",reset_record_bytes="
        << reset_record_bytes
        << ",reset_target_bytes="
        << reset_target_bytes
        << ",reset_grouped_resolve_ms="
        << reset_grouped_resolve_ms
        << ",reset_grouped_apply_ms="
        << reset_grouped_apply_ms
        << ",reset_grouped_total_ms="
        << reset_grouped_total_ms
        << ",grouped_object_resolves="
        << grouped_stats.object_resolves
        << ",grouped_member_steps="
        << grouped_stats.member_steps
        << ",grouped_reused_member_steps="
        << grouped_stats.reused_member_steps
        << ",object_profile_groups="
        << object_profile_stats.groups
        << ",object_profile_components="
        << object_profile_stats.components
        << ",object_find_string_ms="
        << object_find_string_ms
        << ",object_find_identity_ms="
        << object_find_identity_ms
        << ",object_find_object_ms="
        << object_find_object_ms
        << ",object_record_ms="
        << object_record_ms
        << ",object_binding_ms="
        << object_binding_ms
        << ",object_resolve_profile_ms="
        << object_resolve_profile_ms
        << ",ic_strings="
        << reset_view.string_count()
        << ",string_translation_ms="
        << string_translation_ms
        << ",translated_grouped_resolve_ms="
        << translated_grouped_resolve_ms
        << ",translated_grouped_apply_ms="
        << translated_grouped_apply_ms
        << ",translated_grouped_total_ms="
        << translated_grouped_total_ms
        << ",translated_object_resolves="
        << translated_stats.object_resolves
        << ",translated_member_steps="
        << translated_stats.member_steps
        << ",translated_reused_steps="
        << translated_stats.reused_member_steps
        << ",translation_bytes="
        << translation_bytes
        << ",reset_compact_plan_ms="
        << reset_compact_plan_ms
        << ",reset_compact_apply_ms="
        << reset_compact_apply_ms
        << ",reset_compact_ms="
        << reset_compact_ms
        << ",reset_compact_records_per_s="
        << reset_compact_records_per_second
        << ",compact_plan_bytes="
        << compact_plan_bytes
        << '\n';

    return 0;
}
