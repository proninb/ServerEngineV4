#pragma once

#include "compiled_project.hpp"
#include <cstring>
#include <algorithm>
#include <span>

namespace cw::server {

struct char_array_store final {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::uint32_t extent = 0;
    std::uint32_t mask_length = 0;
};
static_assert(sizeof(char_array_store) == 16);

inline bool apply_char_array(std::span<std::byte> destination, std::string_view data,
    std::string_view mask) noexcept {
    if (data.size() > destination.size() || mask.size() > (destination.size() + 7) / 8) return false;
    if (mask.empty()) {
        if (!data.empty()) std::memcpy(destination.data(), data.data(), data.size());
        if (data.size() < destination.size())
            std::memset(destination.data() + data.size(), 0, destination.size() - data.size());
        return true;
    }
    // Coalesce adjacent selected bytes; never touch unselected constructor data.
    std::size_t index = 0;
    while (index < destination.size() && index / 8 < mask.size()) {
        const auto selected = [&](std::size_t at) {
            return at < destination.size() && at / 8 < mask.size() &&
                (static_cast<unsigned char>(mask[at / 8]) & (1u << (at % 8)));
        };
        if (!selected(index)) { ++index; continue; }
        const auto begin = index++;
        while (selected(index)) ++index;
        const auto copied_end = (std::min)(index, data.size());
        if (begin < copied_end) std::memcpy(destination.data() + begin, data.data() + begin, copied_end - begin);
        const auto zero_begin = (std::max)(begin, data.size());
        if (zero_begin < index) std::memset(destination.data() + zero_begin, 0, index - zero_begin);
    }
    return true;
}

// Payload bytes are interned binary strings. Missing data is a zero tail;
// a missing mask means a full-array assignment.
inline bool valid_byte_array_value(const compiled_project_view& project, construction_value value) noexcept {
    if (value.kind != construction_kind::byte_array) return true;
    if (!valid_construction(value) || value.low > project.string_count() || value.high > project.string_count()) return false;
    const auto data = project.string(value.low);
    const auto mask = project.string(value.high);
    if ((value.low && data.empty()) || data.size() > value.operand ||
        (value.high && mask.empty()) || mask.size() > (std::uint64_t{value.operand} + 7) / 8) return false;
    if (value.high && value.operand % 8 && mask.size() == (std::uint64_t{value.operand} + 7) / 8 &&
        (static_cast<unsigned char>(mask.back()) >> (value.operand % 8)) != 0) return false;
    return true;
}

// A packed write must stay inside its final char array, including after
// dereferences. Never infer safety from the enclosing SHM allocation alone.
inline bool valid_byte_patch_target(const compiled_project_view& project,
    object_endpoint endpoint, construction_value value) noexcept {
    if (value.kind != construction_kind::byte_patch) return true;
    if (!valid_construction(value) || !endpoint.member.is_path()) return false;
    endpoint_path_record path;
    if (!project.endpoint_path(endpoint.member.path(), path) || path.steps.count == 0) return false;
    auto current = path.root_type;
    for (std::uint32_t i = 0; i < path.steps.count; ++i) {
        derived_type_record d;
        while (project.derived(current, d) &&
            (d.kind == derived_type_kind::const_qualified || d.kind == derived_type_kind::volatile_qualified))
            current = d.child;
        endpoint_path_step step;
        if (!project.endpoint_path_step_at(path.steps.begin + i, step)) return false;
        if (step.kind == endpoint_path_step_kind::array_index) {
            if (!project.derived(current, d) || d.kind != derived_type_kind::bounded_array || step.value >= d.payload) return false;
            if (i + 1 == path.steps.count) {
                unsigned length = 0;
                for (auto mask = value.operand; mask; mask >>= 1) ++length;
                return (step.value & 7) == 0 && length <= d.payload - step.value &&
                    d.child.kind() == type_ref_kind::intrinsic &&
                    d.child.payload() == static_cast<std::uint32_t>(intrinsic_type::char_type);
            }
            current = d.child;
        } else if (step.kind == endpoint_path_step_kind::dereference) {
            if (!project.derived(current, d) || step.value != 0 ||
                (d.kind != derived_type_kind::pointer && d.kind != derived_type_kind::lvalue_reference && d.kind != derived_type_kind::rvalue_reference)) return false;
            current = d.child;
        } else {
            type_handle owner; type_entry record;
            if (!project.named(current, owner) || !project.type(owner, record)) return false;
            if (step.kind == endpoint_path_step_kind::member) {
                member_record member;
                if (step.value >= record.members.count || !project.member_at(record.members.begin + step.value, member)) return false;
                current = member.type;
            } else if (step.kind == endpoint_path_step_kind::base) {
                base_record base;
                if (step.value >= record.bases.count || !project.base_at(record.bases.begin + step.value, base)) return false;
                current = project.named(project.find_type(base.type));
            } else return false;
        }
    }
    return false;
}

}
