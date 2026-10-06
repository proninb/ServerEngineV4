#include "shm_layout.hpp"

#include "../abi/abi_layout.hpp"
#include "../persistence/compiled_project.hpp"
#include "../persistence/runtime_project.hpp"
#include "../runtime/runtime_system.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
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

    persisted_type_slots = {};
    persisted_derived_slots = {};
    persisted_member_offsets = {};
    persisted_base_offsets = {};
    persisted_object_offsets = {};
    persisted_unconnected_intrinsic_offsets = {};
    persisted_unconnected_type_offsets = {};
    persisted_unconnected_derived_offsets = {};
    persisted_unconnected_types = {};

    target_value = abi_target::windows_x64;
    size_value = 0;
    alignment_value = 1;
    prepared_value = false;
    persisted_view = false;
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
        if (type_value.payload() == 0) {
            return false;
        }

        if (persisted_view) {
            if (type_value.payload() >
                persisted_derived_slots.size()) {
                return false;
            }

            const auto& slot =
                persisted_derived_slots[
                    type_value.payload() - 1];

            if ((slot.flags & shm_abi_value_ready_flag) == 0) {
                return false;
            }

            value = {
                slot.size,
                slot.alignment,
                0,
            };

            return true;
        }

        if (type_value.payload() > derived_slots.size()) {
            return false;
        }

        {
            const auto& slot =
                derived_slots[
                    type_value.payload() - 1];

            if (slot.state != slot_state::ready) {
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

bool shm_layout::type(
    type_handle handle,
    shm_value_layout& value) const noexcept {

    value = {};

    if (!prepared_value || !handle) {
        return false;
    }

    if (persisted_view) {
        if (handle.value() > persisted_type_slots.size()) {
            return false;
        }

        const auto& slot =
            persisted_type_slots[handle.value() - 1];

        if ((slot.flags & shm_abi_value_ready_flag) == 0) {
            return false;
        }

        value = {
            slot.size,
            slot.alignment,
            0,
        };

        return true;
    }

    if (handle.value() > type_slots.size()) {
        return false;
    }

    const auto& slot =
        type_slots[handle.value() - 1];

    if (slot.state != slot_state::ready) {
        return false;
    }

    value = {
        slot.size,
        slot.alignment,
        0,
    };

    return true;
}

bool shm_layout::member_offset(
    std::size_t index,
    shm_record_offset& value) const noexcept {

    value = 0;

    const auto values =
        persisted_view
        ? persisted_member_offsets
        : std::span<const shm_record_offset>{
            member_offsets.data(),
            member_offsets.size()};

    if (!prepared_value ||
        index >= values.size() ||
        values[index] == invalid_record_offset) {
        return false;
    }

    value = values[index];
    return true;
}

bool shm_layout::base_offset(
    std::size_t index,
    shm_record_offset& value) const noexcept {

    value = 0;

    const auto values =
        persisted_view
        ? persisted_base_offsets
        : std::span<const shm_record_offset>{
            base_offsets.data(),
            base_offsets.size()};

    if (!prepared_value ||
        index >= values.size() ||
        values[index] == invalid_record_offset) {
        return false;
    }

    value = values[index];
    return true;
}

bool shm_layout::object_offset(
    object_handle object,
    shm_offset& value) const noexcept {

    value = 0;

    const auto values =
        persisted_view
        ? persisted_object_offsets
        : std::span<const shm_offset>{
            object_offsets.data(),
            object_offsets.size()};

    if (!prepared_value ||
        !object ||
        object.value() >
            values.size()) {

        return false;
    }

    const auto stored =
        values[
            object.value() - 1];

    if (stored == invalid_shm_offset ||
        stored == pending_shm_offset) {

        return false;
    }

    value = stored;
    return true;
}

bool shm_layout::unconnected_offset(
    type_ref type,
    shm_offset& value) const noexcept {

    value = 0;

    if (!prepared_value ||
        !type) {
        return false;
    }

    shm_offset stored =
        invalid_shm_offset;

    switch (type.kind()) {
    case type_ref_kind::intrinsic: {
        const auto values =
            persisted_view
            ? persisted_unconnected_intrinsic_offsets
            : std::span<const shm_offset>{
                unconnected_intrinsic_offsets.data(),
                unconnected_intrinsic_offsets.size()};

        if (type.payload() >=
            values.size()) {
            return false;
        }

        stored =
            values[type.payload()];

        break;
    }

    case type_ref_kind::named: {
        const auto values =
            persisted_view
            ? persisted_unconnected_type_offsets
            : std::span<const shm_offset>{
                unconnected_type_offsets.data(),
                unconnected_type_offsets.size()};

        if (type.payload() == 0 ||
            type.payload() >
                values.size()) {
            return false;
        }

        stored =
            values[
                type.payload() - 1];

        break;
    }

    case type_ref_kind::derived: {
        const auto values =
            persisted_view
            ? persisted_unconnected_derived_offsets
            : std::span<const shm_offset>{
                unconnected_derived_offsets.data(),
                unconnected_derived_offsets.size()};

        if (type.payload() == 0 ||
            type.payload() >
                values.size()) {
            return false;
        }

        stored =
            values[
                type.payload() - 1];

        break;
    }

    case type_ref_kind::invalid:
        return false;
    }

    if (stored ==
            invalid_shm_offset ||
        stored ==
            pending_shm_offset) {
        return false;
    }

    value = stored;

    return true;
}

namespace {

constexpr std::array<std::byte, 8> shm_layout_image_magic{
    std::byte{'S'}, std::byte{'E'}, std::byte{'R'}, std::byte{'T'},
    std::byte{'A'}, std::byte{'B'}, std::byte{'I'}, std::byte{'1'},
};
constexpr std::uint32_t shm_layout_image_version = 1;
constexpr std::uint64_t shm_layout_image_header_size = 104;

void layout_image_write_u32(std::byte* p, std::uint32_t v) noexcept {
    std::memcpy(p, &v, sizeof(v));
}
void layout_image_write_u64(std::byte* p, std::uint64_t v) noexcept {
    std::memcpy(p, &v, sizeof(v));
}
[[nodiscard]] std::uint32_t layout_image_read_u32(const std::byte* p) noexcept {
    std::uint32_t v = 0; std::memcpy(&v, p, sizeof(v)); return v;
}
[[nodiscard]] std::uint64_t layout_image_read_u64(const std::byte* p) noexcept {
    std::uint64_t v = 0; std::memcpy(&v, p, sizeof(v)); return v;
}

}

bool shm_layout_image_size(
    std::size_t type_count,
    std::size_t derived_count,
    std::size_t member_count,
    std::size_t base_count,
    std::size_t object_count,
    std::size_t identity_count,
    std::size_t& output) noexcept {

    output = 0;
    std::uint64_t cursor = shm_layout_image_header_size;

    const auto append = [&cursor](std::uint64_t count, std::uint64_t size) noexcept {
        std::uint64_t bytes = 0;
        return multiply_u64(count, size, bytes) &&
            add_u64(cursor, bytes, cursor);
    };

    if (!append(type_count, 16) ||
        !append(derived_count, 16) ||
        !append(member_count, 4) ||
        !append(base_count, 4) ||
        !align_up(cursor, 8, cursor) ||
        !append(object_count, 8) ||
        !append(shm_layout_intrinsic_slot_count, 8) ||
        !append(identity_count, 8) ||
        !append(derived_count, 8)) {
        return false;
    }

    std::uint64_t max_unconnected = 0;
    if (!add_u64(shm_layout_intrinsic_slot_count, identity_count, max_unconnected) ||
        !add_u64(max_unconnected, derived_count, max_unconnected) ||
        !append(max_unconnected, 4) ||
        cursor > static_cast<std::uint64_t>(
            (std::numeric_limits<std::size_t>::max)())) {
        return false;
    }

    output = static_cast<std::size_t>(cursor);
    return true;
}

bool shm_layout_image_available(
    std::span<const std::byte> image) noexcept {

    return image.size() >= shm_layout_image_header_size &&
        std::equal(shm_layout_image_magic.begin(),
                   shm_layout_image_magic.end(),
                   image.begin()) &&
        layout_image_read_u32(image.data() + 8) ==
            shm_layout_image_version;
}

shm_layout_result encode_shm_layout_image(
    const shm_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> image) noexcept {

    if constexpr (std::endian::native != std::endian::little) {
        return shm_layout_result::invalid_input;
    }

    if (!layout.prepared_value || layout.target_value != abi.target) {
        return shm_layout_result::invalid_input;
    }

    std::size_t expected = 0;
    if (!shm_layout_image_size(
            layout.type_slots.size(),
            layout.derived_slots.size(),
            layout.member_offsets.size(),
            layout.base_offsets.size(),
            layout.object_offsets.size(),
            layout.unconnected_type_offsets.size(),
            expected) ||
        expected != image.size()) {
        return shm_layout_result::invalid_input;
    }

    const auto max_unconnected =
        static_cast<std::uint64_t>(shm_layout_intrinsic_slot_count) +
        layout.unconnected_type_offsets.size() +
        layout.unconnected_derived_offsets.size();

    if (layout.unconnected_types.size() > max_unconnected) {
        return shm_layout_result::invalid_input;
    }

    std::fill(image.begin(), image.end(), std::byte{0});
    auto* base = image.data();

    std::memcpy(base, shm_layout_image_magic.data(), shm_layout_image_magic.size());
    layout_image_write_u32(base + 8, shm_layout_image_version);
    layout_image_write_u32(base + 12, static_cast<std::uint32_t>(abi.target));
    layout_image_write_u32(base + 16, abi.pack);
    layout_image_write_u64(base + 24, image.size());
    layout_image_write_u64(base + 32, layout.size_value);
    layout_image_write_u32(base + 40, layout.alignment_value);
    layout_image_write_u32(base + 44,
        static_cast<std::uint32_t>(shm_layout_intrinsic_slot_count));
    layout_image_write_u64(base + 48, layout.type_slots.size());
    layout_image_write_u64(base + 56, layout.derived_slots.size());
    layout_image_write_u64(base + 64, layout.member_offsets.size());
    layout_image_write_u64(base + 72, layout.base_offsets.size());
    layout_image_write_u64(base + 80, layout.object_offsets.size());
    layout_image_write_u64(base + 88, layout.unconnected_type_offsets.size());
    layout_image_write_u64(base + 96, layout.unconnected_types.size());

    std::size_t cursor = static_cast<std::size_t>(shm_layout_image_header_size);

    const auto write_slots = [&](const auto& slots) {
        for (const auto& slot : slots) {
            if (slot.state == shm_layout::slot_state::visiting) return false;
            layout_image_write_u64(base + cursor, slot.size);
            layout_image_write_u32(base + cursor + 8, slot.alignment);
            std::uint32_t flags = 0;
            if (slot.state == shm_layout::slot_state::ready) flags |= 1u;
            if (slot.empty_record) flags |= 2u;
            layout_image_write_u32(base + cursor + 12, flags);
            cursor += 16;
        }
        return true;
    };

    if (!write_slots(layout.type_slots) ||
        !write_slots(layout.derived_slots)) {
        return shm_layout_result::invalid_input;
    }

    for (const auto v : layout.member_offsets) {
        layout_image_write_u32(base + cursor, v); cursor += 4;
    }
    for (const auto v : layout.base_offsets) {
        layout_image_write_u32(base + cursor, v); cursor += 4;
    }

    cursor = (cursor + 7u) & ~std::size_t{7u};

    for (const auto v : layout.object_offsets) {
        layout_image_write_u64(base + cursor, v); cursor += 8;
    }
    for (const auto v : layout.unconnected_intrinsic_offsets) {
        layout_image_write_u64(base + cursor, v); cursor += 8;
    }
    for (const auto v : layout.unconnected_type_offsets) {
        layout_image_write_u64(base + cursor, v); cursor += 8;
    }
    for (const auto v : layout.unconnected_derived_offsets) {
        layout_image_write_u64(base + cursor, v); cursor += 8;
    }
    for (const auto v : layout.unconnected_types) {
        layout_image_write_u32(base + cursor, v.value()); cursor += 4;
    }

    return cursor <= image.size()
        ? shm_layout_result::success
        : shm_layout_result::invalid_input;
}

shm_layout_result load_shm_layout_image(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    std::span<const std::byte> image,
    shm_layout& output) noexcept {

    output.reset();

    if constexpr (std::endian::native != std::endian::little) {
        return shm_layout_result::invalid_input;
    }

    if (!project.valid() ||
        !shm_layout_image_available(image) ||
        layout_image_read_u32(image.data() + 12) !=
            static_cast<std::uint32_t>(abi.target) ||
        layout_image_read_u32(image.data() + 16) != abi.pack ||
        layout_image_read_u32(image.data() + 20) != 0 ||
        layout_image_read_u64(image.data() + 24) != image.size() ||
        layout_image_read_u32(image.data() + 44) !=
            shm_layout_intrinsic_slot_count ||
        layout_image_read_u64(image.data() + 48) != project.type_slot_count() ||
        layout_image_read_u64(image.data() + 56) != project.derived_type_count() ||
        layout_image_read_u64(image.data() + 64) != project.member_count() ||
        layout_image_read_u64(image.data() + 72) != project.base_count() ||
        layout_image_read_u64(image.data() + 80) != project.object_slot_count() ||
        layout_image_read_u64(image.data() + 88) != project.identity_count()) {
        return shm_layout_result::invalid_input;
    }

    const auto unconnected_count = layout_image_read_u64(image.data() + 96);

    std::size_t expected = 0;
    if (!shm_layout_image_size(
            project.type_slot_count(),
            project.derived_type_count(),
            project.member_count(),
            project.base_count(),
            project.object_slot_count(),
            project.identity_count(),
            expected) ||
        expected != image.size()) {
        return shm_layout_result::invalid_input;
    }

    const auto max_unconnected =
        static_cast<std::uint64_t>(shm_layout_intrinsic_slot_count) +
        project.identity_count() +
        project.derived_type_count();

    if (unconnected_count > max_unconnected) {
        return shm_layout_result::invalid_input;
    }

    try {
        output.type_slots.resize(project.type_slot_count());
        output.derived_slots.resize(project.derived_type_count());
        output.member_offsets.resize(project.member_count());
        output.base_offsets.resize(project.base_count());
        output.object_offsets.resize(project.object_slot_count());
        output.unconnected_type_offsets.resize(project.identity_count());
        output.unconnected_derived_offsets.resize(project.derived_type_count());
        output.unconnected_types.reserve(static_cast<std::size_t>(unconnected_count));
    }
    catch (...) {
        output.reset();
        return shm_layout_result::failed;
    }

    std::size_t cursor = static_cast<std::size_t>(shm_layout_image_header_size);

    const auto read_slots = [&](auto& slots) {
        for (auto& slot : slots) {
            slot.size = layout_image_read_u64(image.data() + cursor);
            slot.alignment = layout_image_read_u32(image.data() + cursor + 8);
            const auto flags = layout_image_read_u32(image.data() + cursor + 12);

            if ((flags & ~3u) != 0 ||
                ((flags & 1u) != 0 &&
                 (slot.alignment == 0 ||
                  (slot.alignment & (slot.alignment - 1)) != 0)) ||
                ((flags & 1u) == 0 &&
                 (slot.size != 0 || slot.alignment != 0 || (flags & 2u) != 0))) {
                return false;
            }

            slot.state = (flags & 1u) != 0
                ? shm_layout::slot_state::ready
                : shm_layout::slot_state::empty;
            slot.empty_record = (flags & 2u) != 0;
            cursor += 16;
        }
        return true;
    };

    if (!read_slots(output.type_slots) ||
        !read_slots(output.derived_slots)) {
        output.reset();
        return shm_layout_result::invalid_input;
    }

    for (auto& v : output.member_offsets) {
        v = layout_image_read_u32(image.data() + cursor); cursor += 4;
    }
    for (auto& v : output.base_offsets) {
        v = layout_image_read_u32(image.data() + cursor); cursor += 4;
    }

    cursor = (cursor + 7u) & ~std::size_t{7u};

    for (auto& v : output.object_offsets) {
        v = layout_image_read_u64(image.data() + cursor); cursor += 8;
    }
    for (auto& v : output.unconnected_intrinsic_offsets) {
        v = layout_image_read_u64(image.data() + cursor); cursor += 8;
    }
    for (auto& v : output.unconnected_type_offsets) {
        v = layout_image_read_u64(image.data() + cursor); cursor += 8;
    }
    for (auto& v : output.unconnected_derived_offsets) {
        v = layout_image_read_u64(image.data() + cursor); cursor += 8;
    }

    for (std::size_t i = 0;
         i < static_cast<std::size_t>(unconnected_count);
         ++i) {
        const auto raw = layout_image_read_u32(image.data() + cursor);
        type_ref value;
        static_assert(sizeof(value) == sizeof(raw));
        std::memcpy(&value, &raw, sizeof(value));
        if (!value) {
            output.reset();
            return shm_layout_result::invalid_input;
        }
        output.unconnected_types.push_back(value);
        cursor += 4;
    }

    const auto runtime_size = layout_image_read_u64(image.data() + 32);
    const auto runtime_alignment = layout_image_read_u32(image.data() + 40);

    if (runtime_size == 0 ||
        runtime_size >= invalid_shm_offset ||
        runtime_alignment == 0 ||
        (runtime_alignment & (runtime_alignment - 1)) != 0 ||
        cursor > image.size()) {
        output.reset();
        return shm_layout_result::invalid_input;
    }

    output.target_value = abi.target;
    output.size_value = runtime_size;
    output.alignment_value = runtime_alignment;
    output.prepared_value = true;
    return shm_layout_result::success;
}


namespace {

constexpr std::array<std::byte, 8> shm_abi_column_magic{
    std::byte{'S'}, std::byte{'E'}, std::byte{'A'}, std::byte{'B'},
    std::byte{'I'}, std::byte{'C'}, std::byte{'0'}, std::byte{'1'},
};

constexpr std::uint32_t shm_abi_column_version = 1;
constexpr std::uint32_t shm_abi_value_ready = 0x01u;
constexpr std::uint32_t shm_abi_value_empty_record = 0x02u;

struct shm_abi_header_record final {
    std::array<std::byte, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t target = 0;
    std::uint32_t pack = 0;
    std::uint32_t reserved0 = 0;
    std::uint64_t runtime_size = 0;
    std::uint32_t runtime_alignment = 0;
    std::uint32_t unconnected_count = 0;
    std::uint64_t reserved1 = 0;
    std::uint64_t reserved2 = 0;
    std::uint64_t reserved3 = 0;
};

static_assert(sizeof(shm_abi_header_record) == 64);

template <typename T>
[[nodiscard]] bool column_records(
    std::span<const std::byte> bytes,
    std::size_t count,
    std::span<const T>& output) noexcept {

    output = {};

    if (count >
            (std::numeric_limits<std::size_t>::max)() /
                sizeof(T) ||
        bytes.size() != count * sizeof(T) ||
        (bytes.data() != nullptr &&
         reinterpret_cast<std::uintptr_t>(bytes.data()) %
             alignof(T) != 0)) {
        return false;
    }

    output = {
        reinterpret_cast<const T*>(bytes.data()),
        count,
    };

    return true;
}

template <typename T>
[[nodiscard]] bool mutable_column_records(
    std::span<std::byte> bytes,
    std::size_t count,
    std::span<T>& output) noexcept {

    output = {};

    if (count >
            (std::numeric_limits<std::size_t>::max)() /
                sizeof(T) ||
        bytes.size() != count * sizeof(T) ||
        (bytes.data() != nullptr &&
         reinterpret_cast<std::uintptr_t>(bytes.data()) %
             alignof(T) != 0)) {
        return false;
    }

    output = {
        reinterpret_cast<T*>(bytes.data()),
        count,
    };

    return true;
}

}

shm_layout_result encode_shm_layout_columns(
    const shm_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> compiled_image) noexcept {

    if constexpr (std::endian::native != std::endian::little) {
        return shm_layout_result::invalid_input;
    }

    if (!layout.prepared_value ||
        layout.persisted_view ||
        layout.target_value != abi.target ||
        layout.unconnected_types.size() >
            (std::numeric_limits<std::uint32_t>::max)()) {
        return shm_layout_result::invalid_input;
    }

    const auto section =
        [&](compiled_project_section kind) noexcept {
            return runtime_project_mutable_section(
                compiled_image,
                kind);
        };

    std::span<shm_abi_header_record> header;
    std::span<shm_abi_value_record> type_abi;
    std::span<shm_abi_value_record> derived_abi;
    std::span<shm_record_offset> member_abi;
    std::span<shm_record_offset> base_abi;
    std::span<shm_offset> object_abi;
    std::span<shm_offset> unconnected_intrinsic;
    std::span<shm_offset> unconnected_type;
    std::span<shm_offset> unconnected_derived;
    std::span<type_ref> unconnected_types_out;

    if (!mutable_column_records(
            section(compiled_project_section::runtime_abi_header),
            1,
            header) ||
        !mutable_column_records(
            section(compiled_project_section::type_abi),
            layout.type_slots.size(),
            type_abi) ||
        !mutable_column_records(
            section(compiled_project_section::derived_abi),
            layout.derived_slots.size(),
            derived_abi) ||
        !mutable_column_records(
            section(compiled_project_section::member_abi),
            layout.member_offsets.size(),
            member_abi) ||
        !mutable_column_records(
            section(compiled_project_section::base_abi),
            layout.base_offsets.size(),
            base_abi) ||
        !mutable_column_records(
            section(compiled_project_section::object_abi),
            layout.object_offsets.size(),
            object_abi) ||
        !mutable_column_records(
            section(compiled_project_section::unconnected_intrinsic_abi),
            shm_layout_intrinsic_slot_count,
            unconnected_intrinsic) ||
        !mutable_column_records(
            section(compiled_project_section::unconnected_type_abi),
            layout.unconnected_type_offsets.size(),
            unconnected_type) ||
        !mutable_column_records(
            section(compiled_project_section::unconnected_derived_abi),
            layout.unconnected_derived_offsets.size(),
            unconnected_derived)) {
        return shm_layout_result::invalid_input;
    }

    const auto unconnected_capacity =
        shm_layout_intrinsic_slot_count +
        layout.unconnected_type_offsets.size() +
        layout.unconnected_derived_offsets.size();

    if (!mutable_column_records(
            section(compiled_project_section::unconnected_types),
            unconnected_capacity,
            unconnected_types_out) ||
        layout.unconnected_types.size() > unconnected_types_out.size()) {
        return shm_layout_result::invalid_input;
    }

    for (std::size_t index = 0;
         index < layout.type_slots.size();
         ++index) {

        const auto& source = layout.type_slots[index];
        auto& target = type_abi[index];

        target = {source.size, source.alignment, 0};

        if (source.state == shm_layout::slot_state::ready) {
            target.flags |= shm_abi_value_ready;
        }

        if (source.empty_record) {
            target.flags |= shm_abi_value_empty_record;
        }
    }

    for (std::size_t index = 0;
         index < layout.derived_slots.size();
         ++index) {

        const auto& source = layout.derived_slots[index];
        auto& target = derived_abi[index];

        target = {source.size, source.alignment, 0};

        if (source.state == shm_layout::slot_state::ready) {
            target.flags |= shm_abi_value_ready;
        }

        if (source.empty_record) {
            target.flags |= shm_abi_value_empty_record;
        }
    }

    std::copy(
        layout.member_offsets.begin(),
        layout.member_offsets.end(),
        member_abi.begin());

    std::copy(
        layout.base_offsets.begin(),
        layout.base_offsets.end(),
        base_abi.begin());

    std::copy(
        layout.object_offsets.begin(),
        layout.object_offsets.end(),
        object_abi.begin());

    std::copy(
        layout.unconnected_intrinsic_offsets.begin(),
        layout.unconnected_intrinsic_offsets.end(),
        unconnected_intrinsic.begin());

    std::copy(
        layout.unconnected_type_offsets.begin(),
        layout.unconnected_type_offsets.end(),
        unconnected_type.begin());

    std::copy(
        layout.unconnected_derived_offsets.begin(),
        layout.unconnected_derived_offsets.end(),
        unconnected_derived.begin());

    std::fill(
        unconnected_types_out.begin(),
        unconnected_types_out.end(),
        type_ref{});

    std::copy(
        layout.unconnected_types.begin(),
        layout.unconnected_types.end(),
        unconnected_types_out.begin());

    header.front() = {};
    header.front().magic = shm_abi_column_magic;
    header.front().version = shm_abi_column_version;
    header.front().target =
        static_cast<std::uint32_t>(abi.target);
    header.front().pack = abi.pack;
    header.front().runtime_size = layout.size_value;
    header.front().runtime_alignment = layout.alignment_value;
    header.front().unconnected_count =
        static_cast<std::uint32_t>(
            layout.unconnected_types.size());

    return shm_layout_result::success;
}

void attach_shm_layout_columns(
    const runtime_project_view& project,
    shm_layout& output) noexcept {

    output.reset();

    const auto records =
        []<typename T>(
            std::span<const std::byte> bytes) noexcept {

            return std::span<const T>{
                reinterpret_cast<const T*>(
                    bytes.data()),
                bytes.size() /
                    sizeof(T),
            };
        };

    const auto header =
        records.template operator()<
            shm_abi_header_record>(
                project.section(
                    compiled_project_section::
                        runtime_abi_header));

    output.persisted_type_slots =
        records.template operator()<
            shm_abi_value_record>(
                project.section(
                    compiled_project_section::
                        type_abi));

    output.persisted_derived_slots =
        records.template operator()<
            shm_abi_value_record>(
                project.section(
                    compiled_project_section::
                        derived_abi));

    output.persisted_member_offsets =
        records.template operator()<
            shm_record_offset>(
                project.section(
                    compiled_project_section::
                        member_abi));

    output.persisted_base_offsets =
        records.template operator()<
            shm_record_offset>(
                project.section(
                    compiled_project_section::
                        base_abi));

    output.persisted_object_offsets =
        records.template operator()<
            shm_offset>(
                project.section(
                    compiled_project_section::
                        object_abi));

    output.persisted_unconnected_intrinsic_offsets =
        records.template operator()<
            shm_offset>(
                project.section(
                    compiled_project_section::
                        unconnected_intrinsic_abi));

    output.persisted_unconnected_type_offsets =
        records.template operator()<
            shm_offset>(
                project.section(
                    compiled_project_section::
                        unconnected_type_abi));

    output.persisted_unconnected_derived_offsets =
        records.template operator()<
            shm_offset>(
                project.section(
                    compiled_project_section::
                        unconnected_derived_abi));

    const auto unconnected_types =
        records.template operator()<
            type_ref>(
                project.section(
                    compiled_project_section::
                        unconnected_types));

    const auto& value =
        header.front();

    output.persisted_unconnected_types = {
        unconnected_types.data(),
        static_cast<std::size_t>(
            value.unconnected_count),
    };

    output.target_value =
        static_cast<abi_target>(
            value.target);

    output.size_value =
        value.runtime_size;

    output.alignment_value =
        value.runtime_alignment;

    output.prepared_value = true;
    output.persisted_view = true;
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
