#include "runtime_ic.hpp"

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
    bool default_value = false;
};

[[nodiscard]] bool add_offset(
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

[[nodiscard]] bool strip_cv(
    const compiled_project_view& project,
    type_ref& type,
    bool& constant) noexcept {

    for (std::size_t step = 0;
         step <= project.derived_type_count();
         ++step) {

        if (type.kind() != type_ref_kind::derived) {
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
        construction_value construction;

        if (!project.member(
                type,
                member,
                record) ||
            !project.construction(
                type,
                member,
                construction)) {

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
            static_cast<runtime_offset>(
                member_offset);

        output.type = record.type;
        output.default_value =
            construction == construction_value{};
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

[[nodiscard]] runtime_ic_result resolve_object(
    const compiled_project_view& project,
    std::span<const std::string_view> components,
    object_handle& output) noexcept {

    output = {};

    if (components.empty()) {
        return runtime_ic_result::invalid_input;
    }

    auto parent =
        project.identity_root();

    if (!parent) {
        return runtime_ic_result::invalid_runtime;
    }

    for (std::size_t index = 0;
         index < components.size();
         ++index) {

        const auto component =
            components[index];

        if (component.empty()) {
            return runtime_ic_result::invalid_input;
        }

        const auto name =
            project.find_string(
                component);

        if (!name) {
            return runtime_ic_result::not_found;
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
                    : identity_kind::namespace_scope);

        if (!identity) {
            return runtime_ic_result::not_found;
        }

        if (last) {
            output =
                project.find_object(
                    identity);

            return output
                ? runtime_ic_result::success
                : runtime_ic_result::not_found;
        }

        parent = identity;
    }

    return runtime_ic_result::not_found;
}

}

runtime_ic_result resolve_runtime_ic_scalar(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::uint64_t runtime_size,
    runtime_ic_path_view path,
    runtime_ic_scalar_target& output) noexcept {

    output = {};

    if (!project.valid() ||
        runtime_size == 0 ||
        path.object.empty()) {

        return runtime_ic_result::invalid_input;
    }

    object_handle object;

    const auto object_result =
        resolve_object(
            project,
            path.object,
            object);

    if (object_result != runtime_ic_result::success) {
        return object_result;
    }

    object_entry object_record;

    if (!project.object(
            object,
            object_record)) {

        return runtime_ic_result::invalid_runtime;
    }

    runtime_offset offset = 0;

    if (!bindings.object_offset(
            object,
            offset) ||
        offset >= runtime_size) {

        return runtime_ic_result::invalid_runtime;
    }

    type_ref type =
        object_record.type;

    bool constant = false;
    bool default_value =
        !object_record.non_default_initializer();

    for (const auto component :
         path.members) {

        if (component.empty()) {
            return runtime_ic_result::invalid_input;
        }

        if (!strip_cv(
                project,
                type,
                constant)) {

            return runtime_ic_result::invalid_runtime;
        }

        if (type.kind() != type_ref_kind::named) {
            return runtime_ic_result::unsupported_type;
        }

        if (type.payload() == 0) {
            return runtime_ic_result::invalid_runtime;
        }

        const auto owner =
            project.type_at(
                static_cast<std::size_t>(
                    type.payload() - 1));

        if (!owner) {
            return runtime_ic_result::invalid_runtime;
        }

        const auto name =
            project.find_string(
                component);

        if (!name) {
            return runtime_ic_result::not_found;
        }

        resolved_member member;

        const auto found =
            find_member_recursive(
                project,
                bindings,
                owner,
                name,
                0,
                member);

        switch (found) {
        case member_search_result::none:
            return runtime_ic_result::not_found;

        case member_search_result::ambiguous:
            return runtime_ic_result::invalid_input;

        case member_search_result::invalid:
            return runtime_ic_result::invalid_runtime;

        case member_search_result::found:
            break;
        }

        runtime_offset member_location = 0;

        if (!add_offset(
                offset,
                member.offset,
                member_location) ||
            member_location >= runtime_size) {

            return runtime_ic_result::invalid_runtime;
        }

        offset = member_location;
        type = member.type;
        default_value =
            member.default_value;
    }

    if (!strip_cv(
            project,
            type,
            constant)) {

        return runtime_ic_result::invalid_runtime;
    }

    if (type.kind() == type_ref_kind::derived ||
        type.kind() == type_ref_kind::named) {

        // Arrays/records are expanded by the IC codec/traversal layer.
        // Pointer/reference slots are structural Runtime state and are never
        // persisted as scalar IC values.
        return runtime_ic_result::unsupported_type;
    }

    if (type.kind() != type_ref_kind::intrinsic) {
        return runtime_ic_result::invalid_runtime;
    }

    const auto intrinsic =
        static_cast<intrinsic_type>(
            type.payload());

    std::uint8_t size = 0;

    if (!bindings.intrinsic_size(
            intrinsic,
            size) ||
        size == 0 ||
        size >
            runtime_ic_scalar_value{}.bytes.size() ||
        offset >
            runtime_size ||
        size >
            runtime_size - offset) {

        return runtime_ic_result::invalid_runtime;
    }

    output.offset = offset;
    output.type = intrinsic;
    output.size = size;

    if (constant) {
        output.flags |=
            runtime_ic_target_const;
    }

    if (default_value) {
        output.flags |=
            runtime_ic_target_default;
    }

    return runtime_ic_result::success;
}

runtime_ic_result snapshot_runtime_ic_scalar(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<const std::byte> runtime,
    runtime_ic_path_view path,
    runtime_ic_scalar_value& output) noexcept {

    output = {};

    runtime_ic_scalar_target target;

    const auto resolved =
        resolve_runtime_ic_scalar(
            project,
            bindings,
            static_cast<std::uint64_t>(
                runtime.size()),
            path,
            target);

    if (resolved != runtime_ic_result::success) {
        return resolved;
    }

    std::memcpy(
        output.bytes.data(),
        runtime.data() +
            static_cast<std::size_t>(
                target.offset),
        target.size);

    output.type = target.type;
    output.size = target.size;

    return runtime_ic_result::success;
}

runtime_ic_result reset_runtime_ic_scalar(
    const compiled_project_view& project,
    const runtime_binding_index& bindings,
    std::span<std::byte> runtime,
    runtime_ic_path_view path,
    const runtime_ic_scalar_value& value) noexcept {

    if (value.type == intrinsic_type::none ||
        value.size == 0 ||
        value.size > value.bytes.size()) {

        return runtime_ic_result::invalid_input;
    }

    runtime_ic_scalar_target target;

    const auto resolved =
        resolve_runtime_ic_scalar(
            project,
            bindings,
            static_cast<std::uint64_t>(
                runtime.size()),
            path,
            target);

    if (resolved != runtime_ic_result::success) {
        return resolved;
    }

    if (target.type != value.type ||
        target.size != value.size) {

        return runtime_ic_result::type_mismatch;
    }

    std::memcpy(
        runtime.data() +
            static_cast<std::size_t>(
                target.offset),
        value.bytes.data(),
        value.size);

    return runtime_ic_result::success;
}

}
