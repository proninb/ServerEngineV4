/*
 * RUNTIME-LOOKUP-PROFILE-V1.
 *
 * Measures the existing mmap-native V4 lookup path without changing
 * compiled.bin, Runtime Query, Runtime layout, or production lookup code.
 */
#include "project/assign/assign_table.hpp"
#include "project/file/file_context.hpp"
#include "project/graph/graph.hpp"
#include "project/persistence/compiled_project.hpp"
#include "project/runtime/fixed_direct_materializer.hpp"
#include "project/runtime/runtime_layout.hpp"
#include "project/runtime/runtime_query.hpp"
#include "project/semantic/identity.hpp"
#include "project/source/source_map.hpp"
#include "project/string/string_table.hpp"
#include "configuration/server_configuration.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace cw::server;
using clock_type = std::chrono::steady_clock;

volatile std::uint64_t benchmark_sink = 0;

struct lookup_fixture final {
    std::vector<std::byte> bytes;
    std::string full_name;
    std::string first_member;
    std::string middle_member;
    std::string last_member;
};

[[nodiscard]] bool resolve_identity(
    string_table& strings,
    identity_space& identities,
    std::string_view spelling,
    identity_kind kind,
    identity_ref& output) {

    output = {};

    string_id name;

    return succeeded(
               strings.intern(
                   spelling,
                   name)) &&
        succeeded(
               identities.resolve(
                   identities.root(),
                   name,
                   kind,
                   output)) &&
        output;
}

[[nodiscard]] bool build_lookup_fixture(
    std::size_t members_per_type,
    std::size_t depth,
    lookup_fixture& output) {

    output = {};

    if (members_per_type == 0 ||
        depth == 0 ||
        members_per_type >
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)())) {

        return false;
    }

    string_table strings;
    identity_space identities{strings};
    graph G;
    assign_table assigns;

    const auto integer =
        G.intrinsic(
            intrinsic_type::signed_int);

    if (!integer) {
        return false;
    }

    std::vector<string_id> field_names(
        members_per_type);

    for (std::size_t index = 0;
         index < members_per_type;
         ++index) {

        const auto spelling =
            "field_" +
            std::to_string(index);

        if (!succeeded(
                strings.intern(
                    spelling,
                    field_names[index]))) {

            return false;
        }
    }

    string_id next_name;
    string_id value_name;
    string_id missing_name;

    if (!succeeded(
            strings.intern(
                "next",
                next_name)) ||
        !succeeded(
            strings.intern(
                "value",
                value_name)) ||
        !succeeded(
            strings.intern(
                "missing",
                missing_name))) {

        return false;
    }

    (void)missing_name;

    std::vector<type_handle> types(depth);

    for (std::size_t level = 0;
         level < depth;
         ++level) {

        const auto spelling =
            "LookupType_" +
            std::to_string(level);

        identity_ref identity;

        if (!resolve_identity(
                strings,
                identities,
                spelling,
                identity_kind::type,
                identity) ||
            !succeeded(
                G.declare_record(
                    identity,
                    graph_record_kind::struct_type,
                    types[level]))) {

            return false;
        }
    }

    for (std::size_t reverse = depth;
         reverse != 0;
         --reverse) {

        const auto level =
            reverse - 1;

        std::vector<member_record> members(
            members_per_type);

        std::vector<construction_value> construction(
            members_per_type);

        for (std::size_t index = 0;
             index < members_per_type;
             ++index) {

            members[index] = {
                field_names[index],
                integer,
                graph_member_access::public_access,
            };
        }

        if (level + 1 < depth) {
            const auto child =
                G.named(
                    types[level + 1]);

            if (!child) {
                return false;
            }

            members.back() = {
                next_name,
                child,
                graph_member_access::public_access,
            };
        }
        else {
            members.back() = {
                value_name,
                integer,
                graph_member_access::public_access,
            };
        }

        if (!succeeded(
                G.define_record(
                    types[level],
                    graph_record_kind::struct_type,
                    members,
                    construction))) {

            return false;
        }
    }

    identity_ref object_identity;

    if (!resolve_identity(
            strings,
            identities,
            "A",
            identity_kind::object,
            object_identity)) {

        return false;
    }

    const auto root_type =
        G.named(
            types.front());

    object_handle object;

    if (!root_type ||
        !succeeded(
            G.add_object(
                object_identity,
                root_type,
                object))) {

        return false;
    }

    file_context files;
    source_map sources;

    if (!succeeded(
            sources.finalize(
                files.size(),
                identities,
                G))) {

        return false;
    }

    compiled_project_layout compiled_layout;

    if (prepare_compiled_project_layout(
            strings,
            identities,
            G,
            assigns,
            files,
            sources,
            compiled_layout) !=
        compiled_project_image_result::success) {

        return false;
    }

    try {
        output.bytes.assign(
            compiled_layout.size(),
            std::byte{0});
    }
    catch (...) {
        return false;
    }

    if (encode_compiled_project_image(
            strings,
            identities,
            G,
            assigns,
            files,
            sources,
            compiled_layout,
            output.bytes) !=
        compiled_project_image_result::success) {

        return false;
    }

    const auto terminal_member =
        depth == 1
            ? std::string{"value"}
            : std::string{"next"};

    output.first_member =
        members_per_type == 1
            ? terminal_member
            : std::string{"field_0"};

    const auto middle =
        members_per_type / 2;

    output.middle_member =
        middle + 1 >= members_per_type
            ? terminal_member
            : "field_" +
                std::to_string(
                    middle);

    output.last_member =
        terminal_member;

    output.full_name = "A";

    for (std::size_t level = 0;
         level + 1 < depth;
         ++level) {

        output.full_name += ".next";
    }

    output.full_name += ".value";

    return true;
}

template <typename Function>
[[nodiscard]] double measure_ns(
    std::size_t iterations,
    Function&& function) {

    std::uint64_t sink = 0;

    const auto begin =
        clock_type::now();

    for (std::size_t index = 0;
         index < iterations;
         ++index) {

        sink ^=
            static_cast<std::uint64_t>(
                function());
    }

    const auto end =
        clock_type::now();

    benchmark_sink = sink;

    const auto elapsed =
        std::chrono::duration<double, std::nano>(
            end - begin)
            .count();

    return elapsed /
        static_cast<double>(iterations);
}

[[nodiscard]] bool resolve_full_path(
    const compiled_project_view& project,
    const runtime_layout& layout,
    std::string_view name,
    runtime_offset& output) noexcept {

    output = 0;

    const auto dot =
        name.find('.');

    const auto object_text =
        name.substr(0, dot);

    const auto object_string =
        project.find_string(
            object_text);

    if (!object_string) {
        return false;
    }

    const auto object_identity =
        project.find_identity(
            project.identity_root(),
            object_string,
            identity_kind::object);

    if (!object_identity) {
        return false;
    }

    const auto object =
        project.find_object(
            object_identity);

    if (!object ||
        !layout.object_offset(
            object,
            output)) {

        return false;
    }

    object_entry object_record;

    if (!project.object(
            object,
            object_record)) {

        return false;
    }

    auto type =
        object_record.type;

    if (dot == std::string_view::npos) {
        return true;
    }

    std::size_t begin =
        dot + 1;

    while (begin < name.size()) {
        const auto next =
            name.find(
                '.',
                begin);

        const auto end =
            next == std::string_view::npos
                ? name.size()
                : next;

        if (end == begin ||
            type.kind() !=
                type_ref_kind::named ||
            type.payload() == 0) {

            return false;
        }

        const auto member_string =
            project.find_string(
                name.substr(
                    begin,
                    end - begin));

        if (!member_string) {
            return false;
        }

        const auto owner =
            project.type_at(
                static_cast<std::size_t>(
                    type.payload() - 1));

        if (!owner) {
            return false;
        }

        const auto member =
            project.find_member(
                owner,
                member_string);

        if (!member) {
            return false;
        }

        type_entry entry;
        member_record record;
        record_offset member_offset = 0;

        if (!project.type(
                owner,
                entry) ||
            !project.member(
                owner,
                member,
                record) ||
            !layout.member_offset(
                static_cast<std::size_t>(
                    entry.members.begin) +
                    member.value(),
                member_offset)) {

            return false;
        }

        if (output >
            (std::numeric_limits<runtime_offset>::max)() -
                member_offset) {

            return false;
        }

        output +=
            static_cast<runtime_offset>(
                member_offset);

        type = record.type;

        if (next == std::string_view::npos) {
            break;
        }

        begin = next + 1;
    }

    return true;
}

[[nodiscard]] bool parse_size(
    const char* text,
    std::size_t& output) {

    output = 0;

    if (text == nullptr ||
        *text == '\0') {

        return false;
    }

    try {
        const auto value =
            std::stoull(text);

        if (value == 0 ||
            value >
                static_cast<unsigned long long>(
                    (std::numeric_limits<std::size_t>::max)())) {

            return false;
        }

        output =
            static_cast<std::size_t>(
                value);

        return true;
    }
    catch (...) {
        return false;
    }
}

}

int main(
    int argc,
    char** argv) {

    if (argc != 4) {
        std::cerr
            << "Usage:\n"
            << "  ServerEngineV4RuntimeLookupBenchmark "
               "<members-per-type> <depth> <iterations>\n";
        return 2;
    }

    std::size_t members = 0;
    std::size_t depth = 0;
    std::size_t iterations = 0;

    if (!parse_size(argv[1], members) ||
        !parse_size(argv[2], depth) ||
        !parse_size(argv[3], iterations)) {

        std::cerr
            << "All arguments must be positive integers\n";
        return 2;
    }

    lookup_fixture fixture;

    if (!build_lookup_fixture(
            members,
            depth,
            fixture)) {

        std::cerr
            << "Cannot build lookup fixture\n";
        return 3;
    }

    compiled_project_view project;

    if (project.bind(
            fixture.bytes) !=
        compiled_project_image_result::success) {

        std::cerr
            << "Cannot bind compiled lookup fixture\n";
        return 4;
    }

#if defined(_WIN32)
    const server_abi_configuration abi{
        abi_target::windows_x64,
        8,
    };
#else
    const server_abi_configuration abi{
        abi_target::posix_x64,
        8,
    };
#endif

    runtime_layout layout;

    if (prepare_runtime_layout(
            project,
            abi,
            layout) !=
        runtime_layout_result::success) {

        std::cerr
            << "Cannot prepare lookup Runtime layout\n";
        return 5;
    }

    std::vector<std::byte> runtime(
        static_cast<std::size_t>(
            layout.size()),
        std::byte{0});

    if (materialize_fixed_direct(
            project,
            layout,
            abi,
            runtime) !=
        fixed_direct_materialization_result::success) {

        std::cerr
            << "Cannot materialize lookup Runtime image\n";
        return 12;
    }

    const auto object_name =
        project.find_string("A");

    const auto object_identity =
        project.find_identity(
            project.identity_root(),
            object_name,
            identity_kind::object);

    const auto object =
        project.find_object(
            object_identity);

    object_entry object_record;

    if (!object_name ||
        !object_identity ||
        !object ||
        !project.object(
            object,
            object_record) ||
        object_record.type.kind() !=
            type_ref_kind::named ||
        object_record.type.payload() == 0) {

        std::cerr
            << "Invalid lookup fixture object\n";
        return 6;
    }

    const auto root_type =
        project.type_at(
            static_cast<std::size_t>(
                object_record.type.payload() - 1));

    const auto first_name =
        project.find_string(
            fixture.first_member);

    const auto middle_name =
        project.find_string(
            fixture.middle_member);

    const auto last_name =
        project.find_string(
            fixture.last_member);

    const auto missing_name =
        project.find_string(
            "missing");

    if (!root_type ||
        !first_name ||
        !middle_name ||
        !last_name ||
        !missing_name) {

        std::cerr
            << "Invalid lookup fixture names\n";
        return 7;
    }

    const auto first_member =
        project.find_member(
            root_type,
            first_name);

    const auto middle_member =
        project.find_member(
            root_type,
            middle_name);

    const auto last_member =
        project.find_member(
            root_type,
            last_name);

    if (!first_member ||
        !middle_member ||
        !last_member ||
        project.find_member(
            root_type,
            missing_name)) {

        std::cerr
            << "Invalid member lookup fixture\n";
        return 8;
    }

    type_entry root_entry;

    if (!project.type(
            root_type,
            root_entry)) {

        std::cerr
            << "Invalid root type\n";
        return 9;
    }

    runtime_offset object_offset = 0;
    record_offset last_offset = 0;

    if (!layout.object_offset(
            object,
            object_offset) ||
        !layout.member_offset(
            static_cast<std::size_t>(
                root_entry.members.begin) +
                last_member.value(),
            last_offset)) {

        std::cerr
            << "Invalid Runtime lookup offsets\n";
        return 10;
    }

    runtime_offset full_offset = 0;

    if (!resolve_full_path(
            project,
            layout,
            fixture.full_name,
            full_offset)) {

        std::cerr
            << "Full lookup path does not resolve\n";
        return 11;
    }

    const auto find_string_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                return project.find_string(
                    fixture.last_member)
                    .value();
            });

    const auto find_identity_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                return project.find_identity(
                    project.identity_root(),
                    object_name,
                    identity_kind::object)
                    .value();
            });

    const auto find_object_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                return project.find_object(
                    object_identity)
                    .value();
            });

    const auto member_first_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                return project.find_member(
                    root_type,
                    first_name)
                    .value();
            });

    const auto member_middle_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                return project.find_member(
                    root_type,
                    middle_name)
                    .value();
            });

    const auto member_last_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                return project.find_member(
                    root_type,
                    last_name)
                    .value();
            });

    const auto member_missing_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                return project.find_member(
                    root_type,
                    missing_name)
                    .value();
            });

    const auto object_offset_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                runtime_offset value = 0;

                return layout.object_offset(
                           object,
                           value)
                    ? value + 1
                    : 0;
            });

    const auto member_offset_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                record_offset value = 0;

                return layout.member_offset(
                           static_cast<std::size_t>(
                               root_entry.members.begin) +
                               last_member.value(),
                           value)
                    ? static_cast<std::uint64_t>(
                          value) +
                          1
                    : 0;
            });

    const auto full_path_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                runtime_offset value = 0;

                return resolve_full_path(
                           project,
                           layout,
                           fixture.full_name,
                           value)
                    ? value + 1
                    : 0;
            });

    runtime_binding_index bindings;

    if (!layout.release_bindings(
            bindings)) {

        std::cerr
            << "Cannot publish Runtime lookup bindings\n";
        return 13;
    }

    runtime_value warm_value;

    if (get_runtime_value(
            project,
            bindings,
            runtime,
            fixture.full_name,
            warm_value) !=
        runtime_query_result::success) {

        std::cerr
            << "Runtime Query path does not resolve\n";
        return 14;
    }

    const auto get_runtime_value_ns =
        measure_ns(
            iterations,
            [&]() noexcept {
                runtime_value value;

                return get_runtime_value(
                           project,
                           bindings,
                           runtime,
                           fixture.full_name,
                           value) ==
                        runtime_query_result::success
                    ? value.bits + 1
                    : 0;
            });

    std::cout
        << std::fixed
        << std::setprecision(3)
        << "members=" << members
        << ",depth=" << depth
        << ",iterations=" << iterations
        << ",find_string_ns=" << find_string_ns
        << ",find_identity_ns=" << find_identity_ns
        << ",find_object_ns=" << find_object_ns
        << ",find_member_first_ns=" << member_first_ns
        << ",find_member_middle_ns=" << member_middle_ns
        << ",find_member_last_ns=" << member_last_ns
        << ",find_member_missing_ns=" << member_missing_ns
        << ",object_offset_ns=" << object_offset_ns
        << ",member_offset_ns=" << member_offset_ns
        << ",full_path_ns=" << full_path_ns
        << ",get_runtime_value_ns=" << get_runtime_value_ns
        << ",runtime_bytes=" << runtime.size()
        << ",compiled_bytes=" << fixture.bytes.size()
        << ",sink=" << benchmark_sink
        << '\n';

    return 0;
}
