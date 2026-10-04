#include "shm_layout.hpp"

#include "../abi/abi_layout.hpp"
#include "../persistence/compiled_project.hpp"
#include "../runtime/runtime_system.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace cw::server {
namespace {

constexpr shm_offset invalid_shm_offset =
    (std::numeric_limits<shm_offset>::max)();
constexpr shm_offset pending_shm_offset = invalid_shm_offset - 1;
constexpr shm_record_offset invalid_record_offset =
    (std::numeric_limits<shm_record_offset>::max)();

[[nodiscard]] bool add_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {
    if (left > (std::numeric_limits<std::uint64_t>::max)() - right) {
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
        right > (std::numeric_limits<std::uint64_t>::max)() / left) {
        return false;
    }
    output = left * right;
    return true;
}

[[nodiscard]] bool align_up(
    std::uint64_t value,
    std::uint32_t alignment,
    std::uint64_t& output) noexcept {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return false;
    }
    const auto mask = static_cast<std::uint64_t>(alignment - 1);
    if (value > (std::numeric_limits<std::uint64_t>::max)() - mask) {
        return false;
    }
    output = (value + mask) & ~mask;
    return true;
}

[[nodiscard]] shm_layout_result intrinsic_layout(
    intrinsic_type type,
    abi_target target,
    shm_value_layout& output) noexcept {
    output = {};
    abi_properties properties;
    if (!abi_layout_properties(target, properties)) {
        return shm_layout_result::invalid_input;
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
        output = {1, 1, 0}; return shm_layout_result::success;
    case intrinsic_type::wchar_type:
        output = windows ? shm_value_layout{2, 2, 0} : shm_value_layout{4, 4, 0};
        return shm_layout_result::success;
    case intrinsic_type::char16_type:
    case intrinsic_type::signed_short:
    case intrinsic_type::unsigned_short:
        output = {2, 2, 0}; return shm_layout_result::success;
    case intrinsic_type::char32_type:
    case intrinsic_type::signed_int:
    case intrinsic_type::unsigned_int:
    case intrinsic_type::float_type:
        output = {4, 4, 0}; return shm_layout_result::success;
    case intrinsic_type::signed_long:
    case intrinsic_type::unsigned_long:
        output = windows ? shm_value_layout{4, 4, 0} : shm_value_layout{8, 8, 0};
        return shm_layout_result::success;
    case intrinsic_type::signed_long_long:
    case intrinsic_type::unsigned_long_long:
    case intrinsic_type::double_type:
        output = {8, 8, 0}; return shm_layout_result::success;
    case intrinsic_type::nullptr_type:
        output = {properties.pointer_size, properties.pointer_alignment, 0};
        return shm_layout_result::success;
    case intrinsic_type::long_double_type:
        output = windows ? shm_value_layout{8, 8, 0} : shm_value_layout{16, 16, 0};
        return shm_layout_result::success;
    case intrinsic_type::void_type:
        return shm_layout_result::unsupported_type;
    case intrinsic_type::none:
        break;
    }
    return shm_layout_result::invalid_input;
}

}

class shm_layout_builder final {
public:
    shm_layout_builder(
        const compiled_project_view& project,
        const server_abi_configuration& abi,
        shm_layout& output) noexcept
        : project(project), abi(abi), output(output) {}

    [[nodiscard]] shm_layout_result build() noexcept {
        for (std::size_t index = 0;
             index < project.object_slot_count();
             ++index) {

            const auto handle =
                project.object_at(index);

            // Stable Graph WHERE may contain retired object slots.
            if (!handle) {
                continue;
            }

            object_entry object;
            shm_value_layout value;

            if (!project.object(
                    handle,
                    object)) {

                return shm_layout_result::invalid_input;
            }

            const auto result =
                resolve(
                    object.type,
                    value);

            if (result != shm_layout_result::success) {
                return result;
            }
        }

        for (std::size_t index = 0; index < output.unconnected_types.size(); ++index) {
            shm_value_layout value;
            const auto result = resolve(output.unconnected_types[index], value);
            if (result != shm_layout_result::success) {
                return result;
            }
        }

        std::uint64_t cursor = sizeof(runtime_system);
        std::uint32_t maximum_alignment = alignof(runtime_system);

        for (const auto type : output.unconnected_types) {
            shm_value_layout value;
            const auto result = resolve(type, value);
            if (result != shm_layout_result::success) {
                return result;
            }
            std::uint64_t aligned = 0;
            if (!align_up(cursor, value.alignment, aligned)) {
                return shm_layout_result::overflow;
            }
            const auto stored = store_unconnected_offset(type, aligned);
            if (stored != shm_layout_result::success) {
                return stored;
            }
            if (!add_u64(aligned, value.size, cursor)) {
                return shm_layout_result::overflow;
            }
            maximum_alignment = (std::max)(maximum_alignment, value.alignment);
        }

        for (std::size_t index = 0;
             index < project.object_slot_count();
             ++index) {

            const auto handle =
                project.object_at(index);

            // DELETE preserves WHO/WHERE lineage but owns no Runtime storage.
            if (!handle) {
                continue;
            }

            object_entry object;
            shm_value_layout value;

            if (!project.object(
                    handle,
                    object)) {

                return shm_layout_result::invalid_input;
            }

            const auto result =
                resolve(
                    object.type,
                    value);

            if (result != shm_layout_result::success) {
                return result;
            }

            std::uint64_t aligned = 0;

            if (!align_up(
                    cursor,
                    value.alignment,
                    aligned)) {

                return shm_layout_result::overflow;
            }

            if (handle.value() == 0 ||
                handle.value() >
                    output.object_offsets.size() ||
                aligned >= invalid_shm_offset) {

                return shm_layout_result::invalid_input;
            }

            output.object_offsets[
                handle.value() - 1] =
                aligned;

            if (!add_u64(
                    aligned,
                    value.size,
                    cursor)) {

                return shm_layout_result::overflow;
            }

            maximum_alignment =
                (std::max)(
                    maximum_alignment,
                    value.alignment);
        }

        std::uint64_t final_size = 0;
        if (!align_up(cursor, maximum_alignment, final_size)) {
            return shm_layout_result::overflow;
        }
        output.size_value = final_size;
        output.alignment_value = maximum_alignment;
        return shm_layout_result::success;
    }

private:
    [[nodiscard]] shm_layout_result require_unconnected(type_ref type) noexcept {
        if (!type) return shm_layout_result::invalid_input;
        shm_offset* offset = nullptr;
        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            if (type.payload() >= output.unconnected_intrinsic_offsets.size()) return shm_layout_result::invalid_input;
            offset = &output.unconnected_intrinsic_offsets[type.payload()]; break;
        case type_ref_kind::named:
            if (type.payload() == 0 || type.payload() > output.unconnected_type_offsets.size()) return shm_layout_result::invalid_input;
            offset = &output.unconnected_type_offsets[type.payload() - 1]; break;
        case type_ref_kind::derived:
            if (type.payload() == 0 || type.payload() > output.unconnected_derived_offsets.size()) return shm_layout_result::invalid_input;
            offset = &output.unconnected_derived_offsets[type.payload() - 1]; break;
        case type_ref_kind::invalid:
            return shm_layout_result::invalid_input;
        }
        if (*offset != invalid_shm_offset) return shm_layout_result::success;
        *offset = pending_shm_offset;
        try {
            output.unconnected_types.push_back(type);
        } catch (...) {
            *offset = invalid_shm_offset;
            return shm_layout_result::failed;
        }
        return shm_layout_result::success;
    }

    [[nodiscard]] shm_layout_result store_unconnected_offset(type_ref type, shm_offset value) noexcept {
        shm_offset* offset = nullptr;
        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            if (type.payload() >= output.unconnected_intrinsic_offsets.size()) return shm_layout_result::invalid_input;
            offset = &output.unconnected_intrinsic_offsets[type.payload()]; break;
        case type_ref_kind::named:
            if (type.payload() == 0 || type.payload() > output.unconnected_type_offsets.size()) return shm_layout_result::invalid_input;
            offset = &output.unconnected_type_offsets[type.payload() - 1]; break;
        case type_ref_kind::derived:
            if (type.payload() == 0 || type.payload() > output.unconnected_derived_offsets.size()) return shm_layout_result::invalid_input;
            offset = &output.unconnected_derived_offsets[type.payload() - 1]; break;
        case type_ref_kind::invalid:
            return shm_layout_result::invalid_input;
        }
        if (*offset != pending_shm_offset || value >= pending_shm_offset) {
            return shm_layout_result::invalid_input;
        }
        *offset = value;
        return shm_layout_result::success;
    }

    [[nodiscard]] shm_layout_result resolve(type_ref type, shm_value_layout& value) noexcept {
        value = {};
        if (!type) return shm_layout_result::invalid_input;
        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            if (type.payload() > static_cast<std::uint32_t>(intrinsic_type::nullptr_type)) return shm_layout_result::invalid_input;
            return intrinsic_layout(static_cast<intrinsic_type>(type.payload()), abi.target, value);
        case type_ref_kind::named: {
            const auto handle = project.type_location(type);
            return handle ? resolve_record(handle, value) : shm_layout_result::invalid_input;
        }
        case type_ref_kind::derived:
            return resolve_derived(type, value);
        case type_ref_kind::invalid:
            break;
        }
        return shm_layout_result::invalid_input;
    }

    [[nodiscard]] shm_layout_result resolve_windows_class_record(
        const type_entry& type,
        shm_layout::value_slot& slot,
        shm_value_layout& value) noexcept {
        value = {};
        if (abi.target != abi_target::windows_x86 && abi.target != abi_target::windows_x64) {
            return shm_layout_result::unsupported_type;
        }
        abi_properties properties;
        if (!abi_layout_properties(abi.target, properties)) return shm_layout_result::invalid_input;

        const auto base_begin = static_cast<std::size_t>(type.bases.begin);
        const auto base_count = static_cast<std::size_t>(type.bases.count);
        if (base_begin > output.base_offsets.size() || base_count > output.base_offsets.size() - base_begin) {
            return shm_layout_result::invalid_input;
        }

        if (base_count > 1) {
            try {
                if (base_marks.size() != output.type_slots.size()) {
                    base_marks.assign(output.type_slots.size(), 0);
                    base_mark_generation = 0;
                }
            } catch (...) {
                return shm_layout_result::failed;
            }
            ++base_mark_generation;
            if (base_mark_generation == 0) {
                std::fill(base_marks.begin(), base_marks.end(), 0);
                base_mark_generation = 1;
            }
            for (std::uint32_t local = 0; local < type.bases.count; ++local) {
                base_record base;
                if (!project.base_at(base_begin + local, base)) return shm_layout_result::invalid_input;
                const auto base_handle = project.type_location(base.type);
                if (!base_handle || base_handle.value() > output.type_slots.size()) return shm_layout_result::invalid_input;
                if (base.virtual_base()) return shm_layout_result::unsupported_type;
                auto& mark = base_marks[base_handle.value() - 1];
                if (mark == base_mark_generation) return shm_layout_result::invalid_input;
                mark = base_mark_generation;
            }
        }

        constexpr std::uint32_t no_primary = (std::numeric_limits<std::uint32_t>::max)();
        std::uint32_t primary_local = no_primary;
        std::uint32_t record_alignment = 1;
        bool single_empty_base = false;

        for (std::uint32_t local = 0; local < type.bases.count; ++local) {
            const auto global = base_begin + local;
            if (output.base_offsets[global] != invalid_record_offset) return shm_layout_result::invalid_input;
            base_record base;
            if (!project.base_at(global, base)) return shm_layout_result::invalid_input;
            if (base.virtual_base()) return shm_layout_result::unsupported_type;
            const auto base_handle = project.type_location(base.type);
            if (!base_handle || base_handle.value() > output.type_slots.size()) return shm_layout_result::invalid_input;
            shm_value_layout base_layout;
            const auto result = resolve_record(base_handle, base_layout);
            if (result != shm_layout_result::success) return result;
            if (output.type_slots[base_handle.value() - 1].empty_record) {
                if (base_count != 1) return shm_layout_result::unsupported_type;
                single_empty_base = true;
            }
            const auto effective_alignment = (std::min)(base_layout.alignment, abi.pack);
            if (effective_alignment == 0) return shm_layout_result::invalid_input;
            record_alignment = (std::max)(record_alignment, effective_alignment);
            type_entry base_type;
            if (!project.type(base_handle, base_type) || !base_type.defined() ||
                base_type.kind != graph_type_kind::record ||
                base_type.record_kind == graph_record_kind::union_type) {
                return shm_layout_result::invalid_input;
            }
            if (base_type.polymorphic() && primary_local == no_primary) primary_local = local;
        }

        for (std::uint32_t local = 0; local < type.members.count; ++local) {
            const auto global = static_cast<std::size_t>(type.members.begin) + local;
            if (global >= output.member_offsets.size() || output.member_offsets[global] != invalid_record_offset) {
                return shm_layout_result::invalid_input;
            }
            member_record member;
            if (!project.member_at(global, member)) return shm_layout_result::invalid_input;
            shm_value_layout member_layout;
            const auto result = resolve(member.type, member_layout);
            if (result != shm_layout_result::success) return result;
            const auto effective_alignment = (std::min)(member_layout.alignment, abi.pack);
            if (effective_alignment == 0) return shm_layout_result::invalid_input;
            record_alignment = (std::max)(record_alignment, effective_alignment);
        }

        const auto has_primary = primary_local != no_primary;
        if (has_primary && !type.polymorphic()) return shm_layout_result::invalid_input;
        const auto own_vfptr = type.polymorphic() && !has_primary;
        if (own_vfptr) {
            const auto a = (std::min)(properties.pointer_alignment, abi.pack);
            if (a == 0) return shm_layout_result::invalid_input;
            record_alignment = (std::max)(record_alignment, a);
        }

        std::uint64_t cursor = 0;
        const auto place_base = [&](std::uint32_t local) -> shm_layout_result {
            if (local >= type.bases.count) return shm_layout_result::invalid_input;
            const auto global = base_begin + local;
            if (output.base_offsets[global] != invalid_record_offset) return shm_layout_result::invalid_input;
            base_record base;
            if (!project.base_at(global, base)) return shm_layout_result::invalid_input;
            if (base.virtual_base()) return shm_layout_result::unsupported_type;
            const auto base_handle = project.type_location(base.type);
            if (!base_handle) return shm_layout_result::invalid_input;
            shm_value_layout base_layout;
            const auto result = resolve_record(base_handle, base_layout);
            if (result != shm_layout_result::success) return result;
            const auto effective_alignment = (std::min)(base_layout.alignment, abi.pack);
            std::uint64_t aligned = 0;
            if (!align_up(cursor, effective_alignment, aligned)) return shm_layout_result::overflow;
            if (aligned >= invalid_record_offset) return shm_layout_result::overflow;
            output.base_offsets[global] = static_cast<shm_record_offset>(aligned);
            if (!add_u64(aligned, single_empty_base ? 0 : base_layout.size, cursor)) return shm_layout_result::overflow;
            return shm_layout_result::success;
        };

        if (has_primary) {
            const auto primary_global = base_begin + primary_local;
            base_record primary;
            if (!project.base_at(primary_global, primary)) return shm_layout_result::invalid_input;
            if (primary.virtual_base()) return shm_layout_result::unsupported_type;
            const auto primary_handle = project.type_location(primary.type);
            shm_value_layout primary_layout;
            const auto primary_result = primary_handle ? resolve_record(primary_handle, primary_layout) : shm_layout_result::invalid_input;
            if (primary_result != shm_layout_result::success || output.base_offsets[primary_global] != invalid_record_offset) {
                return primary_result != shm_layout_result::success ? primary_result : shm_layout_result::invalid_input;
            }
            output.base_offsets[primary_global] = 0;
            cursor = primary_layout.size;

            for (std::uint32_t local = 0; local < type.bases.count; ++local) {
                if (local == primary_local) continue;
                base_record base; type_entry base_type;
                if (!project.base_at(base_begin + local, base)) return shm_layout_result::invalid_input;
                const auto h = project.type_location(base.type);
                if (!h || !project.type(h, base_type) || !base_type.defined()) return shm_layout_result::invalid_input;
                if (!base_type.polymorphic()) continue;
                const auto result = place_base(local);
                if (result != shm_layout_result::success) return result;
            }
            for (std::uint32_t local = 0; local < type.bases.count; ++local) {
                base_record base; type_entry base_type;
                if (!project.base_at(base_begin + local, base)) return shm_layout_result::invalid_input;
                const auto h = project.type_location(base.type);
                if (!h || !project.type(h, base_type) || !base_type.defined()) return shm_layout_result::invalid_input;
                if (base_type.polymorphic()) continue;
                const auto result = place_base(local);
                if (result != shm_layout_result::success) return result;
            }
        } else {
            if (own_vfptr) {
                if (!align_up(properties.pointer_size, record_alignment, cursor)) return shm_layout_result::overflow;
            }
            for (std::uint32_t local = 0; local < type.bases.count; ++local) {
                const auto result = place_base(local);
                if (result != shm_layout_result::success) return result;
            }
        }

        for (std::uint32_t local = 0; local < type.members.count; ++local) {
            const auto global = static_cast<std::size_t>(type.members.begin) + local;
            member_record member;
            if (!project.member_at(global, member)) return shm_layout_result::invalid_input;
            shm_value_layout member_layout;
            const auto result = resolve(member.type, member_layout);
            if (result != shm_layout_result::success) return result;
            const auto effective_alignment = (std::min)(member_layout.alignment, abi.pack);
            std::uint64_t aligned = 0;
            if (!align_up(cursor, effective_alignment, aligned)) return shm_layout_result::overflow;
            if (aligned >= invalid_record_offset) return shm_layout_result::overflow;
            output.member_offsets[global] = static_cast<shm_record_offset>(aligned);
            if (!add_u64(aligned, member_layout.size, cursor)) return shm_layout_result::overflow;
        }

        if (cursor == 0) cursor = 1;
        std::uint64_t final_size = 0;
        if (!align_up(cursor, record_alignment, final_size) || final_size > (std::numeric_limits<shm_record_offset>::max)()) {
            return shm_layout_result::overflow;
        }
        slot.size = final_size;
        slot.alignment = record_alignment;
        slot.empty_record = single_empty_base && type.members.count == 0 && !type.polymorphic();
        slot.state = shm_layout::slot_state::ready;
        value = {final_size, record_alignment, 0};
        return shm_layout_result::success;
    }

    [[nodiscard]] shm_layout_result resolve_record(type_handle handle, shm_value_layout& value) noexcept {
        value = {};
        if (!handle || handle.value() > output.type_slots.size()) return shm_layout_result::invalid_input;
        auto& slot = output.type_slots[handle.value() - 1];
        if (slot.state == shm_layout::slot_state::ready) {
            value = {slot.size, slot.alignment, 0};
            return shm_layout_result::success;
        }
        if (slot.state == shm_layout::slot_state::visiting) return shm_layout_result::invalid_input;
        type_entry type;
        if (!project.type(handle, type) || !type.valid_kind()) return shm_layout_result::invalid_input;
        if (type.kind == graph_type_kind::intrinsic_alias) {
            const auto result = intrinsic_layout(type.alias_intrinsic(), abi.target, value);
            if (result == shm_layout_result::success) {
                slot.size = value.size;
                slot.alignment = value.alignment;
                slot.state = shm_layout::slot_state::ready;
            }
            return result;
        }
        if (!type.defined()) return shm_layout_result::unsupported_type;
        const auto is_union = type.record_kind == graph_record_kind::union_type;
        if (!is_union && type.record_kind != graph_record_kind::struct_type && type.record_kind != graph_record_kind::class_type) {
            return shm_layout_result::invalid_input;
        }
        if (is_union && (type.bases.count != 0 || type.polymorphic())) return shm_layout_result::invalid_input;
        slot.state = shm_layout::slot_state::visiting;
        if (type.bases.count != 0 || type.polymorphic()) {
            const auto result = resolve_windows_class_record(type, slot, value);
            if (result != shm_layout_result::success) slot = {};
            return result;
        }

        std::uint64_t cursor = 0;
        std::uint64_t union_size = 0;
        std::uint32_t record_alignment = 1;
        for (std::uint32_t local = 0; local < type.members.count; ++local) {
            const auto global64 = static_cast<std::uint64_t>(type.members.begin) + local;
            if (global64 >= output.member_offsets.size()) { slot = {}; return shm_layout_result::invalid_input; }
            const auto global = static_cast<std::size_t>(global64);
            auto& member_offset = output.member_offsets[global];
            if (member_offset != invalid_record_offset) { slot = {}; return shm_layout_result::invalid_input; }
            member_record member;
            if (!project.member_at(global, member)) { slot = {}; return shm_layout_result::invalid_input; }
            shm_value_layout member_layout;
            const auto result = resolve(member.type, member_layout);
            if (result != shm_layout_result::success) { slot = {}; return result; }
            const auto effective_alignment = (std::min)(member_layout.alignment, abi.pack);
            if (effective_alignment == 0) { slot = {}; return shm_layout_result::invalid_input; }
            record_alignment = (std::max)(record_alignment, effective_alignment);
            if (is_union) {
                member_offset = 0;
                union_size = (std::max)(union_size, member_layout.size);
                continue;
            }
            std::uint64_t aligned = 0;
            if (!align_up(cursor, effective_alignment, aligned)) { slot = {}; return shm_layout_result::overflow; }
            if (aligned >= invalid_record_offset) { slot = {}; return shm_layout_result::overflow; }
            member_offset = static_cast<shm_record_offset>(aligned);
            if (!add_u64(aligned, member_layout.size, cursor)) { slot = {}; return shm_layout_result::overflow; }
        }
        auto raw_size = is_union ? union_size : cursor;
        if (raw_size == 0) raw_size = 1;
        std::uint64_t final_size = 0;
        if (!align_up(raw_size, record_alignment, final_size) || final_size > (std::numeric_limits<shm_record_offset>::max)()) {
            slot = {}; return shm_layout_result::overflow;
        }
        slot.size = final_size;
        slot.alignment = record_alignment;
        slot.empty_record = !is_union && type.members.count == 0;
        slot.state = shm_layout::slot_state::ready;
        value = {final_size, record_alignment, 0};
        return shm_layout_result::success;
    }

    [[nodiscard]] shm_layout_result resolve_derived(type_ref type, shm_value_layout& value) noexcept {
        value = {};
        if (type.kind() != type_ref_kind::derived || type.payload() == 0 || type.payload() > output.derived_slots.size()) {
            return shm_layout_result::invalid_input;
        }
        auto& slot = output.derived_slots[type.payload() - 1];
        if (slot.state == shm_layout::slot_state::ready) {
            value = {slot.size, slot.alignment, 0}; return shm_layout_result::success;
        }
        if (slot.state == shm_layout::slot_state::visiting) return shm_layout_result::invalid_input;
        derived_type_record derived;
        if (!project.derived(type, derived)) return shm_layout_result::invalid_input;
        slot.state = shm_layout::slot_state::visiting;
        shm_value_layout resolved;
        shm_layout_result result = shm_layout_result::invalid_input;
        switch (derived.kind) {
        case derived_type_kind::const_qualified:
        case derived_type_kind::volatile_qualified:
            result = resolve(derived.child, resolved); break;
        case derived_type_kind::pointer: {
            abi_properties p;
            if (!abi_layout_properties(abi.target, p)) { result = shm_layout_result::invalid_input; break; }
            resolved = {p.pointer_size, p.pointer_alignment, 0};
            result = shm_layout_result::success; break;
        }
        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference: {
            result = require_unconnected(derived.child);
            if (result != shm_layout_result::success) break;
            abi_properties p;
            if (!abi_layout_properties(abi.target, p)) { result = shm_layout_result::invalid_input; break; }
            resolved = {p.reference_size, p.reference_alignment, 0};
            break;
        }
        case derived_type_kind::bounded_array: {
            if (derived.payload == 0) { result = shm_layout_result::invalid_input; break; }
            shm_value_layout child;
            result = resolve(derived.child, child);
            if (result != shm_layout_result::success) break;
            std::uint64_t bytes = 0;
            if (!multiply_u64(child.size, derived.payload, bytes)) { result = shm_layout_result::overflow; break; }
            resolved = {bytes, child.alignment, 0};
            break;
        }
        case derived_type_kind::unbounded_array:
            result = shm_layout_result::unsupported_type; break;
        }
        if (result != shm_layout_result::success) { slot = {}; return result; }
        if (resolved.size > (std::numeric_limits<shm_record_offset>::max)()) { slot = {}; return shm_layout_result::overflow; }
        slot.size = resolved.size;
        slot.alignment = resolved.alignment;
        slot.state = shm_layout::slot_state::ready;
        value = resolved;
        return shm_layout_result::success;
    }

    const compiled_project_view& project;
    const server_abi_configuration& abi;
    shm_layout& output;
    std::vector<std::uint32_t> base_marks;
    std::uint32_t base_mark_generation = 0;
};

void shm_layout::reset() noexcept {
    type_slots.clear();
    derived_slots.clear();
    member_offsets.clear();
    base_offsets.clear();
    object_offsets.clear();
    unconnected_intrinsic_offsets.fill(invalid_shm_offset);
    unconnected_type_offsets.clear();
    unconnected_derived_offsets.clear();
    unconnected_types.clear();
    target_value = abi_target::windows_x64;
    size_value = 0;
    alignment_value = 1;
    prepared_value = false;
}

bool shm_layout::value(
    type_ref type_value,
    shm_value_layout& value) const noexcept {

    value = {};

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
                   value) ==
            shm_layout_result::success;

    case type_ref_kind::named:
        return false;

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

            value = {
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

bool shm_layout::type(type_handle handle, shm_value_layout& value) const noexcept {
    value = {};
    if (!prepared_value || !handle || handle.value() > type_slots.size()) return false;
    const auto& slot = type_slots[handle.value() - 1];
    if (slot.state != slot_state::ready) return false;
    value = {slot.size, slot.alignment, 0};
    return true;
}

bool shm_layout::member_offset(std::size_t index, shm_record_offset& value) const noexcept {
    value = 0;
    if (!prepared_value || index >= member_offsets.size() || member_offsets[index] == invalid_record_offset) return false;
    value = member_offsets[index]; return true;
}

bool shm_layout::base_offset(std::size_t index, shm_record_offset& value) const noexcept {
    value = 0;
    if (!prepared_value || index >= base_offsets.size() || base_offsets[index] == invalid_record_offset) return false;
    value = base_offsets[index]; return true;
}

bool shm_layout::object_offset(
    object_handle object,
    shm_offset& value) const noexcept {

    value = 0;

    if (!prepared_value ||
        !object ||
        object.value() >
            object_offsets.size()) {

        return false;
    }

    const auto stored =
        object_offsets[
            object.value() - 1];

    if (stored == invalid_shm_offset ||
        stored == pending_shm_offset) {

        return false;
    }

    value = stored;
    return true;
}

bool shm_layout::unconnected_offset(type_ref type, shm_offset& value) const noexcept {
    value = 0;
    if (!prepared_value || !type) return false;
    shm_offset stored = invalid_shm_offset;
    switch (type.kind()) {
    case type_ref_kind::intrinsic:
        if (type.payload() >= unconnected_intrinsic_offsets.size()) return false;
        stored = unconnected_intrinsic_offsets[type.payload()]; break;
    case type_ref_kind::named:
        if (type.payload() == 0 || type.payload() > unconnected_type_offsets.size()) return false;
        stored = unconnected_type_offsets[type.payload() - 1]; break;
    case type_ref_kind::derived:
        if (type.payload() == 0 || type.payload() > unconnected_derived_offsets.size()) return false;
        stored = unconnected_derived_offsets[type.payload() - 1]; break;
    case type_ref_kind::invalid:
        return false;
    }
    if (stored == invalid_shm_offset || stored == pending_shm_offset) return false;
    value = stored; return true;
}

shm_layout_result prepare_shm_layout(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    shm_layout& output) noexcept {
    output.reset();
    if (!project.valid() || !valid_abi_layout_key(make_abi_layout_key(abi))) {
        return shm_layout_result::invalid_input;
    }
    output.target_value = abi.target;
    try {
        output.type_slots.resize(project.type_slot_count());
        output.derived_slots.resize(project.derived_type_count());
        output.member_offsets.assign(project.member_count(), invalid_record_offset);
        output.base_offsets.assign(project.base_count(), invalid_record_offset);
        output.object_offsets.assign(
            project.object_slot_count(),
            invalid_shm_offset);
        output.unconnected_type_offsets.assign(project.identity_count(), invalid_shm_offset);
        output.unconnected_derived_offsets.assign(project.derived_type_count(), invalid_shm_offset);
    } catch (...) {
        output.reset(); return shm_layout_result::failed;
    }
    shm_layout_builder builder{project, abi, output};
    const auto result = builder.build();
    if (result != shm_layout_result::success) {
        output.reset(); return result;
    }
    output.prepared_value = true;
    return shm_layout_result::success;
}

}
