#include "runtime_binding.hpp"

#include "../shm/shm_layout.hpp"

#include <limits>
#include <utility>

namespace cw::server {

bool runtime_binding_index::object_offset(
    object_handle object,
    runtime_offset& output_value) const noexcept {

    output_value = 0;

    if (!object ||
        object.value() > object_offsets.size()) {

        return false;
    }

    const auto stored =
        object_offsets[
            object.value() - 1];

    if (stored ==
        (std::numeric_limits<
            runtime_offset>::max)()) {

        return false;
    }

    output_value = stored;
    return true;
}

bool runtime_binding_index::member_offset(
    std::size_t index,
    record_offset& output_value) const noexcept {

    output_value = 0;

    if (index >= member_offsets.size() ||
        member_offsets[index] ==
            (std::numeric_limits<
                record_offset>::max)()) {

        return false;
    }

    output_value = member_offsets[index];
    return true;
}

bool runtime_binding_index::base_offset(
    std::size_t index,
    record_offset& output_value) const noexcept {

    output_value = 0;

    if (index >= base_offsets.size() ||
        base_offsets[index] ==
            (std::numeric_limits<
                record_offset>::max)()) {

        return false;
    }

    output_value = base_offsets[index];
    return true;
}

bool runtime_binding_index::reference_layout(
    std::uint8_t& size,
    std::uint64_t& target_base_address) const noexcept {

    size = reference_size_value;
    target_base_address =
        target_base_address_value;

    return size == 4 ||
        size == 8;
}

bool runtime_binding_index::intrinsic_size(
    intrinsic_type type,
    std::uint8_t& output_value) const noexcept {

    output_value = 0;

    const auto index =
        static_cast<std::size_t>(
            type);

    if (index == 0 ||
        index >= intrinsic_sizes.size() ||
        intrinsic_sizes[index] == 0) {

        return false;
    }

    output_value = intrinsic_sizes[index];
    return true;
}

bool prepare_runtime_bindings(
    const compiled_project_view& project,
    const shm_layout& layout,
    const server_abi_configuration& abi,
    std::uint64_t target_base_address,
    runtime_binding_index& output) noexcept {

    output = runtime_binding_index{};

    if (!project.valid() ||
        layout.target() != abi.target) {

        return false;
    }

    abi_properties properties;

    if (!abi_layout_properties(
            abi.target,
            properties) ||
        (properties.reference_size != 4 &&
         properties.reference_size != 8)) {

        return false;
    }

    const auto mask =
        properties.reference_size == 4
        ? static_cast<std::uint64_t>(
            (std::numeric_limits<
                std::uint32_t>::max)())
        : (std::numeric_limits<
            std::uint64_t>::max)();

    if (target_base_address > mask ||
        (layout.size() != 0 &&
         layout.size() - 1 >
             mask - target_base_address)) {

        return false;
    }

    runtime_binding_index next;

    const auto intrinsic_size =
        [&](intrinsic_type type,
            std::uint8_t& size) noexcept {

            size = 0;

            const auto windows =
                abi.target ==
                    abi_target::windows_x86 ||
                abi.target ==
                    abi_target::windows_x64;

            switch (type) {
            case intrinsic_type::bool_type:
            case intrinsic_type::char_type:
            case intrinsic_type::signed_char:
            case intrinsic_type::unsigned_char:
            case intrinsic_type::char8_type:
                size = 1;
                return true;

            case intrinsic_type::wchar_type:
                size = windows ? 2 : 4;
                return true;

            case intrinsic_type::char16_type:
            case intrinsic_type::signed_short:
            case intrinsic_type::unsigned_short:
                size = 2;
                return true;

            case intrinsic_type::char32_type:
            case intrinsic_type::signed_int:
            case intrinsic_type::unsigned_int:
            case intrinsic_type::float_type:
                size = 4;
                return true;

            case intrinsic_type::signed_long:
            case intrinsic_type::unsigned_long:
                size = windows ? 4 : 8;
                return true;

            case intrinsic_type::signed_long_long:
            case intrinsic_type::unsigned_long_long:
            case intrinsic_type::double_type:
                size = 8;
                return true;

            case intrinsic_type::long_double_type:
                size = windows ? 8 : 16;
                return true;

            case intrinsic_type::nullptr_type:
                if (properties.pointer_size == 0 ||
                    properties.pointer_size >
                        (std::numeric_limits<
                            std::uint8_t>::max)()) {

                    return false;
                }

                size =
                    static_cast<std::uint8_t>(
                        properties.pointer_size);

                return true;

            case intrinsic_type::void_type:
            case intrinsic_type::none:
                return false;
            }

            return false;
        };

    try {
        next.object_offsets.assign(
            project.object_slot_count(),
            (std::numeric_limits<
                runtime_offset>::max)());

        next.member_offsets.assign(
            project.member_count(),
            (std::numeric_limits<
                record_offset>::max)());

        next.base_offsets.assign(
            project.base_count(),
            (std::numeric_limits<
                record_offset>::max)());
    }
    catch (...) {
        return false;
    }

    for (std::size_t index = 0;
         index < project.object_slot_count();
         ++index) {

        const auto object =
            project.object_at(index);

        if (!object) {
            continue;
        }

        shm_offset offset = 0;

        if (!layout.object_offset(
                object,
                offset)) {

            return false;
        }

        next.object_offsets[index] =
            static_cast<runtime_offset>(
                offset);
    }

    for (std::size_t index = 0;
         index < project.member_count();
         ++index) {

        shm_record_offset offset = 0;

        if (layout.member_offset(
                index,
                offset)) {

            next.member_offsets[index] =
                static_cast<record_offset>(
                    offset);
        }
    }

    for (std::size_t index = 0;
         index < project.base_count();
         ++index) {

        shm_record_offset offset = 0;

        if (layout.base_offset(
                index,
                offset)) {

            next.base_offsets[index] =
                static_cast<record_offset>(
                    offset);
        }
    }

    for (std::size_t index = 1;
         index <
             runtime_binding_index::
                 intrinsic_slot_count;
         ++index) {

        const auto type =
            static_cast<intrinsic_type>(
                index);

        std::uint8_t size = 0;

        if (intrinsic_size(
                type,
                size)) {

            next.intrinsic_sizes[index] =
                size;
        }
    }

    next.target_base_address_value =
        target_base_address;

    next.reference_size_value =
        static_cast<std::uint8_t>(
            properties.reference_size);

    output = std::move(next);
    return true;
}

}
