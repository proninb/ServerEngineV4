#include "runtime_layout.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace cw::server {
namespace {

constexpr runtime_offset invalid_offset =
    (std::numeric_limits<runtime_offset>::max)();

constexpr runtime_offset pending_offset =
    invalid_offset - 1;

constexpr record_offset invalid_record_offset =
    (std::numeric_limits<record_offset>::max)();

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

    abi_properties properties;

    if (!abi_layout_properties(
            target,
            properties)) {

        return runtime_layout_result::invalid_input;
    }

    const auto windows =
        target == abi_target::windows_x86 ||
        target == abi_target::windows_x64;

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
            windows
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
            windows
                ? runtime_value_layout{4, 4, 0}
                : runtime_value_layout{8, 8, 0};
        return runtime_layout_result::success;

    case intrinsic_type::signed_long_long:
    case intrinsic_type::unsigned_long_long:
    case intrinsic_type::double_type:
        output = {8, 8, 0};
        return runtime_layout_result::success;

    case intrinsic_type::nullptr_type:
        output = {
            properties.pointer_size,
            properties.pointer_alignment,
            0,
        };
        return runtime_layout_result::success;

    case intrinsic_type::long_double_type:
        output =
            windows
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
        for (std::size_t index = 0;
             index < project.object_count();
             ++index) {

            const auto handle =
                project.object_at(index);

            object_entry object;
            runtime_value_layout value;

            if (!handle ||
                !project.object(
                    handle,
                    object)) {

                return runtime_layout_result::invalid_input;
            }

            const auto resolved =
                resolve(
                    object.type,
                    value);

            if (resolved !=
                runtime_layout_result::success) {

                return resolved;
            }
        }

        // Resolving an object may discover T& and therefore require one
        // canonical unconnected<T>. Resolving that T can discover more
        // reference members, so the dense worklist is allowed to grow.
        for (std::size_t index = 0;
             index <
                 output.unconnected_types.size();
             ++index) {

            runtime_value_layout value;

            const auto resolved =
                resolve(
                    output.unconnected_types[index],
                    value);

            if (resolved !=
                runtime_layout_result::success) {

                return resolved;
            }
        }

        std::uint64_t cursor =
            sizeof(runtime_system);

        std::uint32_t maximum_alignment =
            alignof(runtime_system);

        for (const auto type :
             output.unconnected_types) {

            runtime_value_layout value;

            const auto resolved =
                resolve(
                    type,
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

            const auto stored =
                store_unconnected_offset(
                    type,
                    aligned);

            if (stored !=
                runtime_layout_result::success) {

                return stored;
            }

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

        for (std::size_t index = 0;
             index < project.object_count();
             ++index) {

            const auto handle =
                project.object_at(index);

            object_entry object;
            runtime_value_layout value;

            if (!handle ||
                !project.object(
                    handle,
                    object)) {

                return runtime_layout_result::invalid_input;
            }

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
    [[nodiscard]] runtime_layout_result
    require_unconnected(
        type_ref type) noexcept {

        if (!type) {
            return runtime_layout_result::invalid_input;
        }

        runtime_offset* offset = nullptr;

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            if (type.payload() >=
                output.unconnected_intrinsic_offsets.size()) {

                return runtime_layout_result::invalid_input;
            }

            offset =
                &output.unconnected_intrinsic_offsets[
                    type.payload()];
            break;

        case type_ref_kind::named:
            if (type.payload() == 0 ||
                type.payload() >
                    output.unconnected_type_offsets.size()) {

                return runtime_layout_result::invalid_input;
            }

            offset =
                &output.unconnected_type_offsets[
                    type.payload() - 1];
            break;

        case type_ref_kind::derived:
            if (type.payload() == 0 ||
                type.payload() >
                    output.unconnected_derived_offsets.size()) {

                return runtime_layout_result::invalid_input;
            }

            offset =
                &output.unconnected_derived_offsets[
                    type.payload() - 1];
            break;

        case type_ref_kind::invalid:
            return runtime_layout_result::invalid_input;
        }

        if (*offset !=
            invalid_offset) {

            return runtime_layout_result::success;
        }

        *offset =
            pending_offset;

        try {
            output.unconnected_types.push_back(
                type);
        }
        catch (...) {
            *offset =
                invalid_offset;

            return runtime_layout_result::failed;
        }

        return runtime_layout_result::success;
    }

    [[nodiscard]] runtime_layout_result
    store_unconnected_offset(
        type_ref type,
        std::uint64_t value) noexcept {

        runtime_offset* offset = nullptr;

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            if (type.payload() >=
                output.unconnected_intrinsic_offsets.size()) {

                return runtime_layout_result::invalid_input;
            }

            offset =
                &output.unconnected_intrinsic_offsets[
                    type.payload()];
            break;

        case type_ref_kind::named:
            if (type.payload() == 0 ||
                type.payload() >
                    output.unconnected_type_offsets.size()) {

                return runtime_layout_result::invalid_input;
            }

            offset =
                &output.unconnected_type_offsets[
                    type.payload() - 1];
            break;

        case type_ref_kind::derived:
            if (type.payload() == 0 ||
                type.payload() >
                    output.unconnected_derived_offsets.size()) {

                return runtime_layout_result::invalid_input;
            }

            offset =
                &output.unconnected_derived_offsets[
                    type.payload() - 1];
            break;

        case type_ref_kind::invalid:
            return runtime_layout_result::invalid_input;
        }

        if (*offset !=
                pending_offset ||
            value >=
                pending_offset) {

            return runtime_layout_result::invalid_input;
        }

        *offset = value;

        return runtime_layout_result::success;
    }

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
            type_handle handle;

            if (!project.named(
                    type,
                    handle)) {

                return runtime_layout_result::invalid_input;
            }

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

    [[nodiscard]] runtime_layout_result
    resolve_windows_class_record(
        const type_entry& type,
        runtime_layout::layout_slot& slot,
        runtime_value_layout& value) noexcept {

        value = {};

        if (abi.target !=
                abi_target::windows_x86 &&
            abi.target !=
                abi_target::windows_x64) {

            return runtime_layout_result::
                unsupported_type;
        }

        abi_properties properties;

        if (!abi_layout_properties(
                abi.target,
                properties)) {

            return runtime_layout_result::
                invalid_input;
        }

        const auto base_begin =
            static_cast<std::size_t>(
                type.bases.begin);

        const auto base_count =
            static_cast<std::size_t>(
                type.bases.count);

        if (base_begin >
                output.base_offsets.size() ||
            base_count >
                output.base_offsets.size() -
                    base_begin) {

            return runtime_layout_result::
                invalid_input;
        }

        std::uint32_t base_mark = 0;

        if (base_count > 1) {
            try {
                if (base_marks.size() !=
                    output.type_slots.size()) {

                    base_marks.assign(
                        output.type_slots.size(),
                        0);

                    base_mark_generation = 0;
                }
            }
            catch (...) {
                return runtime_layout_result::
                    failed;
            }

            ++base_mark_generation;

            if (base_mark_generation == 0) {
                std::fill(
                    base_marks.begin(),
                    base_marks.end(),
                    0);

                base_mark_generation = 1;
            }

            base_mark =
                base_mark_generation;

            for (std::uint32_t local = 0;
                 local <
                     type.bases.count;
                 ++local) {

                base_record base;

                if (!project.base_at(
                        base_begin +
                            local,
                        base)) {

                    return runtime_layout_result::
                        invalid_input;
                }

                const auto base_handle =
                    project.find_type(
                        base.type);

                if (!base_handle ||
                    base_handle.value() >
                        output.type_slots.size()) {

                    return runtime_layout_result::
                        invalid_input;
                }

                if (base.virtual_base()) {
                    return runtime_layout_result::
                        unsupported_type;
                }

                auto& mark =
                    base_marks[
                        base_handle.value() - 1];

                if (mark == base_mark) {
                    return runtime_layout_result::
                        invalid_input;
                }

                mark = base_mark;
            }
        }

        constexpr std::uint32_t no_primary =
            (std::numeric_limits<std::uint32_t>::max)();

        std::uint32_t primary_local =
            no_primary;

        std::uint32_t record_alignment = 1;
        bool all_bases_empty = base_count != 0;
        bool previous_base_empty = false;
        std::uint64_t previous_empty_end = 0;
        // The last empty base is permitted at the end of a complete
        // record in the legacy MSVC ABI (it may have offset == sizeof).

        for (std::uint32_t local = 0;
             local <
                 type.bases.count;
             ++local) {

            const auto global =
                base_begin +
                local;

            if (output.base_offsets[
                    global] !=
                invalid_record_offset) {

                return runtime_layout_result::
                    invalid_input;
            }

            base_record base;

            if (!project.base_at(
                    global,
                    base)) {

                return runtime_layout_result::
                    invalid_input;
            }

            if (base.virtual_base()) {
                return runtime_layout_result::
                    unsupported_type;
            }

            const auto base_handle =
                project.find_type(
                    base.type);

            if (!base_handle ||
                base_handle.value() >
                    output.type_slots.size()) {

                return runtime_layout_result::
                    invalid_input;
            }

            runtime_value_layout base_layout;

            const auto resolved =
                resolve_record(
                    base_handle,
                    base_layout);

            if (resolved !=
                runtime_layout_result::success) {

                return resolved;
            }

            // Empty-ness is a property of each base type, not of the
            // derived record or the number of direct bases.
            all_bases_empty = all_bases_empty &&
                output.type_slots[base_handle.value() - 1].empty_record;

            const auto effective_alignment =
                (std::min)(
                    base_layout.alignment,
                    abi.pack);

            if (effective_alignment == 0) {
                return runtime_layout_result::
                    invalid_input;
            }

            record_alignment =
                (std::max)(
                    record_alignment,
                    effective_alignment);

            type_entry base_type;

            if (!project.type(
                    base_handle,
                    base_type) ||
                !base_type.defined() ||
                base_type.kind !=
                    graph_type_kind::record ||
                base_type.record_kind ==
                    graph_record_kind::union_type) {

                return runtime_layout_result::
                    invalid_input;
            }

            if (base_type.polymorphic() &&
                primary_local ==
                    no_primary) {

                primary_local = local;
            }
        }

        for (std::uint32_t local = 0;
             local <
                 type.members.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.members.begin) +
                local;

            if (global >=
                    output.member_offsets.size() ||
                output.member_offsets[
                    global] !=
                    invalid_record_offset) {

                return runtime_layout_result::
                    invalid_input;
            }

            member_record member;

            if (!project.member_at(
                    global,
                    member)) {

                return runtime_layout_result::
                    invalid_input;
            }

            runtime_value_layout member_layout;

            const auto resolved =
                resolve(
                    member.type,
                    member_layout);

            if (resolved !=
                runtime_layout_result::success) {

                return resolved;
            }

            const auto effective_alignment =
                (std::min)(
                    member_layout.alignment,
                    abi.pack);

            if (effective_alignment == 0) {
                return runtime_layout_result::
                    invalid_input;
            }

            record_alignment =
                (std::max)(
                    record_alignment,
                    effective_alignment);
        }

        const auto has_primary =
            primary_local !=
                no_primary;

        if (has_primary &&
            !type.polymorphic()) {

            return runtime_layout_result::
                invalid_input;
        }

        const auto own_vfptr =
            type.polymorphic() &&
            !has_primary;

        if (own_vfptr) {
            const auto vfptr_alignment =
                (std::min)(
                    properties.pointer_alignment,
                    abi.pack);

            if (vfptr_alignment == 0) {
                return runtime_layout_result::
                    invalid_input;
            }

            record_alignment =
                (std::max)(
                    record_alignment,
                    vfptr_alignment);
        }

        std::uint64_t cursor = 0;

        const auto place_base =
            [&](std::uint32_t local)
                -> runtime_layout_result {

                if (local >=
                    type.bases.count) {

                    return runtime_layout_result::
                        invalid_input;
                }

                const auto global =
                    base_begin +
                    local;

                if (output.base_offsets[
                        global] !=
                    invalid_record_offset) {

                    return runtime_layout_result::
                        invalid_input;
                }

                base_record base;

                if (!project.base_at(
                        global,
                        base)) {

                    return runtime_layout_result::
                        invalid_input;
                }

                if (base.virtual_base()) {
                    return runtime_layout_result::
                        unsupported_type;
                }

                const auto base_handle =
                    project.find_type(
                        base.type);

                if (!base_handle) {
                    return runtime_layout_result::
                        invalid_input;
                }

                runtime_value_layout base_layout;

                const auto resolved =
                    resolve_record(
                        base_handle,
                        base_layout);

                if (resolved !=
                    runtime_layout_result::success) {

                    return resolved;
                }

                const auto effective_alignment =
                    (std::min)(
                        base_layout.alignment,
                        abi.pack);

                std::uint64_t aligned = 0;

                if (!align_up(
                        cursor,
                        effective_alignment,
                        aligned)) {

                    return runtime_layout_result::
                        overflow;
                }

                if (aligned >=
                    invalid_record_offset) {

                    return runtime_layout_result::
                        overflow;
                }

                const bool is_empty =
                    output.type_slots[base_handle.value() - 1].empty_record;

                // Default MSVC empty-base layout (without the optional
                // __declspec(empty_bases) attribute): consecutive empty
                // bases receive distinct locations. First empty base may
                // overlap the next nonempty base or the first data member.
                const auto extent =
                    output.type_slots[base_handle.value() - 1].nonvirtual_size;
                if (is_empty && previous_base_empty &&
                    aligned <= previous_empty_end) {
                    if (!add_u64(previous_empty_end, 1, aligned)) {
                        return runtime_layout_result::overflow;
                    }
                }
                if (aligned >= invalid_record_offset) {
                    return runtime_layout_result::overflow;
                }

                output.base_offsets[global] =
                    static_cast<record_offset>(aligned);

                if (is_empty) {
                    previous_base_empty = true;
                    // A recursively empty class can have nonzero MSVC
                    // non-virtual extent (e.g. E0,E1). A plain empty has
                    // extent 0, so it still overlaps the next data member.
                    if (!add_u64(aligned, extent, cursor)) {
                        return runtime_layout_result::overflow;
                    }
                    previous_empty_end = cursor;
                }
                else {
                    previous_base_empty = false;
                    if (!add_u64(aligned, base_layout.size, cursor)) {
                        return runtime_layout_result::overflow;
                    }
                }

                return runtime_layout_result::success;
            };

        if (has_primary) {
            const auto primary_global =
                base_begin +
                primary_local;

            base_record primary;
            runtime_value_layout primary_layout;

            if (!project.base_at(
                    primary_global,
                    primary)) {

                return runtime_layout_result::
                    invalid_input;
            }

            if (primary.virtual_base()) {
                return runtime_layout_result::
                    unsupported_type;
            }

            const auto primary_handle =
                project.find_type(
                    primary.type);

            const auto primary_resolved =
                primary_handle
                ? resolve_record(
                    primary_handle,
                    primary_layout)
                : runtime_layout_result::
                    invalid_input;

            if (primary_resolved !=
                    runtime_layout_result::success ||
                output.base_offsets[
                    primary_global] !=
                    invalid_record_offset) {

                return primary_resolved !=
                        runtime_layout_result::success
                    ? primary_resolved
                    : runtime_layout_result::
                        invalid_input;
            }

            output.base_offsets[
                primary_global] = 0;

            cursor =
                primary_layout.size;

            for (std::uint32_t local = 0;
                 local <
                     type.bases.count;
                 ++local) {

                if (local ==
                    primary_local) {

                    continue;
                }

                base_record base;
                type_entry base_type;

                if (!project.base_at(
                        base_begin +
                            local,
                        base)) {

                    return runtime_layout_result::
                        invalid_input;
                }

                const auto base_handle =
                    project.find_type(
                        base.type);

                if (!base_handle ||
                    !project.type(
                        base_handle,
                        base_type) ||
                    !base_type.defined()) {

                    return runtime_layout_result::
                        invalid_input;
                }

                if (!base_type.polymorphic()) {
                    continue;
                }

                const auto placed =
                    place_base(local);

                if (placed !=
                    runtime_layout_result::success) {

                    return placed;
                }
            }

            for (std::uint32_t local = 0;
                 local <
                     type.bases.count;
                 ++local) {

                base_record base;
                type_entry base_type;

                if (!project.base_at(
                        base_begin +
                            local,
                        base)) {

                    return runtime_layout_result::
                        invalid_input;
                }

                const auto base_handle =
                    project.find_type(
                        base.type);

                if (!base_handle ||
                    !project.type(
                        base_handle,
                        base_type) ||
                    !base_type.defined()) {

                    return runtime_layout_result::
                        invalid_input;
                }

                if (base_type.polymorphic()) {
                    continue;
                }

                const auto placed =
                    place_base(local);

                if (placed !=
                    runtime_layout_result::success) {

                    return placed;
                }
            }
        }
        else {
            if (own_vfptr) {
                if (!align_up(
                        properties.pointer_size,
                        record_alignment,
                        cursor)) {

                    return runtime_layout_result::
                        overflow;
                }
            }

            for (std::uint32_t local = 0;
                 local <
                     type.bases.count;
                 ++local) {

                const auto placed =
                    place_base(local);

                if (placed !=
                    runtime_layout_result::success) {

                    return placed;
                }
            }
        }

        for (std::uint32_t local = 0;
             local <
                 type.members.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.members.begin) +
                local;

            member_record member;

            if (!project.member_at(
                    global,
                    member)) {

                return runtime_layout_result::
                    invalid_input;
            }

            runtime_value_layout member_layout;

            const auto resolved =
                resolve(
                    member.type,
                    member_layout);

            if (resolved !=
                runtime_layout_result::success) {

                return resolved;
            }

            const auto effective_alignment =
                (std::min)(
                    member_layout.alignment,
                    abi.pack);

            std::uint64_t aligned = 0;

            if (!align_up(
                    cursor,
                    effective_alignment,
                    aligned)) {

                return runtime_layout_result::
                    overflow;
            }

            if (aligned >=
                invalid_record_offset) {

                return runtime_layout_result::
                    overflow;
            }

            output.member_offsets[
                global] =
                    static_cast<record_offset>(
                        aligned);

            if (!add_u64(
                    aligned,
                    member_layout.size,
                    cursor)) {

                return runtime_layout_result::
                    overflow;
            }
        }

        const auto nonvirtual_size = cursor;
        if (cursor == 0) {
            cursor = 1;
        }

        std::uint64_t final_size = 0;

        if (!align_up(
                cursor,
                record_alignment,
                final_size) ||
            final_size >
                (std::numeric_limits<record_offset>::max)()) {

            return runtime_layout_result::
                overflow;
        }

        slot.size =
            final_size;

        // Non-virtual physical extent differs from sizeof for empty classes;
        // nested multiple inheritance needs the *actual* nonvirtual extent.
        slot.nonvirtual_size =
            static_cast<std::uint32_t>(nonvirtual_size);

        slot.alignment =
            record_alignment;

        slot.empty_record = all_bases_empty && type.members.count == 0 && !type.polymorphic();

        slot.state =
            runtime_layout::slot_state::ready;

        value = {
            final_size,
            record_alignment,
            0,
        };

        return runtime_layout_result::
            success;
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

        const auto identity =
            project.identity(
                handle);

        if (!identity ||
            identity.kind() !=
                identity_kind::type ||
            identity.slot() == 0 ||
            identity.slot() >
                output.type_handles_by_identity.size()) {

            return runtime_layout_result::invalid_input;
        }

        auto& current =
            output.type_handles_by_identity[
                identity.slot() - 1];

        if (current &&
            current != handle) {

            return runtime_layout_result::invalid_input;
        }

        current = handle;

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
                type) || !type.valid_kind()) {

            return runtime_layout_result::invalid_input;
        }

        if (type.kind == graph_type_kind::intrinsic_alias) {
            const auto result = intrinsic_layout(type.alias_intrinsic(), abi.target, value);
            if (result == runtime_layout_result::success) {
                slot.size = value.size;
                slot.alignment = static_cast<std::uint16_t>(value.alignment);
                slot.state = runtime_layout::slot_state::ready;
            }
            return result;
        }

        if (!type.defined()) {
            return runtime_layout_result::unsupported_type;
        }

        const auto is_union =
            type.record_kind ==
                graph_record_kind::union_type;

        if (!is_union &&
            type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {

            return runtime_layout_result::invalid_input;
        }

        if (is_union &&
            (type.bases.count != 0 ||
             type.polymorphic())) {

            return runtime_layout_result::invalid_input;
        }

        slot.state =
            runtime_layout::slot_state::visiting;

        if (type.bases.count != 0 ||
            type.polymorphic()) {

            const auto result =
                resolve_windows_class_record(
                    type,
                    slot,
                    value);

            if (result !=
                runtime_layout_result::success) {

                slot = {};
            }

            return result;
        }

        std::uint64_t cursor = 0;
        std::uint64_t union_size = 0;
        std::uint32_t record_alignment = 1;

        for (std::uint32_t local = 0;
             local < type.members.count;
             ++local) {

            const auto global =
                static_cast<std::uint64_t>(
                    type.members.begin) +
                local;

            if (global >=
                output.member_offsets.size()) {

                slot = {};
                return runtime_layout_result::invalid_input;
            }

            auto& member_offset =
                output.member_offsets[
                    static_cast<std::size_t>(
                        global)];

            if (member_offset !=
                invalid_record_offset) {

                slot = {};
                return runtime_layout_result::invalid_input;
            }

            member_record member;

            if (!project.member_at(
                    static_cast<std::size_t>(
                        global),
                    member)) {

                slot = {};
                return runtime_layout_result::invalid_input;
            }

            runtime_value_layout member_layout;

            const auto resolved =
                resolve(
                    member.type,
                    member_layout);

            if (resolved !=
                runtime_layout_result::success) {

                slot = {};
                return resolved;
            }

            const auto effective_alignment =
                (std::min)(
                    member_layout.alignment,
                    abi.pack);

            if (effective_alignment == 0) {
                slot = {};
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

                slot = {};
                return runtime_layout_result::overflow;
            }

            if (aligned >=
                invalid_record_offset) {

                slot = {};
                return runtime_layout_result::overflow;
            }

            member_offset =
                static_cast<record_offset>(
                    aligned);

            if (!add_u64(
                    aligned,
                    member_layout.size,
                    cursor)) {

                slot = {};
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
                final_size) ||
            final_size >
                (std::numeric_limits<record_offset>::max)()) {

            slot = {};
            return runtime_layout_result::overflow;
        }

        slot.size =
            final_size;
        slot.nonvirtual_size = static_cast<std::uint32_t>(
            is_union ? union_size : cursor);

        slot.alignment =
            static_cast<std::uint16_t>(record_alignment);

        slot.empty_record =
            !is_union &&
            type.members.count == 0;

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

        case derived_type_kind::pointer: {
            abi_properties properties;

            if (!abi_layout_properties(
                    abi.target,
                    properties)) {

                result =
                    runtime_layout_result::invalid_input;
                break;
            }

            resolved = {
                properties.pointer_size,
                properties.pointer_alignment,
                0,
            };

            result =
                runtime_layout_result::success;
            break;
        }

        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference: {
            result =
                require_unconnected(
                    derived.child);

            if (result !=
                runtime_layout_result::success) {

                break;
            }

            abi_properties properties;

            if (!abi_layout_properties(
                    abi.target,
                    properties)) {

                result =
                    runtime_layout_result::invalid_input;
                break;
            }

            resolved = {
                properties.reference_size,
                properties.reference_alignment,
                0,
            };

            break;
        }

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

        if (resolved.size >
            (std::numeric_limits<record_offset>::max)()) {

            slot.state =
                runtime_layout::slot_state::empty;

            return runtime_layout_result::overflow;
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

    // Allocated lazily only when a reachable record has multiple direct bases.
    // Generation marks reject duplicate direct bases without sort/hash/O(B^2).
    std::vector<std::uint32_t> base_marks;
    std::uint32_t base_mark_generation = 0;
};

void runtime_layout::reset() noexcept {
    type_slots.clear();
    type_handles_by_identity.clear();
    derived_slots.clear();
    member_offsets.clear();
    base_offsets.clear();
    object_offsets.clear();

    unconnected_intrinsic_offsets.fill(
        invalid_offset);

    unconnected_type_offsets.clear();
    unconnected_derived_offsets.clear();
    unconnected_types.clear();

    target_value =
        abi_target::windows_x64;

    size_value = 0;
    alignment_value = 1;
    prepared_value = false;
}

bool runtime_layout::value(
    type_ref type_value,
    runtime_value_layout& output_value) const noexcept {

    output_value = {};

    if (!prepared_value ||
        !type_value) {

        return false;
    }

    switch (type_value.kind()) {
    case type_ref_kind::intrinsic:
        if (type_value.payload() >
            static_cast<std::uint32_t>(
                intrinsic_type::nullptr_type)) {

            return false;
        }

        return intrinsic_layout(
                   static_cast<intrinsic_type>(
                       type_value.payload()),
                   target_value,
                   output_value) ==
            runtime_layout_result::success;

    case type_ref_kind::named:
        if (type_value.payload() == 0 ||
            type_value.payload() >
                type_handles_by_identity.size()) {

            return false;
        }

        {
            const auto handle =
                type_handles_by_identity[
                    type_value.payload() - 1];

            if (!handle ||
                handle.value() >
                    type_slots.size()) {

                return false;
            }

            const auto& slot =
                type_slots[
                    handle.value() - 1];

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

    case type_ref_kind::derived:
        if (type_value.payload() == 0 ||
            type_value.payload() >
                derived_slots.size()) {

            return false;
        }

        {
            const auto& slot =
                derived_slots[
                    type_value.payload() - 1];

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

    case type_ref_kind::invalid:
        break;
    }

    return false;
}

bool runtime_layout::unconnected_offset(
    type_ref type_value,
    runtime_offset& output_value) const noexcept {

    output_value = 0;

    if (!prepared_value ||
        !type_value) {

        return false;
    }

    std::uint64_t value =
        invalid_offset;

    switch (type_value.kind()) {
    case type_ref_kind::intrinsic:
        if (type_value.payload() >=
            unconnected_intrinsic_offsets.size()) {

            return false;
        }

        value =
            unconnected_intrinsic_offsets[
                type_value.payload()];
        break;

    case type_ref_kind::named:
        if (type_value.payload() == 0 ||
            type_value.payload() >
                unconnected_type_offsets.size()) {

            return false;
        }

        value =
            unconnected_type_offsets[
                type_value.payload() - 1];
        break;

    case type_ref_kind::derived:
        if (type_value.payload() == 0 ||
            type_value.payload() >
                unconnected_derived_offsets.size()) {

            return false;
        }

        value =
            unconnected_derived_offsets[
                type_value.payload() - 1];
        break;

    case type_ref_kind::invalid:
        return false;
    }

    if (value ==
            invalid_offset ||
        value ==
            pending_offset) {

        return false;
    }

    output_value = value;
    return true;
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
    record_offset& output_value) const noexcept {

    output_value = 0;

    if (index >=
            member_offsets.size() ||
        member_offsets[index] ==
            invalid_record_offset) {

        return false;
    }

    output_value =
        member_offsets[index];

    return true;
}

bool runtime_layout::base_offset(
    std::size_t index,
    record_offset& output_value) const noexcept {

    output_value = 0;

    if (index >=
            base_offsets.size() ||
        base_offsets[index] ==
            invalid_record_offset) {

        return false;
    }

    output_value =
        base_offsets[index];

    return true;
}

bool runtime_layout::object_offset(
    object_handle object,
    runtime_offset& output_value) const noexcept {

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

bool runtime_layout::release_bindings(
    runtime_binding_index& output,
    std::uint64_t target_base_address) noexcept {

    if (!prepared_value) {
        return false;
    }

    abi_properties properties;

    if (!abi_layout_properties(
            target_value,
            properties) ||
        (properties.reference_size != 4 &&
         properties.reference_size != 8)) {

        return false;
    }

    const auto address_mask =
        properties.reference_size == 4
        ? static_cast<std::uint64_t>(
            (std::numeric_limits<
                std::uint32_t>::max)())
        : (std::numeric_limits<
            std::uint64_t>::max)();

    if (target_base_address != 0 &&
        (target_base_address >
             address_mask ||
         (size_value != 0 &&
          size_value - 1 >
              address_mask -
                  target_base_address))) {

        return false;
    }

    output.target_base_address_value =
        target_base_address;

    output.reference_size_value =
        static_cast<std::uint8_t>(
            properties.reference_size);

    output.intrinsic_sizes.fill(0);

    for (std::size_t index = 1;
         index < output.intrinsic_sizes.size();
         ++index) {

        const auto type =
            static_cast<intrinsic_type>(index);

        runtime_value_layout value;

        const auto resolved =
            intrinsic_layout(
                type,
                target_value,
                value);

        if (resolved ==
            runtime_layout_result::unsupported_type) {

            continue;
        }

        if (resolved !=
                runtime_layout_result::success ||
            value.size == 0 ||
            value.size >
                (std::numeric_limits<
                    std::uint8_t>::max)()) {

            return false;
        }

        output.intrinsic_sizes[index] =
            static_cast<std::uint8_t>(
                value.size);
    }

    output.object_offsets =
        std::move(object_offsets);

    output.member_offsets =
        std::move(member_offsets);

    output.base_offsets =
        std::move(base_offsets);

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

    output.target_value =
        abi.target;

    try {
        output.type_slots.resize(
            project.type_count());

        output.type_handles_by_identity.resize(
            project.identity_count());

        output.derived_slots.resize(
            project.derived_type_count());

        output.member_offsets.assign(
            project.member_count(),
            invalid_record_offset);

        output.base_offsets.assign(
            project.base_count(),
            invalid_record_offset);

        output.object_offsets.resize(
            project.object_count());

        output.unconnected_type_offsets.assign(
            project.identity_count(),
            invalid_offset);

        output.unconnected_derived_offsets.assign(
            project.derived_type_count(),
            invalid_offset);
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
        return result;
    }

    output.prepared_value = true;

    return runtime_layout_result::success;
}

}
