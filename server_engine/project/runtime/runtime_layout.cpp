#include "runtime_layout.hpp"

#include <algorithm>
#include <limits>

namespace cw::server {
namespace {

constexpr std::uint64_t invalid_offset =
    (std::numeric_limits<std::uint64_t>::max)();

[[nodiscard]] bool add_u64(
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

[[nodiscard]] bool multiply_u64(
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

[[nodiscard]] bool align_up(
    std::uint64_t value,
    std::uint32_t alignment,
    std::uint64_t& output) noexcept {

    if (alignment == 0 ||
        (alignment &
            (alignment - 1)) != 0) {

        return false;
    }

    const auto mask =
        static_cast<std::uint64_t>(
            alignment - 1);

    if (value >
        (std::numeric_limits<std::uint64_t>::max)() -
            mask) {

        return false;
    }

    output =
        (value + mask) &
        ~mask;

    return true;
}

[[nodiscard]] runtime_layout_result intrinsic_layout(
    intrinsic_type type,
    abi_target target,
    runtime_value_layout& output) noexcept {

    output = {};

    switch (type) {
    case intrinsic_type::bool_type:
    case intrinsic_type::char_type:
    case intrinsic_type::signed_char:
    case intrinsic_type::unsigned_char:
    case intrinsic_type::char8_type:
        output = {1, 1, 0};
        return runtime_layout_result::success;

    case intrinsic_type::wchar_type:
        output =
            target == abi_target::windows_x64
                ? runtime_value_layout{2, 2, 0}
                : runtime_value_layout{4, 4, 0};
        return runtime_layout_result::success;

    case intrinsic_type::char16_type:
    case intrinsic_type::signed_short:
    case intrinsic_type::unsigned_short:
        output = {2, 2, 0};
        return runtime_layout_result::success;

    case intrinsic_type::char32_type:
    case intrinsic_type::signed_int:
    case intrinsic_type::unsigned_int:
    case intrinsic_type::float_type:
        output = {4, 4, 0};
        return runtime_layout_result::success;

    case intrinsic_type::signed_long:
    case intrinsic_type::unsigned_long:
        output =
            target == abi_target::windows_x64
                ? runtime_value_layout{4, 4, 0}
                : runtime_value_layout{8, 8, 0};
        return runtime_layout_result::success;

    case intrinsic_type::signed_long_long:
    case intrinsic_type::unsigned_long_long:
    case intrinsic_type::double_type:
    case intrinsic_type::nullptr_type:
        output = {8, 8, 0};
        return runtime_layout_result::success;

    case intrinsic_type::long_double_type:
        output =
            target == abi_target::windows_x64
                ? runtime_value_layout{8, 8, 0}
                : runtime_value_layout{16, 16, 0};
        return runtime_layout_result::success;

    case intrinsic_type::void_type:
        return runtime_layout_result::unsupported_type;

    case intrinsic_type::none:
        break;
    }

    return runtime_layout_result::invalid_input;
}

}

class runtime_layout_builder final {
public:
    runtime_layout_builder(
        const compiled_project_view& project,
        const server_abi_configuration& abi,
        runtime_layout& output) noexcept
        : project(project),
          abi(abi),
          output(output) {
    }

    [[nodiscard]] runtime_layout_result build() noexcept {
        std::uint64_t cursor = 0;
        std::uint32_t maximum_alignment = 1;

        for (std::size_t index = 0;
             index < project.object_count();
             ++index) {

            const auto handle =
                project.object_at(index);

            object_entry object;

            if (!handle ||
                !project.object(
                    handle,
                    object)) {

                return runtime_layout_result::invalid_input;
            }

            runtime_value_layout value;

            const auto resolved =
                resolve(
                    object.type,
                    value);

            if (resolved !=
                runtime_layout_result::success) {

                return resolved;
            }

            std::uint64_t aligned = 0;

            if (!align_up(
                    cursor,
                    value.alignment,
                    aligned)) {

                return runtime_layout_result::overflow;
            }

            output.object_offsets[index] =
                aligned;

            if (!add_u64(
                    aligned,
                    value.size,
                    cursor)) {

                return runtime_layout_result::overflow;
            }

            maximum_alignment =
                (std::max)(
                    maximum_alignment,
                    value.alignment);
        }

        std::uint64_t final_size = 0;

        if (!align_up(
                cursor,
                maximum_alignment,
                final_size)) {

            return runtime_layout_result::overflow;
        }

        output.size_value =
            final_size;

        output.alignment_value =
            maximum_alignment;

        return runtime_layout_result::success;
    }

private:
    [[nodiscard]] runtime_layout_result resolve(
        type_ref type,
        runtime_value_layout& value) noexcept {

        value = {};

        if (!type) {
            return runtime_layout_result::invalid_input;
        }

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            if (type.payload() >
                static_cast<std::uint32_t>(
                    intrinsic_type::nullptr_type)) {

                return runtime_layout_result::invalid_input;
            }

            return intrinsic_layout(
                static_cast<intrinsic_type>(
                    type.payload()),
                abi.target,
                value);

        case type_ref_kind::named: {
            if (type.payload() == 0 ||
                type.payload() >
                    project.type_count()) {

                return runtime_layout_result::invalid_input;
            }

            const auto handle =
                project.type_at(
                    type.payload() - 1);

            return resolve_record(
                handle,
                value);
        }

        case type_ref_kind::derived:
            return resolve_derived(
                type,
                value);

        case type_ref_kind::invalid:
            break;
        }

        return runtime_layout_result::invalid_input;
    }

    [[nodiscard]] runtime_layout_result resolve_record(
        type_handle handle,
        runtime_value_layout& value) noexcept {

        value = {};

        if (!handle ||
            handle.value() >
                output.type_slots.size()) {

            return runtime_layout_result::invalid_input;
        }

        auto& slot =
            output.type_slots[
                handle.value() - 1];

        if (slot.state ==
            runtime_layout::slot_state::ready) {

            value = {
                slot.size,
                slot.alignment,
                0,
            };

            return runtime_layout_result::success;
        }

        if (slot.state ==
            runtime_layout::slot_state::visiting) {

            return runtime_layout_result::invalid_input;
        }

        type_entry type;

        if (!project.type(
                handle,
                type) ||
            type.kind !=
                graph_type_kind::record) {

            return runtime_layout_result::invalid_input;
        }

        if (!type.defined()) {
            return runtime_layout_result::unsupported_type;
        }

        slot.state =
            runtime_layout::slot_state::visiting;

        std::uint64_t cursor = 0;
        std::uint64_t union_size = 0;
        std::uint32_t record_alignment = 1;

        const auto is_union =
            type.record_kind ==
                graph_record_kind::union_type;

        if (!is_union &&
            type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {

            slot.state =
                runtime_layout::slot_state::empty;

            return runtime_layout_result::invalid_input;
        }

        for (std::uint32_t local = 0;
             local < type.members.count;
             ++local) {

            const auto global =
                static_cast<std::uint64_t>(
                    type.members.begin) +
                local;

            if (global >=
                output.member_offsets.size()) {

                slot.state =
                    runtime_layout::slot_state::empty;

                return runtime_layout_result::invalid_input;
            }

            auto& member_offset =
                output.member_offsets[
                    static_cast<std::size_t>(
                        global)];

            if (member_offset !=
                invalid_offset) {

                slot.state =
                    runtime_layout::slot_state::empty;

                return runtime_layout_result::invalid_input;
            }

            member_record member;

            if (!project.member_at(
                    static_cast<std::size_t>(
                        global),
                    member)) {

                slot.state =
                    runtime_layout::slot_state::empty;

                return runtime_layout_result::invalid_input;
            }

            runtime_value_layout member_layout;

            const auto resolved =
                resolve(
                    member.type,
                    member_layout);

            if (resolved !=
                runtime_layout_result::success) {

                slot.state =
                    runtime_layout::slot_state::empty;

                return resolved;
            }

            const auto effective_alignment =
                (std::min)(
                    member_layout.alignment,
                    abi.pack);

            if (effective_alignment == 0) {
                slot.state =
                    runtime_layout::slot_state::empty;

                return runtime_layout_result::invalid_input;
            }

            record_alignment =
                (std::max)(
                    record_alignment,
                    effective_alignment);

            if (is_union) {
                member_offset = 0;

                union_size =
                    (std::max)(
                        union_size,
                        member_layout.size);

                continue;
            }

            std::uint64_t aligned = 0;

            if (!align_up(
                    cursor,
                    effective_alignment,
                    aligned)) {

                slot.state =
                    runtime_layout::slot_state::empty;

                return runtime_layout_result::overflow;
            }

            member_offset =
                aligned;

            if (!add_u64(
                    aligned,
                    member_layout.size,
                    cursor)) {

                slot.state =
                    runtime_layout::slot_state::empty;

                return runtime_layout_result::overflow;
            }
        }

        auto raw_size =
            is_union
                ? union_size
                : cursor;

        if (raw_size == 0) {
            raw_size = 1;
        }

        std::uint64_t final_size = 0;

        if (!align_up(
                raw_size,
                record_alignment,
                final_size)) {

            slot.state =
                runtime_layout::slot_state::empty;

            return runtime_layout_result::overflow;
        }

        slot.size =
            final_size;

        slot.alignment =
            record_alignment;

        slot.state =
            runtime_layout::slot_state::ready;

        value = {
            final_size,
            record_alignment,
            0,
        };

        return runtime_layout_result::success;
    }

    [[nodiscard]] runtime_layout_result resolve_derived(
        type_ref type,
        runtime_value_layout& value) noexcept {

        value = {};

        if (type.kind() !=
                type_ref_kind::derived ||
            type.payload() == 0 ||
            type.payload() >
                output.derived_slots.size()) {

            return runtime_layout_result::invalid_input;
        }

        auto& slot =
            output.derived_slots[
                type.payload() - 1];

        if (slot.state ==
            runtime_layout::slot_state::ready) {

            value = {
                slot.size,
                slot.alignment,
                0,
            };

            return runtime_layout_result::success;
        }

        if (slot.state ==
            runtime_layout::slot_state::visiting) {

            return runtime_layout_result::invalid_input;
        }

        derived_type_record derived;

        if (!project.derived(
                type,
                derived)) {

            return runtime_layout_result::invalid_input;
        }

        slot.state =
            runtime_layout::slot_state::visiting;

        runtime_value_layout resolved;

        runtime_layout_result result =
            runtime_layout_result::invalid_input;

        switch (derived.kind) {
        case derived_type_kind::const_qualified:
        case derived_type_kind::volatile_qualified:
            result =
                resolve(
                    derived.child,
                    resolved);
            break;

        case derived_type_kind::pointer:
        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference:
            resolved = {8, 8, 0};
            result =
                runtime_layout_result::success;
            break;

        case derived_type_kind::bounded_array: {
            if (derived.payload == 0) {
                result =
                    runtime_layout_result::invalid_input;
                break;
            }

            runtime_value_layout child;

            result =
                resolve(
                    derived.child,
                    child);

            if (result !=
                runtime_layout_result::success) {

                break;
            }

            std::uint64_t bytes = 0;

            if (!multiply_u64(
                    child.size,
                    derived.payload,
                    bytes)) {

                result =
                    runtime_layout_result::overflow;
                break;
            }

            resolved = {
                bytes,
                child.alignment,
                0,
            };

            break;
        }

        case derived_type_kind::unbounded_array:
            result =
                runtime_layout_result::unsupported_type;
            break;
        }

        if (result !=
            runtime_layout_result::success) {

            slot.state =
                runtime_layout::slot_state::empty;

            return result;
        }

        slot.size =
            resolved.size;

        slot.alignment =
            resolved.alignment;

        slot.state =
            runtime_layout::slot_state::ready;

        value = resolved;

        return runtime_layout_result::success;
    }

    const compiled_project_view& project;
    const server_abi_configuration& abi;
    runtime_layout& output;
};

void runtime_layout::reset() noexcept {
    type_slots.clear();
    derived_slots.clear();
    member_offsets.clear();
    object_offsets.clear();

    size_value = 0;
    alignment_value = 1;
}

bool runtime_layout::type(
    type_handle type_value,
    runtime_value_layout& output_value) const noexcept {

    output_value = {};

    if (!type_value ||
        type_value.value() >
            type_slots.size()) {

        return false;
    }

    const auto& slot =
        type_slots[
            type_value.value() - 1];

    if (slot.state !=
        slot_state::ready) {

        return false;
    }

    output_value = {
        slot.size,
        slot.alignment,
        0,
    };

    return true;
}

bool runtime_layout::member_offset(
    std::size_t index,
    std::uint64_t& output_value) const noexcept {

    output_value = 0;

    if (index >=
            member_offsets.size() ||
        member_offsets[index] ==
            invalid_offset) {

        return false;
    }

    output_value =
        member_offsets[index];

    return true;
}

bool runtime_layout::object_offset(
    object_handle object,
    std::uint64_t& output_value) const noexcept {

    output_value = 0;

    if (!object ||
        object.value() >
            object_offsets.size()) {

        return false;
    }

    output_value =
        object_offsets[
            object.value() - 1];

    return true;
}

runtime_layout_result prepare_runtime_layout(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    runtime_layout& output) noexcept {

    output.reset();

    if (!project.valid() ||
        !valid_abi_layout_key(
            make_abi_layout_key(
                abi))) {

        return runtime_layout_result::invalid_input;
    }

    if (project.object_count() == 0) {
        return runtime_layout_result::success;
    }

    try {
        output.type_slots.resize(
            project.type_count());

        output.derived_slots.resize(
            project.derived_type_count());

        output.member_offsets.assign(
            project.member_count(),
            invalid_offset);

        output.object_offsets.resize(
            project.object_count());
    }
    catch (...) {
        output.reset();

        return runtime_layout_result::failed;
    }

    runtime_layout_builder builder{
        project,
        abi,
        output,
    };

    const auto result =
        builder.build();

    if (result !=
        runtime_layout_result::success) {

        output.reset();
    }

    return result;
}

}
