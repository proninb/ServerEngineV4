#include "runtime_query.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

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
