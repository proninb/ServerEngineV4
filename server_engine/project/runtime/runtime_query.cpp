#include "runtime_query.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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
    std::span<const std::byte> runtime,
    type_ref& type,
    runtime_offset& offset) noexcept {

    const auto runtime_base =
        reinterpret_cast<std::uintptr_t>(
            runtime.data());

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
            type = derived.child;
            continue;

        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference: {
            if (offset >
                    static_cast<runtime_offset>(
                        runtime.size()) ||
                sizeof(std::uintptr_t) >
                    static_cast<runtime_offset>(
                        runtime.size()) - offset) {

                return runtime_query_result::invalid_runtime;
            }

            std::uintptr_t target = 0;

            std::memcpy(
                &target,
                runtime.data() +
                    static_cast<std::size_t>(offset),
                sizeof(target));

            if (target < runtime_base) {
                return runtime_query_result::invalid_runtime;
            }

            const auto relative =
                target - runtime_base;

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
    const runtime_binding_index& bindings,
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

        if (!bindings.member_offset(
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
            base.virtual_base() ||
            !bindings.base_offset(
                global,
                base_offset)) {

            return member_search_result::invalid;
        }

        resolved_member candidate;

        const auto found =
            find_member_recursive(
                project,
                bindings,
                base.type,
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

        const auto handle =
            project.type_at(
                static_cast<std::size_t>(
                    type.payload() - 1));

        if (!handle) {
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

    const auto handle =
        project.type_at(
            static_cast<std::size_t>(
                type.payload() - 1));

    if (!handle) {
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
                project.identity(
                    base.type),
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

    const auto owner =
        project.type_at(
            static_cast<std::size_t>(
                type.payload() - 1));

    member_record member;

    if (!owner ||
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

    object_entry object;

    if (!endpoint.object ||
        !endpoint.member ||
        !project.object(
            endpoint.object,
            object) ||
        !qualified_identity_name(
            project,
            project.identity(
                endpoint.object),
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
        kind != expected ||
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

            return runtime_query_result::success;
        }

        output.pattern = true;

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

            if (requested.empty() ||
                wildcard_query(
                    requested)) {

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

            object_entry entry;

            if (!project.object(
                    object,
                    entry)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

            runtime_object_type result;

            if (!qualified_identity_name(
                    project,
                    project.identity(object),
                    result.object,
                    identity_scratch) ||
                !build_type_spec(
                    project,
                    entry.type,
                    result.type,
                    identity_scratch)) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }

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

            if (!file) {
                file =
                    contribution.file;
                continue;
            }

            if (file !=
                contribution.file) {

                output = {};
                return runtime_query_result::
                    invalid_runtime;
            }
        }

        for (std::size_t group = 0;
             group < handles.size();
             ++group) {

            const auto selected =
                handles[group];

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

                if (link.source.object != selected &&
                    link.target.object != selected) {

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


runtime_query_result get_runtime_value(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    std::string_view name,
    runtime_value& output) noexcept {

    output = {};

    if (!project.valid() ||
        name.empty()) {

        return runtime_query_result::invalid_input;
    }

    const auto dot =
        name.find('.');

    object_handle object;

    const auto object_result =
        resolve_object(
            project,
            name.substr(0, dot),
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
                    runtime,
                    type,
                    offset);

            if (normalized != runtime_query_result::success) {
                return normalized;
            }

            if (type.kind() != type_ref_kind::named) {
                return runtime_query_result::unsupported_type;
            }

            const auto owner =
                project.type_at(
                    static_cast<std::size_t>(
                        type.payload() - 1));

            if (!owner) {
                return runtime_query_result::invalid_runtime;
            }

            const auto string =
                project.find_string(
                    name.substr(
                        begin,
                        end - begin));

            if (!string) {
                return runtime_query_result::not_found;
            }

            resolved_member member;

            const auto found =
                find_member_recursive(
                    project,
                    bindings,
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

            if (next == std::string_view::npos) {
                break;
            }

            begin = next + 1;
        }
    }

    const auto normalized =
        normalize_location(
            project,
            runtime,
            type,
            offset);

    if (normalized != runtime_query_result::success) {
        return normalized;
    }

    if (type.kind() != type_ref_kind::intrinsic) {
        return runtime_query_result::unsupported_type;
    }

    const auto intrinsic =
        static_cast<intrinsic_type>(
            type.payload());

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

    output.type = intrinsic;
    output.size = size;
    output.bits = bits;

    return runtime_query_result::success;
}

}
