#include "runtime_query.hpp"
#include "runtime_system.hpp"
#include "../task/runtime/task_variables.hpp"

#include <array>
#include <charconv>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace cw::server {
namespace {

enum class member_search_result : std::uint8_t {
    none = 0,
    found,
    ambiguous,
    invalid,
};

struct resolved_member final {
    runtime_offset offset = 0;
    type_ref type{};
};

[[nodiscard]] bool add_offset(
    runtime_offset left,
    runtime_offset right,
    runtime_offset& output) noexcept {

    if (left >
        (std::numeric_limits<runtime_offset>::max)() - right) {

        return false;
    }

    output = left + right;
    return true;
}

[[nodiscard]] runtime_query_result read_native_bits(
    std::span<const std::byte> runtime,
    runtime_offset offset,
    std::uint8_t size,
    std::uint64_t& output) noexcept {

    output = 0;

    if (size == 0 ||
        size > sizeof(output) ||
        offset >
            static_cast<runtime_offset>(
                runtime.size()) ||
        size >
            static_cast<runtime_offset>(
                runtime.size()) - offset) {

        return runtime_query_result::invalid_runtime;
    }

    std::memcpy(
        &output,
        runtime.data() +
            static_cast<std::size_t>(offset),
        size);

    return runtime_query_result::success;
}

[[nodiscard]] runtime_query_result normalize_location(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    type_ref& type,
    runtime_offset& offset, bool* writable = nullptr, bool linked_reference = false) noexcept {

    const auto limit =
        project.derived_type_count() + 1;

    for (std::size_t depth = 0;
         depth <= limit;
         ++depth) {

        if (!type) {
            return runtime_query_result::invalid_runtime;
        }

        if (type.kind() != type_ref_kind::derived) {
            return runtime_query_result::success;
        }

        derived_type_record derived;

        if (!project.derived(
                type,
                derived)) {

            return runtime_query_result::invalid_runtime;
        }

        switch (derived.kind) {
        case derived_type_kind::const_qualified:
        case derived_type_kind::volatile_qualified:
            if (writable && derived.kind == derived_type_kind::const_qualified) *writable = false;
            type = derived.child;
            continue;

        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference: {
            if (writable && !linked_reference) *writable = false;
            linked_reference = false;
            std::uint8_t reference_size = 0;
            std::uint64_t target_base_address = 0;

            if (!bindings.reference_layout(
                    reference_size,
                    target_base_address)) {

                return runtime_query_result::invalid_runtime;
            }

            if (target_base_address == 0) {
                target_base_address =
                    static_cast<std::uint64_t>(
                        reinterpret_cast<
                            std::uintptr_t>(
                                runtime.data()));
            }

            std::uint64_t target = 0;

            const auto read =
                read_native_bits(
                    runtime,
                    offset,
                    reference_size,
                    target);

            if (read !=
                runtime_query_result::success) {

                return read;
            }

            if (target <
                target_base_address) {

                return runtime_query_result::invalid_runtime;
            }

            const auto relative =
                target -
                target_base_address;

            if (relative >= runtime.size()) {
                return runtime_query_result::invalid_runtime;
            }

            offset =
                static_cast<runtime_offset>(
                    relative);

            type = derived.child;
            continue;
        }

        case derived_type_kind::pointer:
        case derived_type_kind::bounded_array:
        case derived_type_kind::unbounded_array:
            return runtime_query_result::success;
        }
    }

    return runtime_query_result::invalid_runtime;
}

[[nodiscard]] member_search_result find_member_recursive(
    const compiled_project_view& project,
    const runtime_binding_index* bindings,
    type_handle type,
    string_id name,
    std::size_t depth,
    resolved_member& output) noexcept {

    output = {};

    if (!type ||
        depth > project.type_count()) {

        return member_search_result::invalid;
    }

    type_entry entry;

    if (!project.type(
            type,
            entry) ||
        !entry.defined()) {

        return member_search_result::invalid;
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

            return member_search_result::invalid;
        }

        const auto global =
            static_cast<std::size_t>(
                entry.members.begin) +
            member.value();

        record_offset member_offset = 0;

        if (bindings && !bindings->member_offset(
                global,
                member_offset)) {

            return member_search_result::invalid;
        }

        output.offset =
            static_cast<runtime_offset>(member_offset);

        output.type = record.type;
        return member_search_result::found;
    }

    member_search_result state =
        member_search_result::none;

    resolved_member selected;

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
            (bindings && (base.virtual_base() ||
                !bindings->base_offset(global, base_offset)))) {

            return member_search_result::invalid;
        }

        const auto base_handle =
            project.find_type(
                base.type);

        if (!base_handle) {
            return member_search_result::invalid;
        }

        resolved_member candidate;

        const auto found =
            find_member_recursive(
                project,
                bindings,
                base_handle,
                name,
                depth + 1,
                candidate);

        if (found == member_search_result::invalid ||
            found == member_search_result::ambiguous) {

            return found;
        }

        if (found != member_search_result::found) {
            continue;
        }

        runtime_offset combined = 0;

        if (!add_offset(
                static_cast<runtime_offset>(
                    base_offset),
                candidate.offset,
                combined)) {

            return member_search_result::invalid;
        }

        candidate.offset = combined;

        if (state == member_search_result::found) {
            return member_search_result::ambiguous;
        }

        selected = candidate;
        state = member_search_result::found;
    }

    if (state == member_search_result::found) {
        output = selected;
    }

    return state;
}

[[nodiscard]] const char* intrinsic_type_name(
    intrinsic_type type) noexcept {

    switch (type) {
    case intrinsic_type::void_type:
        return "void";
    case intrinsic_type::bool_type:
        return "bool";
    case intrinsic_type::char_type:
        return "char";
    case intrinsic_type::signed_char:
        return "signed char";
    case intrinsic_type::unsigned_char:
        return "unsigned char";
    case intrinsic_type::wchar_type:
        return "wchar_t";
    case intrinsic_type::char8_type:
        return "char8_t";
    case intrinsic_type::char16_type:
        return "char16_t";
    case intrinsic_type::char32_type:
        return "char32_t";
    case intrinsic_type::signed_short:
        return "short";
    case intrinsic_type::unsigned_short:
        return "unsigned short";
    case intrinsic_type::signed_int:
        return "int";
    case intrinsic_type::unsigned_int:
        return "unsigned int";
    case intrinsic_type::signed_long:
        return "long";
    case intrinsic_type::unsigned_long:
        return "unsigned long";
    case intrinsic_type::signed_long_long:
        return "long long";
    case intrinsic_type::unsigned_long_long:
        return "unsigned long long";
    case intrinsic_type::float_type:
        return "float";
    case intrinsic_type::double_type:
        return "double";
    case intrinsic_type::long_double_type:
        return "long double";
    case intrinsic_type::nullptr_type:
        return "std::nullptr_t";
    case intrinsic_type::none:
        break;
    }

    return nullptr;
}

[[nodiscard]] bool qualified_identity_name(
    const compiled_project_view& project,
    identity_ref identity,
    std::string& output,
    std::vector<identity_ref>& scratch) {

    output.clear();
    scratch.clear();

    const auto root =
        project.identity_root();

    if (!root ||
        !identity) {

        return false;
    }

    auto current = identity;

    for (std::size_t depth = 0;
         depth <= project.identity_count();
         ++depth) {

        if (current == root) {
            break;
        }

        if (!current) {
            return false;
        }

        scratch.push_back(
            current);

        current =
            project.identity_parent(
                current);
    }

    if (current != root ||
        scratch.empty()) {

        return false;
    }

    for (auto iterator = scratch.rbegin();
         iterator != scratch.rend();
         ++iterator) {

        const auto name =
            project.identity_name(
                *iterator);

        if (!name) {
            return false;
        }

        const auto value =
            project.string(name);

        if (value.empty()) {
            return false;
        }

        if (!output.empty()) {
            output += "::";
        }

        output.append(
            value.data(),
            value.size());
    }

    return !output.empty();
}

[[nodiscard]] bool format_type_name(
    const compiled_project_view& project,
    type_ref type,
    std::string& output,
    std::vector<identity_ref>& scratch,
    std::size_t depth = 0) {

    output.clear();

    if (!type ||
        depth >
            project.derived_type_count() + 1) {

        return false;
    }

    switch (type.kind()) {
    case type_ref_kind::intrinsic: {
        const auto* name =
            intrinsic_type_name(
                static_cast<intrinsic_type>(
                    type.payload()));

        if (name == nullptr) {
            return false;
        }

        output = name;
        return true;
    }

    case type_ref_kind::named: {
        if (type.payload() == 0) {
            return false;
        }

        type_handle handle;

        if (!project.named(
                type,
                handle)) {

            return false;
        }

        const auto identity =
            project.identity(
                handle);

        return qualified_identity_name(
            project,
            identity,
            output,
            scratch);
    }

    case type_ref_kind::derived: {
        derived_type_record derived;

        if (!project.derived(
                type,
                derived)) {

            return false;
        }

        std::string child;

        if (!format_type_name(
                project,
                derived.child,
                child,
                scratch,
                depth + 1)) {

            return false;
        }

        switch (derived.kind) {
        case derived_type_kind::const_qualified:
            output = "const ";
            output += child;
            return true;

        case derived_type_kind::volatile_qualified:
            output = "volatile ";
            output += child;
            return true;

        case derived_type_kind::pointer:
            output = std::move(child);
            output += "*";
            return true;

        case derived_type_kind::lvalue_reference:
            output = std::move(child);
            output += "&";
            return true;

        case derived_type_kind::rvalue_reference:
            output = std::move(child);
            output += "&&";
            return true;

        case derived_type_kind::bounded_array:
            output = std::move(child);
            output += "[";
            output += std::to_string(
                derived.payload);
            output += "]";
            return true;

        case derived_type_kind::unbounded_array:
            output = std::move(child);
            output += "[]";
            return true;
        }

        return false;
    }

    case type_ref_kind::invalid:
        return false;
    }

    return false;
}

[[nodiscard]] bool glob_match(
    std::string_view pattern,
    std::string_view value) noexcept {

    std::size_t pattern_index = 0;
    std::size_t value_index = 0;
    std::size_t star = std::string_view::npos;
    std::size_t retry = 0;

    while (value_index < value.size()) {
        if (pattern_index < pattern.size() &&
            (pattern[pattern_index] == '?' ||
             pattern[pattern_index] ==
                 value[value_index])) {

            ++pattern_index;
            ++value_index;
            continue;
        }

        if (pattern_index < pattern.size() &&
            pattern[pattern_index] == '*') {

            star = pattern_index++;
            retry = value_index;
            continue;
        }

        if (star != std::string_view::npos) {
            pattern_index = star + 1;
            value_index = ++retry;
            continue;
        }

        return false;
    }

    while (pattern_index < pattern.size() &&
           pattern[pattern_index] == '*') {

        ++pattern_index;
    }

    return pattern_index == pattern.size();
}

[[nodiscard]] bool wildcard_query(
    std::string_view name) noexcept {

    return name.find_first_of("*?") !=
        std::string_view::npos;
}

[[nodiscard]] bool type_filter_matches(
    std::string_view filter,
    std::string_view actual) noexcept {

    if (filter.empty()) {
        return true;
    }

    if (filter.starts_with("::")) {
        filter.remove_prefix(2);
    }

    return filter == actual;
}

struct runtime_system_member_descriptor final {
    std::string_view name;
    std::string_view type_name;
    intrinsic_type type = intrinsic_type::none;
    std::uint8_t size = 0;
    std::size_t offset = 0;
};

inline constexpr std::array<
    runtime_system_member_descriptor,
    10>
runtime_system_members{{
    {
        "state",
        "std::uint32_t",
        intrinsic_type::unsigned_int,
        sizeof(std::uint32_t),
        offsetof(runtime_system, state),
    },
    {
        "flags",
        "std::uint32_t",
        intrinsic_type::unsigned_int,
        sizeof(std::uint32_t),
        offsetof(runtime_system, flags),
    },
    {
        "current_cycle",
        "std::uint64_t",
        intrinsic_type::unsigned_long_long,
        sizeof(std::uint64_t),
        offsetof(runtime_system, current_cycle),
    },
    {
        "completed_cycle",
        "std::uint64_t",
        intrinsic_type::unsigned_long_long,
        sizeof(std::uint64_t),
        offsetof(runtime_system, completed_cycle),
    },
    {
        "target_cycle",
        "std::uint64_t",
        intrinsic_type::unsigned_long_long,
        sizeof(std::uint64_t),
        offsetof(runtime_system, target_cycle),
    },
    {
        "server_datetime_ns",
        "std::int64_t",
        intrinsic_type::signed_long_long,
        sizeof(std::int64_t),
        offsetof(runtime_system, server_datetime_ns),
    },
    {
        "model_datetime_ns",
        "std::int64_t",
        intrinsic_type::signed_long_long,
        sizeof(std::int64_t),
        offsetof(runtime_system, model_datetime_ns),
    },
    {
        "value_generation",
        "std::uint64_t",
        intrinsic_type::unsigned_long_long,
        sizeof(std::uint64_t),
        offsetof(runtime_system, value_generation),
    },
    {
        "snapshot_generation",
        "std::uint64_t",
        intrinsic_type::unsigned_long_long,
        sizeof(std::uint64_t),
        offsetof(runtime_system, snapshot_generation),
    },
    {
        "current_ic_generation",
        "std::uint64_t",
        intrinsic_type::unsigned_long_long,
        sizeof(std::uint64_t),
        offsetof(runtime_system, current_ic_generation),
    },
}};

[[nodiscard]] bool runtime_system_name(
    std::string_view value) noexcept {

    if (value.starts_with("::")) {
        value.remove_prefix(2);
    }

    return value ==
        runtime_system_object_name;
}

[[nodiscard]] bool build_runtime_system_type(
    runtime_object_type& output) {

    output = {};
    output.object =
        runtime_system_object_name;

    output.type.name =
        runtime_system_object_name;

    output.type.members.reserve(
        runtime_system_members.size());

    for (const auto& member :
         runtime_system_members) {

        output.type.members.push_back({
            std::string(member.name),
            std::string(member.type_name),
        });
    }

    return true;
}

[[nodiscard]] runtime_query_result
read_runtime_system_value(
    std::span<const std::byte> runtime,
    std::string_view member_name,
    runtime_value& output) noexcept {

    output = {};

    if (runtime.size() <
        sizeof(runtime_system)) {

        return runtime_query_result::
            invalid_runtime;
    }

    for (const auto& member :
         runtime_system_members) {

        if (member.name !=
            member_name) {

            continue;
        }

        output.type =
            member.type;

        output.size =
            member.size;

        return read_native_bits(
            runtime,
            static_cast<runtime_offset>(
                member.offset),
            member.size,
            output.bits);
    }

    return runtime_query_result::not_found;
}

[[nodiscard]] bool build_type_spec(
    const compiled_project_view& project,
    type_ref type,
    runtime_type_spec& output,
    std::vector<identity_ref>& identity_scratch) {

    output = {};

    if (!format_type_name(
            project,
            type,
            output.name,
            identity_scratch)) {

        return false;
    }

    if (type.kind() !=
        type_ref_kind::named) {

        return true;
    }

    if (type.payload() == 0) {
        return false;
    }

    type_handle handle;

    if (!project.named(
            type,
            handle)) {

        return false;
    }

    type_entry entry;

    if (!project.type(
            handle,
            entry)) {

        return false;
    }

    output.bases.reserve(
        entry.bases.count);

    for (std::uint32_t index = 0;
         index < entry.bases.count;
         ++index) {

        base_record base;

        if (!project.base_at(
                static_cast<std::size_t>(
                    entry.bases.begin) +
                    index,
                base)) {

            return false;
        }

        std::string base_name;

        if (!qualified_identity_name(
                project,
                base.type,
                base_name,
                identity_scratch)) {

            return false;
        }

        output.bases.push_back(
            std::move(base_name));
    }

    output.members.reserve(
        entry.members.count);

    for (std::uint32_t index = 0;
         index < entry.members.count;
         ++index) {

        member_record member;

        if (!project.member(
                handle,
                index,
                member)) {

            return false;
        }

        const auto member_name =
            project.string(
                member.name);

        if (member_name.empty()) {
            return false;
        }

        runtime_type_member value;

        value.name.assign(
            member_name.data(),
            member_name.size());

        if (!format_type_name(
                project,
                member.type,
                value.type,
                identity_scratch)) {

            return false;
        }

        output.members.push_back(
            std::move(value));
    }

    return true;
}

[[nodiscard]] bool append_endpoint_member(
    const compiled_project_view& project,
    type_ref& type,
    std::uint32_t local,
    std::string& output) {

    derived_type_record derived;

    while (project.derived(
               type,
               derived) &&
           (derived.kind ==
                derived_type_kind::const_qualified ||
            derived.kind ==
                derived_type_kind::volatile_qualified)) {

        type = derived.child;
    }

    if (type.kind() !=
            type_ref_kind::named ||
        type.payload() == 0) {

        return false;
    }

    type_handle owner;

    member_record member;

    if (!project.named(
            type,
            owner) ||
        !project.member(
            owner,
            local,
            member)) {

        return false;
    }

    const auto name =
        project.string(
            member.name);

    if (name.empty()) {
        return false;
    }

    output.push_back('.');
    output.append(
        name.data(),
        name.size());

    type = member.type;
    return true;
}

[[nodiscard]] bool format_endpoint_name(
    const compiled_project_view& project,
    object_endpoint endpoint,
    std::string& output,
    std::vector<identity_ref>& identity_scratch) {

    output.clear();

    const auto object_handle_value =
        project.find_object(
            endpoint.object);

    object_entry object;

    if (!endpoint.object ||
        endpoint.object.kind() !=
            identity_kind::object ||
        !endpoint.member ||
        !object_handle_value ||
        !project.object(
            object_handle_value,
            object) ||
        !qualified_identity_name(
            project,
            endpoint.object,
            output,
            identity_scratch)) {

        return false;
    }

    auto type =
        object.type;

    if (endpoint.member.is_member()) {
        return append_endpoint_member(
            project,
            type,
            endpoint.member.direct_member().value(),
            output);
    }

    endpoint_path_record path;

    if (!project.endpoint_path(
            endpoint.member.path(),
            path) ||
        path.root_type !=
            object.type) {

        return false;
    }

    for (std::uint32_t local = 0;
         local < path.steps.count;
         ++local) {

        endpoint_path_step step;

        if (!project.endpoint_path_step_at(
                static_cast<std::size_t>(
                    path.steps.begin) +
                    local,
                step)) {

            return false;
        }

        if (step.kind == endpoint_path_step_kind::dereference) {
            derived_type_record reference;
            if (step.value != 0 || !project.derived(type, reference) ||
                (reference.kind != derived_type_kind::lvalue_reference && reference.kind != derived_type_kind::rvalue_reference)) { return false; }
            type = reference.child;
            continue;
        }
        if (step.kind == endpoint_path_step_kind::base) {
            type_handle record;
            type_entry entry;
            base_record base;
            if (!project.named(type, record) || !project.type(record, entry) || step.value >= entry.bases.count ||
                !project.base_at(entry.bases.begin + static_cast<std::size_t>(step.value), base)) { return false; }
            type = project.named(project.find_type(base.type));
            continue;
        }
        if (step.kind ==
            endpoint_path_step_kind::member) {

            if (step.value >
                (std::numeric_limits<std::uint32_t>::max)() ||
                !append_endpoint_member(
                    project,
                    type,
                    static_cast<std::uint32_t>(
                        step.value),
                    output)) {

                return false;
            }

            continue;
        }

        derived_type_record derived;

        while (project.derived(
                   type,
                   derived) &&
               (derived.kind ==
                    derived_type_kind::const_qualified ||
                derived.kind ==
                    derived_type_kind::volatile_qualified)) {

            type = derived.child;
        }

        if (step.kind == endpoint_path_step_kind::bit_index) {
            output.push_back('.');
            output += std::to_string(step.value);
            return local + 1 == path.steps.count;
        }
        if (step.kind !=
                endpoint_path_step_kind::array_index ||
            !project.derived(
                type,
                derived) ||
            derived.kind !=
                derived_type_kind::bounded_array ||
            step.value >=
                derived.payload) {

            return false;
        }

        output.push_back('[');
        output +=
            std::to_string(
                step.value);
        output.push_back(']');

        type = derived.child;
    }

    return true;
}

[[nodiscard]] std::string_view assign_object_name(
    std::string_view endpoint) noexcept {

    if (endpoint.starts_with("::")) {
        endpoint.remove_prefix(2);
    }

    const auto dot =
        endpoint.find('.');

    return endpoint.substr(
        0,
        dot);
}

[[nodiscard]] bool assign_matches_object(
    std::string_view endpoint,
    std::string_view object) noexcept {

    return !endpoint.empty() &&
        assign_object_name(
            endpoint) ==
        object;
}

// Explicit Source links override Header defaults. Keep the first contribution
// within each kind so a surviving page binding can be opened from Find Link.
[[nodiscard]] bool prefer_link_contribution(
    const compiled_project_view& project, file_id current, file_id candidate) noexcept {

    if (!current) return true;
    if (current == candidate) return false;
    std::string_view path;
    file_kind kind{};
    source_map_range range;
    return project.source_file(current, path, kind, range) && kind == file_kind::header &&
        project.source_file(candidate, path, kind, range) && kind == file_kind::source;
}

[[nodiscard]] bool query_file_path(
    const compiled_project_view& project,
    file_id file,
    file_kind expected,
    std::string& output) {

    output.clear();

    std::string_view path;
    file_kind kind{};
    source_map_range range;

    if (!file ||
        !project.source_file(
            file,
            path,
            kind,
            range) ||
        (kind != expected && !(expected == file_kind::source && kind == file_kind::header)) ||
        path.empty()) {

        return false;
    }

    output.assign(
        path.data(),
        path.size());

    return true;
}


[[nodiscard]] runtime_query_result resolve_object(
    const compiled_project_view& project,
    std::string_view qualified,
    object_handle& output) noexcept {

    output = {};

    if (qualified.starts_with("::")) {
        qualified.remove_prefix(2);
    }

    if (qualified.empty()) {
        return runtime_query_result::invalid_input;
    }

    auto parent =
        project.identity_root();

    if (!parent) {
        return runtime_query_result::invalid_runtime;
    }

    std::size_t begin = 0;

    for (;;) {
        const auto separator =
            qualified.find(
                "::",
                begin);

        const auto last =
            separator == std::string_view::npos;

        const auto end =
            last ? qualified.size() : separator;

        if (end == begin) {
            return runtime_query_result::invalid_input;
        }

        const auto string =
            project.find_string(
                qualified.substr(
                    begin,
                    end - begin));

        if (!string) {
            return runtime_query_result::not_found;
        }

        const auto identity =
            project.find_identity(
                parent,
                string,
                last
                    ? identity_kind::object
                    : identity_kind::namespace_scope);

        if (!identity) {
            return runtime_query_result::not_found;
        }

        if (last) {
            output =
                project.find_object(
                    identity);

            return output
                ? runtime_query_result::success
                : runtime_query_result::not_found;
        }

        parent = identity;
        begin = separator + 2;

        if (begin >= qualified.size()) {
            return runtime_query_result::invalid_input;
        }
    }
}

}

runtime_query_result get_runtime_object(
    const compiled_project_view& project,
    std::string_view name,
    std::string_view type,
    runtime_object_query& output) noexcept {

    output = {};

    if (!project.valid() ||
        name.empty()) {

        return runtime_query_result::invalid_input;
    }

    if (name.starts_with("::")) {
        name.remove_prefix(2);
    }

    if (name.empty()) {
        return runtime_query_result::invalid_input;
    }

    try {
        std::vector<identity_ref> identity_scratch;

        if (!wildcard_query(name) &&
            runtime_system_name(name)) {

            if (!type_filter_matches(
                    type,
                    runtime_system_object_name)) {

                return runtime_query_result::
                    not_found;
            }

            output.name =
                runtime_system_object_name;

            output.type =
                runtime_system_object_name;

            return runtime_query_result::success;
        }

        if (!wildcard_query(name)) {
            object_handle object;

            const auto resolved =
                resolve_object(
                    project,
                    name,
                    object);

            if (resolved !=
                runtime_query_result::success) {

                return resolved;
            }

            object_entry record;

            if (!project.object(
                    object,
                    record)) {

                return runtime_query_result::
                    invalid_runtime;
            }

            if (!qualified_identity_name(
                    project,
                    project.identity(object),
                    output.name,
                    identity_scratch) ||
                !format_type_name(
                    project,
                    record.type,
                    output.type,
                    identity_scratch)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            if (!type_filter_matches(
                    type,
                    output.type)) {

                output = {};
                return runtime_query_result::
                    not_found;
            }

            const auto who = project.identity(object);
            for (std::size_t i = 0; i < project.source_contribution_count(); ++i) {
                source_contribution_record contribution;
                if (!project.source_contribution(static_cast<std::uint32_t>(i), contribution))
                    return runtime_query_result::invalid_runtime;
                if (contribution.data.kind() == source_data_kind::object &&
                    project.identity_at_slot(contribution.data.slot()) == who) {
                    std::string_view path;
                    file_kind kind{};
                    source_map_range range;
                    if (!project.source_file(contribution.file, path, kind, range))
                        return runtime_query_result::invalid_runtime;
                    output.file.assign(path);
                    break;
                }
            }
            return runtime_query_result::success;
        }

        output.pattern = true;

        if (glob_match(
                name,
                runtime_system_object_name) &&
            type_filter_matches(
                type,
                runtime_system_object_name)) {

            runtime_object_match system;
            system.name =
                runtime_system_object_name;

            output.objects.push_back(
                std::move(system));
        }

        std::vector<file_id> object_files(
            project.object_count());

        const auto contribution_count =
            project.source_contribution_count();

        if (contribution_count >
            (std::numeric_limits<std::uint32_t>::max)()) {

            output = {};
            return runtime_query_result::
                invalid_runtime;
        }

        for (std::size_t index = 0;
             index < contribution_count;
             ++index) {

            source_contribution_record contribution;

            if (!project.source_contribution(
                    static_cast<std::uint32_t>(
                        index),
                    contribution)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            if (contribution.data.kind() !=
                source_data_kind::object) {

                continue;
            }

            const auto identity =
                project.identity_at_slot(
                    contribution.data.slot());

            if (!identity ||
                identity.kind() !=
                    identity_kind::object) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            const auto object =
                project.find_object(
                    identity);

            if (!object ||
                object.value() >
                    object_files.size()) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            auto& file =
                object_files[
                    object.value() - 1];

            if (!file) {
                file = contribution.file;
                continue;
            }

            if (file != contribution.file) {
                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }
        }

        std::string object_name;
        std::string object_type;

        for (std::size_t index = 0;
             index < project.object_count();
             ++index) {

            const auto object =
                project.object_at(index);

            object_entry record;

            if (!object ||
                !project.object(
                    object,
                    record) ||
                !qualified_identity_name(
                    project,
                    project.identity(object),
                    object_name,
                    identity_scratch)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            if (!glob_match(
                    name,
                    object_name)) {

                continue;
            }

            if (!format_type_name(
                    project,
                    record.type,
                    object_type,
                    identity_scratch)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            if (!type_filter_matches(
                    type,
                    object_type)) {

                continue;
            }

            const auto file =
                object_files[index];

            if (!file) {
                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            std::string_view file_path;
            file_kind file_type{};
            source_map_range contributions;

            if (!project.source_file(
                    file,
                    file_path,
                    file_type,
                    contributions) ||
                file_path.empty()) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            runtime_object_match match;
            match.name = object_name;
            match.file.assign(
                file_path.data(),
                file_path.size());

            output.objects.push_back(
                std::move(match));
        }

        return runtime_query_result::success;
    }
    catch (...) {
        output = {};
        return runtime_query_result::
            invalid_runtime;
    }
}

static void unwrap_endpoint_type(const compiled_project_view& project, type_ref& type) noexcept {
    derived_type_record derived;
    while (project.derived(type, derived)) {
        if (derived.kind != derived_type_kind::const_qualified &&
            derived.kind != derived_type_kind::volatile_qualified &&
            derived.kind != derived_type_kind::lvalue_reference &&
            derived.kind != derived_type_kind::rvalue_reference) {
            return;
        }
        type = derived.child;
    }
}

static bool indexed_record_array(const compiled_project_view& project,
    type_ref type, type_ref& array_type, std::size_t& member_offset_index, std::int32_t& origin) noexcept {
    type_handle handle;
    type_entry record;
    if (!project.named(type, handle) || !project.type(handle, record) || !record.index_member_slot())
        return false;

    const auto index = static_cast<std::uint32_t>(record.index_member_slot() - 1);
    member_record member;
    derived_type_record array;
    if (!project.member(handle, index, member) || !project.derived(member.type, array) ||
        array.kind != derived_type_kind::bounded_array) return false;
    origin = array_origin(member.reserved);
    array_type = member.type;
    member_offset_index = static_cast<std::size_t>(record.members.begin) + index;
    return true;
}

static runtime_query_result resolve_endpoint_type(const compiled_project_view& project,
    std::string_view path, type_ref& type) noexcept {
    while (!path.empty()) {
        unwrap_endpoint_type(project, type);
        if (path.front() == '[') {
            const auto close = path.find(']');
            if (close == std::string_view::npos) return runtime_query_result::invalid_input;
            const auto digits = path.substr(1, close - 1);
            std::int64_t logical_index = 0;
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), logical_index);
            if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
                return runtime_query_result::invalid_input;
            std::int32_t origin = 0;
            derived_type_record array;
            if (!project.derived(type, array) || array.kind != derived_type_kind::bounded_array) {
                std::size_t ignored;
                if (!indexed_record_array(project, type, type, ignored, origin) || !project.derived(type, array))
                    return runtime_query_result::unsupported_type;
            }
            std::uint64_t index;
            if (!array_offset(logical_index, origin, array.payload, index)) return runtime_query_result::not_found;
            type = array.child;
            path.remove_prefix(close + 1);
            continue;
        }

        if (path.front() != '.') return runtime_query_result::invalid_input;
        path.remove_prefix(1);
        const auto length = path.find_first_of(".[");
        const auto name = path.substr(0, length);
        if (name.empty()) return runtime_query_result::invalid_input;
        type_handle owner;
        type_entry record;
        if (!project.named(type, owner) || !project.type(owner, record) ||
            record.kind != graph_type_kind::record) return runtime_query_result::unsupported_type;
        const auto id = project.find_string(name);
        if (!id) return runtime_query_result::not_found;
        resolved_member member;
        switch (find_member_recursive(project, nullptr, owner, id, 0, member)) {
        case member_search_result::none: return runtime_query_result::not_found;
        case member_search_result::ambiguous: return runtime_query_result::invalid_input;
        case member_search_result::invalid: return runtime_query_result::invalid_runtime;
        case member_search_result::found: type = member.type; break;
        }
        path.remove_prefix(name.size());
    }
    unwrap_endpoint_type(project, type);
    return runtime_query_result::success;
}

runtime_query_result get_runtime_type(
    const compiled_project_view& project,
    std::span<const std::string> objects,
    runtime_type_query& output) noexcept {

    output = {};

    if (!project.valid() ||
        objects.empty()) {

        return runtime_query_result::invalid_input;
    }

    try {
        output.types.reserve(
            objects.size());

        std::vector<identity_ref>
            identity_scratch;

        for (const auto& requested :
             objects) {

            if (requested.empty()) {
                output = {};
                return runtime_query_result::
                    invalid_input;
            }

            if (runtime_system_name(
                    requested)) {

                runtime_object_type result;

                if (!build_runtime_system_type(
                        result)) {

                    output = {};
                    return runtime_query_result::
                        invalid_runtime;
                }

                output.types.push_back(
                    std::move(result));

                continue;
            }

            const auto separator = requested.find_first_of(".[");
            object_handle object;

            const auto resolved =
                resolve_object(
                    project,
                    std::string_view{requested}.substr(0, separator),
                    object);

            if (resolved !=
                runtime_query_result::success) {

                output = {};
                return resolved;
            }

            object_entry entry;

            if (!project.object(
                    object,
                    entry)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            auto type = entry.type;
            if (separator != std::string_view::npos) {
                const auto status = resolve_endpoint_type(project, std::string_view{requested}.substr(separator), type);
                if (status != runtime_query_result::success) {
                    output = {};
                    return status;
                }
            }

            runtime_object_type result;

            if (!qualified_identity_name(
                    project,
                    project.identity(object),
                    result.object,
                    identity_scratch) ||
                !build_type_spec(
                    project,
                    type,
                    result.type,
                    identity_scratch)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            if (separator != std::string_view::npos) result.object.append(requested, separator);
            output.types.push_back(
                std::move(result));
        }

        return runtime_query_result::success;
    }
    catch (...) {
        output = {};
        return runtime_query_result::
            invalid_runtime;
    }
}

runtime_query_result get_runtime_link(
    const compiled_project_view& project,
    std::span<const std::string> objects,
    runtime_link_query& output) noexcept {

    output = {};

    if (!project.valid() ||
        objects.empty()) {

        return runtime_query_result::invalid_input;
    }

    try {
        std::vector<object_handle> handles;
        handles.reserve(
            objects.size());

        output.objects.reserve(
            objects.size());

        std::vector<identity_ref>
            identity_scratch;

        for (const auto& requested :
             objects) {

            if (requested.empty()) {
                output = {};
                return runtime_query_result::
                    invalid_input;
            }

            object_handle object;

            const auto resolved =
                resolve_object(
                    project,
                    requested,
                    object);

            if (resolved !=
                runtime_query_result::success) {

                output = {};
                return resolved;
            }

            runtime_object_links result;

            if (!qualified_identity_name(
                    project,
                    project.identity(object),
                    result.object,
                    identity_scratch)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            handles.push_back(
                object);

            output.objects.push_back(
                std::move(result));
        }

        std::vector<file_id> link_files(
            project.link_count());

        const auto contribution_count =
            project.source_contribution_count();

        if (contribution_count >
            (std::numeric_limits<std::uint32_t>::max)()) {

            output = {};
            return runtime_query_result::
                invalid_runtime;
        }

        for (std::size_t index = 0;
             index < contribution_count;
             ++index) {

            source_contribution_record contribution;

            if (!project.source_contribution(
                    static_cast<std::uint32_t>(
                        index),
                    contribution)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            if (contribution.data.kind() !=
                source_data_kind::link) {

                continue;
            }

            const auto slot =
                contribution.data.slot();

            if (slot == 0 ||
                slot >
                    link_files.size()) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            auto& file =
                link_files[
                    slot - 1];

            if (prefer_link_contribution(project, file, contribution.file))
                file = contribution.file;

        }

        for (std::size_t group = 0;
             group < handles.size();
             ++group) {

            const auto selected =
                handles[group];

            const auto selected_identity =
                project.identity(
                    selected);

            if (!selected_identity) {
                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            auto& result =
                output.objects[group];

            for (std::size_t index = 0;
                 index < project.link_count();
                 ++index) {

                const auto handle =
                    project.link_at(index);

                link_record link;

                if (!handle ||
                    !project.link(
                        handle,
                        link)) {

                    output = {};
                    return runtime_query_result::
                        invalid_runtime;
                }

                if (link.source.object !=
                        selected_identity &&
                    link.target.object !=
                        selected_identity) {

                    continue;
                }

                runtime_link_match match;

                if (!format_endpoint_name(
                        project,
                        link.source,
                        match.source,
                        identity_scratch) ||
                    !format_endpoint_name(
                        project,
                        link.target,
                        match.target,
                        identity_scratch) ||
                    index >=
                        link_files.size() ||
                    !query_file_path(
                        project,
                        link_files[index],
                        file_kind::source,
                        match.file)) {

                    output = {};
                    return runtime_query_result::
                        invalid_runtime;
                }

                result.links.push_back(
                    std::move(match));
            }

            for (std::size_t index = 0;
                 index < project.assign_count();
                 ++index) {

                std::string_view source;
                std::string_view target;

                if (!project.assign(
                        index,
                        source,
                        target)) {

                    output = {};
                    return runtime_query_result::
                        invalid_runtime;
                }

                if (!assign_matches_object(
                        source,
                        result.object) &&
                    !assign_matches_object(
                        target,
                        result.object)) {

                    continue;
                }

                file_id file;

                runtime_assign_match match;

                if (!project.assign_file(
                        index,
                        file) ||
                    !query_file_path(
                        project,
                        file,
                        file_kind::assign,
                        match.file)) {

                    output = {};
                    return runtime_query_result::
                        invalid_runtime;
                }

                match.source.assign(
                    source.data(),
                    source.size());

                match.target.assign(
                    target.data(),
                    target.size());

                result.assigns.push_back(
                    std::move(match));
            }
        }

        return runtime_query_result::success;
    }
    catch (...) {
        output = {};
        return runtime_query_result::
            invalid_runtime;
    }
}


runtime_query_result prepare_runtime_link_index(
    const compiled_project_view& project, runtime_link_index& index, std::stop_token stop) {
    if (!project.valid()) return runtime_query_result::invalid_runtime;
    if (stop.stop_requested()) return runtime_query_result::invalid_input;
    if (index.owner == &project) return runtime_query_result::success;
            runtime_link_index built;
            const auto n = project.object_count(), l = project.link_count(), a = project.assign_count();
            constexpr auto limit = (std::numeric_limits<std::uint32_t>::max)();
            if (n > limit-2 || l > limit || a > limit || project.source_contribution_count() > limit)
                return runtime_query_result::invalid_runtime;
            built.offsets.resize(n+2);
            built.files.resize(l);
            if (a) built.assign_offsets.resize(n+2);
            auto endpoints = [&](const link_record& link, object_handle& source, object_handle& target) {
                source = project.find_object(link.source.object); target = project.find_object(link.target.object);
                return source && target && source.value() <= n && target.value() <= n;
            };
            for (std::size_t i=0;i<l;++i) {
                if ((i & 4095u)==0 && stop.stop_requested()) return runtime_query_result::invalid_input;
                link_record link; object_handle source, target;
                if (!project.link(project.link_at(i),link) || !endpoints(link,source,target)) return runtime_query_result::invalid_runtime;
                if (built.offsets[source.value()+1] == limit) return runtime_query_result::invalid_runtime;
                ++built.offsets[source.value()+1];
                if (source != target) ++built.offsets[target.value()+1];
            }
            auto assign_objects = [&](std::size_t i, object_handle& source, object_handle& target) {
                std::string_view from,to;
                if (!project.assign(i,from,to)) return false;
                // Unresolved names cannot match a valid requested object in the linear path.
                const auto fs = resolve_object(project,assign_object_name(from),source);
                const auto ts = resolve_object(project,assign_object_name(to),target);
                if (fs == runtime_query_result::invalid_runtime || ts == runtime_query_result::invalid_runtime) return false;
                if (fs != runtime_query_result::success) source = {};
                if (ts != runtime_query_result::success) target = {};
                return true;
            };
            for (std::size_t i=0;i<a;++i) {
                if ((i & 4095u)==0 && stop.stop_requested()) return runtime_query_result::invalid_input;
                object_handle source,target;
                if (!assign_objects(i,source,target)) return runtime_query_result::invalid_runtime;
                if (source) ++built.assign_offsets[source.value()+1];
                if (target && source != target) ++built.assign_offsets[target.value()+1];
            }
            auto prefix = [&](std::vector<std::uint32_t>& offsets, std::vector<std::uint32_t>& incidents) {
                std::uint64_t sum=0;
                for (auto& count:offsets) { sum+=count; if(sum>limit) return false; count=static_cast<std::uint32_t>(sum); }
                incidents.resize(static_cast<std::size_t>(sum)); return true;
            };
            if (!prefix(built.offsets,built.incidents) || (a && !prefix(built.assign_offsets,built.assign_incidents))) return runtime_query_result::invalid_runtime;
            {
                auto cursor=built.offsets;
                for (std::size_t i=0;i<l;++i) {
                if ((i & 4095u)==0 && stop.stop_requested()) return runtime_query_result::invalid_input;
                    link_record link; object_handle source,target;
                    if (!project.link(project.link_at(i),link) || !endpoints(link,source,target)) return runtime_query_result::invalid_runtime;
                    built.incidents[cursor[source.value()]++]=static_cast<std::uint32_t>(i);
                    if (source!=target) built.incidents[cursor[target.value()]++]=static_cast<std::uint32_t>(i);
                }
            }
            if (a) {
                auto cursor=built.assign_offsets;
                for (std::size_t i=0;i<a;++i) {
                if ((i & 4095u)==0 && stop.stop_requested()) return runtime_query_result::invalid_input;
                    object_handle source,target;
                    if (!assign_objects(i,source,target)) return runtime_query_result::invalid_runtime;
                    if (source) built.assign_incidents[cursor[source.value()]++]=static_cast<std::uint32_t>(i);
                    if (target && source!=target) built.assign_incidents[cursor[target.value()]++]=static_cast<std::uint32_t>(i);
                }
            }
            for (std::size_t i=0;i<project.source_contribution_count();++i) {
                if ((i & 4095u)==0 && stop.stop_requested()) return runtime_query_result::invalid_input;
                source_contribution_record contribution;
                if (!project.source_contribution(static_cast<std::uint32_t>(i),contribution)) return runtime_query_result::invalid_runtime;
                if (contribution.data.kind()!=source_data_kind::link) continue;
                const auto slot=contribution.data.slot();
                if (!slot || slot>l) return runtime_query_result::invalid_runtime;
                auto& file = built.files[slot - 1];
                if (prefer_link_contribution(project, file_id{file}, contribution.file))
                    file = contribution.file.value();
            }
            if (stop.stop_requested()) return runtime_query_result::invalid_input;
            built.owner=&project;
            index=std::move(built); // Publish only a complete index; failures leave prior cache intact.
    return runtime_query_result::success;
}

runtime_query_result get_runtime_link_indexed(
    const compiled_project_view& project, std::span<const std::string> objects,
    runtime_link_query& output, runtime_link_index& index) noexcept {
    output = {};
    if (!project.valid() || objects.empty()) return runtime_query_result::invalid_input;
    try {
        std::vector<object_handle> selected;
        std::vector<identity_ref> scratch;
        for (const auto& name : objects) {
            object_handle handle;
            const auto status = resolve_object(project, name, handle);
            if (status != runtime_query_result::success) return status;
            selected.push_back(handle);
        }
        if (index.owner != &project && prepare_runtime_link_index(project,index) != runtime_query_result::success)
            return get_runtime_link(project,objects,output);
        for (const auto handle:selected) {
            runtime_object_links group;
            if (!qualified_identity_name(project,project.identity(handle),group.object,scratch)) return runtime_query_result::invalid_runtime;
            const auto slot=handle.value();
            for (auto p=index.offsets[slot];p<index.offsets[slot+1];++p) {
                const auto ordinal=index.incidents[p]; link_record link; runtime_link_match match;
                if (!project.link(project.link_at(ordinal),link) ||
                    !format_endpoint_name(project,link.source,match.source,scratch) ||
                    !format_endpoint_name(project,link.target,match.target,scratch) ||
                    !query_file_path(project,file_id{index.files[ordinal]},file_kind::source,match.file)) {
                    output={}; return runtime_query_result::invalid_runtime;
                }
                group.links.push_back(std::move(match));
            }
            if (!index.assign_offsets.empty()) for (auto p=index.assign_offsets[slot];p<index.assign_offsets[slot+1];++p) {
                const auto ordinal=index.assign_incidents[p]; runtime_assign_match match;
                std::string_view source,target; file_id file;
                if (!project.assign(ordinal,source,target) || !project.assign_file(ordinal,file) ||
                    !query_file_path(project,file,file_kind::assign,match.file)) { output={}; return runtime_query_result::invalid_runtime; }
                match.source=source; match.target=target; group.assigns.push_back(std::move(match));
            }
            output.objects.push_back(std::move(group));
        }
        return runtime_query_result::success;
    } catch (const std::bad_alloc&) {
        // The query cache is optional. A transient allocation failure must not
        // invalidate an otherwise readable project.
        return get_runtime_link(project, objects, output);
    } catch (...) { output={}; return runtime_query_result::invalid_runtime; }
}

namespace {

runtime_query_result index_runtime_array(const compiled_project_view& project,
    const runtime_binding_index& bindings, std::string_view suffix,
    type_ref& type, runtime_offset& offset) noexcept {
    while (!suffix.empty()) {
        const auto close = suffix.find(']');
        if (suffix.front() != '[' || close == std::string_view::npos)
            return runtime_query_result::invalid_input;
        const auto text = suffix.substr(1, close - 1);
        std::int64_t logical_index = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), logical_index);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            return runtime_query_result::invalid_input;
        std::int32_t origin = 0;
        derived_type_record array;
        if (!project.derived(type, array) || array.kind != derived_type_kind::bounded_array) {
            std::size_t member_index;
            if (!indexed_record_array(project, type, type, member_index, origin) || !project.derived(type, array))
                return runtime_query_result::unsupported_type;
            record_offset member;
            if (!bindings.member_offset(member_index, member) || !add_offset(offset, member, offset))
                return runtime_query_result::invalid_runtime;
        }
        std::uint64_t index;
        if (!array_offset(logical_index, origin, array.payload, index)) return runtime_query_result::not_found;
        type = array.child;

        auto element = type;
        runtime_offset stride = 1;
        derived_type_record derived;
        while (project.derived(element, derived)) {
            if (derived.kind == derived_type_kind::bounded_array) {
                if (!derived.payload || stride > UINT64_MAX / derived.payload)
                    return runtime_query_result::invalid_runtime;
                stride *= derived.payload;
            } else if (derived.kind != derived_type_kind::const_qualified &&
                       derived.kind != derived_type_kind::volatile_qualified) {
                return runtime_query_result::unsupported_type;
            }
            element = derived.child;
        }
        runtime_offset size = 0;
        if (element.kind() == type_ref_kind::intrinsic) {
            std::uint8_t scalar_size = 0;
            if (!bindings.intrinsic_size(static_cast<intrinsic_type>(element.payload()), scalar_size))
                return runtime_query_result::unsupported_type;
            size = scalar_size;
        } else {
            const auto handle = project.type_location(element);
            if (!bindings.record_size(handle, size)) return runtime_query_result::unsupported_type;
        }
        if (stride > UINT64_MAX / size || index > UINT64_MAX / (stride * size) ||
            !add_offset(offset, index * stride * size, offset))
            return runtime_query_result::invalid_runtime;
        suffix.remove_prefix(close + 1);
    }
    return runtime_query_result::success;
}

} // namespace

static runtime_query_result resolve_runtime_value(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    std::string_view name,
    runtime_value& output,
    task_runtime::variable_catalog_entry* location, bool* writable = nullptr) noexcept {

    output = {};

    if (!project.valid() ||
        name.empty()) {

        return runtime_query_result::invalid_input;
    }

    auto runtime_name =
        name;

    if (runtime_name.starts_with("::")) {
        runtime_name.remove_prefix(2);
    }

    const auto system_dot =
        runtime_name.find('.');

    const auto system_object =
        runtime_name.substr(
            0,
            system_dot);

    if (system_object ==
        runtime_system_object_name) {
        if (writable) return runtime_query_result::unsupported_type;

        if (system_dot ==
            std::string_view::npos) {

            return runtime_query_result::
                unsupported_type;
        }

        const auto member =
            runtime_name.substr(
                system_dot + 1);

        if (member.empty() ||
            member.find('.') !=
                std::string_view::npos) {

            return runtime_query_result::
                not_found;
        }

        return read_runtime_system_value(
            runtime,
            member,
            output);
    }

    const auto dot = name.find('.');
    const auto root = name.substr(0, dot);
    const auto bracket = root.find('[');

    object_handle object;

    const auto object_result =
        resolve_object(
            project,
            root.substr(0, bracket),
            object);

    if (object_result != runtime_query_result::success) {
        return object_result;
    }

    object_entry object_record;

    if (!project.object(
            object,
            object_record)) {

        return runtime_query_result::invalid_runtime;
    }

    runtime_offset offset = 0;

    if (!bindings.object_offset(
            object,
            offset)) {

        return runtime_query_result::invalid_runtime;
    }

    type_ref type = object_record.type;
    bool linked_reference = false;
    if (bracket != std::string_view::npos) {
        const auto normalized = normalize_location(project, bindings, runtime, type, offset, writable);
        if (normalized != runtime_query_result::success) return normalized;
        const auto indexed = index_runtime_array(project, bindings, root.substr(bracket), type, offset);
        if (indexed != runtime_query_result::success) return indexed;
    }

    if (dot != std::string_view::npos) {
        std::size_t begin = dot + 1;

        if (begin >= name.size()) {
            return runtime_query_result::invalid_input;
        }

        for (;;) {
            const auto next =
                name.find(
                    '.',
                    begin);

            const auto end =
                next == std::string_view::npos
                    ? name.size()
                    : next;

            if (end == begin) {
                return runtime_query_result::invalid_input;
            }

            const auto normalized =
                normalize_location(
                    project,
                    bindings,
                    runtime,
                    type,
                    offset, writable, linked_reference);

            if (normalized != runtime_query_result::success) {
                return normalized;
            }

            if (type.kind() != type_ref_kind::named) {
                return runtime_query_result::unsupported_type;
            }

            type_handle owner;

            if (!project.named(
                    type,
                    owner)) {

                return runtime_query_result::invalid_runtime;
            }

            type_entry owner_type;
            if (!project.type(owner, owner_type)) return runtime_query_result::invalid_runtime;
            const auto component = name.substr(begin, end - begin);
            if (const auto rule = owner_type.bits(); rule.bit_count) {
                unsigned bit = 0;
                const auto parsed = std::from_chars(component.data(), component.data() + component.size(), bit);
                if (next != std::string_view::npos || parsed.ec != std::errc{} ||
                    parsed.ptr != component.data() + component.size() || bit >= rule.bit_count)
                    return runtime_query_result::not_found;
                std::uint8_t size = 0;
                if (!bindings.intrinsic_size(owner_type.storage_intrinsic(), size) || bit / 8 >= size)
                    return runtime_query_result::invalid_runtime;
                std::uint64_t bits = 0;
                const auto read = read_native_bits(runtime, offset, size, bits);
                if (read != runtime_query_result::success) return read;
                if (location) {
                    location->offset = offset;
                    location->size = size;
                    location->bit = static_cast<std::uint8_t>(bit);
                }
                output.type = intrinsic_type::bool_type;
                output.size = 1;
                output.bits = (bits >> bit) & 1;
                return runtime_query_result::success;
            }
            if (owner_type.kind != graph_type_kind::record) return runtime_query_result::unsupported_type;
            const auto index = component.find('[');
            const auto string = project.find_string(component.substr(0, index));

            if (!string) {
                return runtime_query_result::not_found;
            }

            resolved_member member;

            const auto found =
                find_member_recursive(
                    project,
                    &bindings,
                    owner,
                    string,
                    0,
                    member);

            switch (found) {
            case member_search_result::none:
                return runtime_query_result::not_found;

            case member_search_result::ambiguous:
                return runtime_query_result::invalid_input;

            case member_search_result::invalid:
                return runtime_query_result::invalid_runtime;

            case member_search_result::found:
                break;
            }

            runtime_offset member_location = 0;

            if (!add_offset(
                    offset,
                    member.offset,
                    member_location) ||
                member_location >=
                    static_cast<runtime_offset>(
                        runtime.size())) {

                return runtime_query_result::invalid_runtime;
            }

            offset = member_location;
            type = member.type;
            linked_reference = false;
            // A live, explicitly linked field on the root object has a
            // materialized referent. Unconnected reference slots remain read-only.
            if (writable && begin == dot + 1 && bracket == std::string_view::npos) {
                const auto local = project.find_member(owner, string);
                link_record link;
                endpoint_path_record source_path;
                derived_type_record source_type;
                if (local && type_ref_kind::named == object_record.type.kind() &&
                    object_record.type == project.named(owner) &&
                    project.link(project.find_link_target({project.identity(object), endpoint_ref{local}}), link) &&
                    link.source.member.is_path() && project.endpoint_path(link.source.member.path(), source_path) &&
                    source_path.steps.count == 0) {
                    auto source = source_path.value_type;
                    while (project.derived(source, source_type) &&
                           source_type.kind == derived_type_kind::volatile_qualified)
                        source = source_type.child;
                    linked_reference = source.kind() == type_ref_kind::named ||
                        source.kind() == type_ref_kind::intrinsic ||
                        (project.derived(source, source_type) &&
                         source_type.kind == derived_type_kind::bounded_array);
                }
            }
            if (index != std::string_view::npos) {
                const auto resolved = normalize_location(project, bindings, runtime, type, offset, writable, linked_reference);
                linked_reference = false;
                if (resolved != runtime_query_result::success) return resolved;
                const auto indexed = index_runtime_array(project, bindings, component.substr(index), type, offset);
                if (indexed != runtime_query_result::success) return indexed;
            }

            if (next == std::string_view::npos) {
                break;
            }

            begin = next + 1;
        }
    }

    const auto normalized =
        normalize_location(
            project,
            bindings,
            runtime,
            type,
            offset, writable, linked_reference);

    if (normalized != runtime_query_result::success) {
        return normalized;
    }

    auto intrinsic = type.kind() == type_ref_kind::intrinsic
        ? static_cast<intrinsic_type>(type.payload()) : intrinsic_type::none;
    if (type.kind() == type_ref_kind::named) {
        type_entry entry;
        if (project.type(project.type_location(type), entry) &&
            entry.scalar_named()) intrinsic = entry.storage_intrinsic();
    }
    if (intrinsic == intrinsic_type::none) return runtime_query_result::unsupported_type;

    std::uint8_t size = 0;

    if (!bindings.intrinsic_size(
            intrinsic,
            size) ||
        size > sizeof(std::uint64_t)) {

        return runtime_query_result::unsupported_type;
    }

    std::uint64_t bits = 0;

    const auto read =
        read_native_bits(
            runtime,
            offset,
            size,
            bits);

    if (read != runtime_query_result::success) {
        return read;
    }

    if (location) {
        location->offset = offset;
        location->size = size;
        location->bit = task_runtime::no_bit;
    }
    output.type = intrinsic;
    output.size = size;
    output.bits = bits;

    return runtime_query_result::success;
}


static runtime_query_result locate_runtime_value(const compiled_project_view& project,
    const runtime_binding_index& bindings, std::span<const std::byte> runtime,
    std::string_view name, runtime_value& output,
    task_runtime::variable_catalog_entry* location, bool* writable) noexcept {
    if (name.find_first_of(".[") == std::string_view::npos) {
        try {
            object_handle object;
            object_entry entry;
            if (resolve_object(project, name, object) == runtime_query_result::success &&
                project.object(object, entry)) {
                const auto path = project.find_endpoint_path(entry.type, {});
                const auto link = path ? project.find_link_target(
                    {project.identity(object), endpoint_ref::from_path(path)}) : link_handle{};
                link_record alias;
                std::uint8_t bit = 0;
                if (link && project.link(link, alias) && project.bit_endpoint(alias.source.member, bit)) {
                    std::string source;
                    std::vector<identity_ref> scratch;
                    if (!format_endpoint_name(project, alias.source, source, scratch))
                        return runtime_query_result::invalid_runtime;
                    return resolve_runtime_value(project, bindings, runtime, source, output, location, writable);
                }
            }
        } catch (...) {
            return runtime_query_result::invalid_runtime;
        }
    }
    return resolve_runtime_value(project, bindings, runtime, name, output, location, writable);
}


runtime_query_result get_runtime_value(const compiled_project_view& project,
    const runtime_binding_index& bindings, std::span<const std::byte> runtime,
    std::string_view name, runtime_value& output) noexcept {
    return locate_runtime_value(project, bindings, runtime, name, output, nullptr, nullptr);
}

namespace {
bool parse_runtime_value(std::string_view text, runtime_value& value) noexcept {
    if (text.empty() || !value.size || value.size > 8) return false;
    const auto parse = [&](auto& result) noexcept {
        const auto end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, result);
        return parsed.ec == std::errc{} && parsed.ptr == end;
    };
    const auto width = static_cast<unsigned>(value.size) * 8;
    bool signed_integer = false;
    switch (value.type) {
    case intrinsic_type::bool_type:
        if (text == "true" || text == "True" || text == "1") value.bits = 1;
        else if (text == "false" || text == "False" || text == "0") value.bits = 0;
        else return false;
        return true;
    case intrinsic_type::float_type: {
        float number = 0;
        if (value.size != sizeof number || !parse(number) || !std::isfinite(number)) return false;
        value.bits = std::bit_cast<std::uint32_t>(number);
        return true;
    }
    case intrinsic_type::double_type:
    case intrinsic_type::long_double_type: {
        double number = 0;
        if (value.size != sizeof number || !parse(number) || !std::isfinite(number)) return false;
        value.bits = std::bit_cast<std::uint64_t>(number);
        return true;
    }
    case intrinsic_type::char_type: signed_integer = std::numeric_limits<char>::is_signed; break;
    case intrinsic_type::wchar_type: signed_integer = std::numeric_limits<wchar_t>::is_signed; break;
    case intrinsic_type::signed_char:
    case intrinsic_type::signed_short:
    case intrinsic_type::signed_int:
    case intrinsic_type::signed_long:
    case intrinsic_type::signed_long_long: signed_integer = true; break;
    case intrinsic_type::unsigned_char:
    case intrinsic_type::char8_type:
    case intrinsic_type::char16_type:
    case intrinsic_type::char32_type:
    case intrinsic_type::unsigned_short:
    case intrinsic_type::unsigned_int:
    case intrinsic_type::unsigned_long:
    case intrinsic_type::unsigned_long_long: break;
    default: return false;
    }
    const std::uint64_t maximum = UINT64_MAX >> (64 - width);
    if (signed_integer) {
        std::int64_t number = 0;
        const auto upper = static_cast<std::int64_t>(maximum >> 1);
        if (!parse(number) || number < -upper - 1 || number > upper) return false;
        value.bits = static_cast<std::uint64_t>(number) & maximum;
    } else {
        if (!parse(value.bits) || value.bits > maximum) return false;
    }
    return true;
}
} // namespace

runtime_query_result set_runtime_value(const compiled_project_view& project,
    const runtime_binding_index& bindings, std::span<std::byte> runtime,
    std::string_view name, std::string_view input, runtime_value& output) noexcept {
    task_runtime::variable_catalog_entry location{};
    bool writable = true;
    const auto found = locate_runtime_value(project, bindings, runtime, name, output, &location, &writable);
    if (found != runtime_query_result::success) return found;
    if (!writable) return runtime_query_result::unsupported_type;
    if (!parse_runtime_value(input, output)) return runtime_query_result::invalid_input;
    auto* target = runtime.data() + location.offset;
    if (location.bit != task_runtime::no_bit) {
        auto& byte = reinterpret_cast<std::uint8_t*>(target)[location.bit / 8];
        const auto mask = static_cast<std::uint8_t>(1u << (location.bit % 8));
        std::atomic_ref cell{byte};
        if (output.bits) cell.fetch_or(mask, std::memory_order_relaxed);
        else cell.fetch_and(static_cast<std::uint8_t>(~mask), std::memory_order_relaxed);
    } else {
        std::memcpy(target, &output.bits, location.size);
    }
    return runtime_query_result::success;
}

bool build_task_variable_catalog(const compiled_project_view& project,
    const runtime_binding_index& bindings, std::span<const std::byte> runtime,
    std::vector<std::byte>& output) noexcept {
    using namespace task_runtime;
    try {
        std::vector<variable_catalog_entry> entries;
        std::string names;
        std::string name, source;
        std::vector<identity_ref> scratch;
        auto add = [&](std::string_view spelling, variable_catalog_entry entry) {
            if (spelling.size() > UINT32_MAX - names.size()) return false;
            entry.name_offset = static_cast<std::uint32_t>(names.size());
            entry.name_size = static_cast<std::uint32_t>(spelling.size());
            names.append(spelling);
            entries.push_back(entry);
            return true;
        };
        for (std::size_t index = 0; index < project.object_count(); ++index) {
            const auto handle = project.object_at(index);
            if (!handle) continue;
            object_entry object;
            if (!project.object(handle, object) ||
                !qualified_identity_name(project, project.identity(handle), name, scratch)) return false;
            variable_catalog_entry entry{};
            type_entry scalar;
            const auto type = object.type;
            auto storage = intrinsic_type::none;
            if (type.kind() == type_ref_kind::intrinsic) {
                storage = static_cast<intrinsic_type>(type.payload());
            } else if (type.kind() == type_ref_kind::named &&
                       project.type(project.type_location(type), scalar)) {
                storage = scalar.storage_intrinsic();
                entry.bit_count = scalar.bits().bit_count;
            }
            if (!bindings.intrinsic_size(storage, entry.size)) continue;
            if (!bindings.object_offset(handle, entry.offset) ||
                entry.offset > runtime.size() || entry.size > runtime.size() - entry.offset) return false;
            entry.bit = no_bit;
            if (!add(name, entry)) return false;
        }
        for (std::size_t index = 0; index < project.link_count(); ++index) {
            const auto handle = project.link_at(index);
            if (!handle) continue;
            link_record link;
            if (!project.link(handle, link)) return false;
            std::uint8_t bit = 0;
            if (!project.bit_endpoint(link.source.member, bit)) continue;
            if (!format_endpoint_name(project, link.target, name, scratch) ||
                !format_endpoint_name(project, link.source, source, scratch)) return false;
            runtime_value ignored;
            variable_catalog_entry entry{};
            if (resolve_runtime_value(project, bindings, runtime, source, ignored, &entry) !=
                runtime_query_result::success || !entry.size || entry.bit != bit ||
                !add(name, entry) || !add(source, entry)) return false;
        }
        if (entries.size() > UINT32_MAX) return false;
        const auto table_size = entries.size() * sizeof(variable_catalog_entry);
        output.resize(sizeof(variable_catalog_header) + table_size + names.size());
        const variable_catalog_header header{variable_catalog_magic,
            static_cast<std::uint32_t>(entries.size()), output.size()};
        std::memcpy(output.data(), &header, sizeof header);
        if (table_size) std::memcpy(output.data() + sizeof header, entries.data(), table_size);
        if (!names.empty()) std::memcpy(output.data() + sizeof header + table_size, names.data(), names.size());
        return true;
    } catch (...) {
        return false;
    }
}
}
