#include "shm_materializer.hpp"

// SHM-OBJECTS-DIAG-04

#include "../abi/abi_layout.hpp"
#include "../persistence/compiled_project.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace cw::server {
namespace {

template <typename T>
void store_native(
    std::byte* target,
    T value) noexcept {

    static_assert(
        std::is_trivially_copyable_v<T>);

    std::memcpy(
        target,
        &value,
        sizeof(value));
}

template <typename T>
[[nodiscard]] bool integer_value(
    construction_value construction,
    T& output) noexcept {

    static_assert(std::is_integral_v<T>);

    output = {};

    if (construction.kind ==
        construction_kind::zero) {

        return true;
    }

    if (construction.kind ==
        construction_kind::signed_integer) {

        const auto value =
            std::bit_cast<std::int64_t>(
                construction.bits());

        if constexpr (std::is_signed_v<T>) {
            if (value <
                    static_cast<std::int64_t>(
                        (std::numeric_limits<T>::min)()) ||
                value >
                    static_cast<std::int64_t>(
                        (std::numeric_limits<T>::max)())) {

                return false;
            }
        }
        else {
            if (value < 0 ||
                static_cast<std::uint64_t>(
                    value) >
                    static_cast<std::uint64_t>(
                        (std::numeric_limits<T>::max)())) {

                return false;
            }
        }

        output =
            static_cast<T>(
                value);

        return true;
    }

    if (construction.kind ==
        construction_kind::unsigned_integer) {

        const auto value =
            construction.bits();

        if (value >
            static_cast<std::uint64_t>(
                (std::numeric_limits<T>::max)())) {

            return false;
        }

        output =
            static_cast<T>(
                value);

        return true;
    }

    return false;
}

[[nodiscard]] bool zero_pointer_construction(
    construction_value construction) noexcept {

    if (construction.kind ==
        construction_kind::zero) {

        return true;
    }

    if (construction.kind ==
            construction_kind::signed_integer ||
        construction.kind ==
            construction_kind::unsigned_integer) {

        return construction.bits() == 0;
    }

    return false;
}

[[nodiscard]] bool host_compatible(
    abi_target target) noexcept {

#if defined(_WIN32)
    if constexpr (sizeof(void*) == 8) {
        return target ==
            abi_target::windows_x64;
    }
    else {
        return target ==
            abi_target::windows_x86;
    }
#else
    return sizeof(void*) == 8 &&
        target ==
            abi_target::posix_x64;
#endif
}

class direct_shm_materializer final {
public:
    direct_shm_materializer(
        const compiled_project_view& project,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        std::span<std::byte> shm,
        shm_materialization_telemetry* telemetry) noexcept
        : project(project),
          abi(abi),
          layout(layout),
          shm(shm),
          telemetry(telemetry) {
    }

    [[nodiscard]] shm_materialization_result
    validate() noexcept {

        if (!project.valid() ||
            layout.target() !=
                abi.target ||
            !host_compatible(
                abi.target) ||
            layout.size() >
                shm.size() ||
            (layout.size() != 0 &&
             shm.data() == nullptr)) {

            return shm_materialization_result::
                incompatible_abi;
        }

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {

            return shm_materialization_result::
                incompatible_abi;
        }

        const auto base =
            reinterpret_cast<std::uintptr_t>(
                shm.data());

        const auto mask =
            properties.reference_size == 4
            ? static_cast<std::uint64_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())
            : (std::numeric_limits<
                std::uint64_t>::max)();

        if (base == 0 ||
            base > mask ||
            (layout.size() != 0 &&
             layout.size() - 1 >
                 mask -
                     static_cast<std::uint64_t>(
                         base))) {

            return shm_materialization_result::
                overflow;
        }

        return shm_materialization_result::
            success;
    }

    [[nodiscard]] shm_materialization_result
    canonical() noexcept {

        const auto valid =
            validate();

        if (valid !=
            shm_materialization_result::
                success) {

            return valid;
        }

        for (std::size_t index = 0;
             index <
                 layout.unconnected_count();
             ++index) {

            const auto type =
                layout.unconnected_type(
                    index);

            shm_offset offset = 0;

            if (!type ||
                !layout.unconnected_offset(
                    type,
                    offset)) {

                return shm_materialization_result::
                    invalid_input;
            }

            auto* target =
                address(
                    offset);

            if (target == nullptr) {
                return shm_materialization_result::
                    invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->
                    canonical_values;
            }

            const auto result =
                canonical_value(
                    type,
                    target);

            if (result !=
                shm_materialization_result::
                    success) {

                return result;
            }
        }

        return shm_materialization_result::
            success;
    }

    [[nodiscard]] shm_materialization_result
    objects() noexcept {

        const auto valid =
            validate();

        if (valid !=
            shm_materialization_result::
                success) {

            return valid;
        }

        for (std::size_t index = 0;
             index <
                 project.object_count();
             ++index) {

            const auto handle =
                project.object_at(
                    index);

            object_entry object;
            construction_value construction;
            shm_offset offset = 0;

            if (!handle) {
                record_object_failure(10, {}, {}, {});
                return shm_materialization_result::invalid_input;
            }

            if (!project.object(handle, object)) {
                record_object_failure(11, handle, {}, {});
                return shm_materialization_result::invalid_input;
            }

            if (!project.construction(handle, construction)) {
                record_object_failure(12, handle, object.type, {});
                return shm_materialization_result::invalid_input;
            }

            if (!layout.object_offset(handle, offset)) {
                record_object_failure(13, handle, object.type, construction);
                return shm_materialization_result::invalid_input;
            }

            auto* target = address(offset);

            if (target == nullptr) {
                record_object_failure(14, handle, object.type, construction);
                return shm_materialization_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->objects;
            }

            current_object_slot = handle.value();

            const auto result =
                value(
                    object.type,
                    construction,
                    target);

            if (result !=
                shm_materialization_result::
                    success) {

                // SHM-OBJECTS-DIAG-05:
                // Preserve a more precise inner stage when present.
                record_object_failure(
                    15,
                    handle,
                    object.type,
                    construction);

                return result;
            }
        }

        return shm_materialization_result::
            success;
    }

private:
    [[nodiscard]] std::byte* address(
        shm_offset offset) const noexcept {

        if (offset >=
            layout.size()) {

            return nullptr;
        }

        return shm.data() +
            static_cast<std::size_t>(
                offset);
    }

    [[nodiscard]] std::uint64_t native_address(
        const std::byte* target) const noexcept {

        if (target == nullptr ||
            shm.data() == nullptr) {

            return 0;
        }

        const auto begin =
            reinterpret_cast<std::uintptr_t>(
                shm.data());

        const auto value =
            reinterpret_cast<std::uintptr_t>(
                target);

        if (value < begin) {
            return 0;
        }

        const auto offset =
            static_cast<std::uint64_t>(
                value - begin);

        if (offset >=
            layout.size()) {

            return 0;
        }

        return static_cast<std::uint64_t>(
            value);
    }

    [[nodiscard]] shm_materialization_result
    write_reference(
        std::byte* target,
        std::uint64_t value) noexcept {

        if (target == nullptr ||
            value == 0) {

            return shm_materialization_result::
                invalid_input;
        }

        if (properties.reference_size == 4) {
            if (value >
                (std::numeric_limits<
                    std::uint32_t>::max)()) {

                return shm_materialization_result::
                    overflow;
            }

            store_native(
                target,
                static_cast<std::uint32_t>(
                    value));

            return shm_materialization_result::
                success;
        }

        if (properties.reference_size == 8) {
            store_native(
                target,
                value);

            return shm_materialization_result::
                success;
        }

        return shm_materialization_result::
            incompatible_abi;
    }

    [[nodiscard]] bool reference_referent(
        type_ref type,
        type_ref& output) const noexcept {

        output = {};

        derived_type_record derived;

        if (!project.derived(
                type,
                derived) ||
            (derived.kind !=
                 derived_type_kind::
                     lvalue_reference &&
             derived.kind !=
                 derived_type_kind::
                     rvalue_reference)) {

            return false;
        }

        output =
            derived.child;

        return static_cast<bool>(
            output);
    }

    [[nodiscard]] shm_materialization_result
    canonical_value(
        type_ref type,
        std::byte* target) noexcept {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return shm_materialization_result::
                success;

        case type_ref_kind::named: {
            const auto handle =
                project.type_location(
                    type);

            type_entry named;

            if (!handle ||
                !project.type(
                    handle,
                    named) ||
                !named.valid_kind()) {

                return shm_materialization_result::
                    invalid_input;
            }

            // SHM-NAMED-ALIAS-01:
            // named identity may denote an intrinsic alias.
            if (named.kind ==
                graph_type_kind::
                    intrinsic_alias) {

                return shm_materialization_result::
                    success;
            }

            return canonical_record(
                handle,
                target);
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(
                    type,
                    derived)) {

                return shm_materialization_result::
                    invalid_input;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                return canonical_value(
                    derived.child,
                    target);

            case derived_type_kind::pointer:
                return shm_materialization_result::
                    success;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference: {
                shm_offset source_offset = 0;

                if (!layout.unconnected_offset(
                        derived.child,
                        source_offset)) {

                    return shm_materialization_result::
                        invalid_input;
                }

                auto* source =
                    address(
                        source_offset);

                const auto native =
                    native_address(
                        source);

                const auto written =
                    write_reference(
                        target,
                        native);

                if (written ==
                        shm_materialization_result::
                            success &&
                    telemetry != nullptr) {

                    ++telemetry->
                        canonical_references;
                }

                return written;
            }

            case derived_type_kind::bounded_array: {
                if (derived.payload == 0) {
                    return shm_materialization_result::
                        invalid_input;
                }

                shm_value_layout child_layout;

                if (!value_layout(
                        derived.child,
                        child_layout)) {

                    return shm_materialization_result::
                        invalid_input;
                }

                for (std::uint64_t index = 0;
                     index <
                         derived.payload;
                     ++index) {

                    const auto result =
                        canonical_value(
                            derived.child,
                            target +
                                static_cast<std::size_t>(
                                    index *
                                    child_layout.size));

                    if (result !=
                        shm_materialization_result::
                            success) {

                        return result;
                    }
                }

                return shm_materialization_result::
                    success;
            }

            case derived_type_kind::unbounded_array:
                return shm_materialization_result::
                    unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_materialization_result::
            invalid_input;
    }

    [[nodiscard]] shm_materialization_result
    canonical_record(
        type_handle handle,
        std::byte* base) noexcept {

        type_entry type;

        if (!project.type(
                handle,
                type) ||
            !type.defined() ||
            type.kind !=
                graph_type_kind::record) {

            return shm_materialization_result::
                invalid_input;
        }

        if (type.record_kind ==
            graph_record_kind::union_type) {

            return shm_materialization_result::
                unsupported_type;
        }

        if (type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {

            return shm_materialization_result::
                invalid_input;
        }

        for (std::uint32_t local = 0;
             local <
                 type.bases.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.bases.begin) +
                local;

            base_record child;
            shm_record_offset offset = 0;

            if (!project.base_at(
                    global,
                    child) ||
                child.virtual_base() ||
                !layout.base_offset(
                    global,
                    offset)) {

                return child.virtual_base()
                    ? shm_materialization_result::
                          unsupported_type
                    : shm_materialization_result::
                          invalid_input;
            }

            const auto child_handle =
                project.type_location(
                    child.type);

            if (!child_handle) {
                return shm_materialization_result::
                    invalid_input;
            }

            const auto result =
                canonical_record(
                    child_handle,
                    base +
                        static_cast<std::size_t>(
                            offset));

            if (result !=
                shm_materialization_result::
                    success) {

                return result;
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
            shm_record_offset offset = 0;

            if (!project.member_at(
                    global,
                    member) ||
                !layout.member_offset(
                    global,
                    offset)) {

                return shm_materialization_result::
                    invalid_input;
            }

            const auto result =
                canonical_value(
                    member.type,
                    base +
                        static_cast<std::size_t>(
                            offset));

            if (result !=
                shm_materialization_result::
                    success) {

                return result;
            }
        }

        return shm_materialization_result::
            success;
    }

    [[nodiscard]] bool value_layout(
        type_ref type,
        shm_value_layout& output) const noexcept {

        output = {};

        if (type.kind() ==
            type_ref_kind::named) {

            const auto handle =
                project.type_location(
                    type);

            return handle &&
                layout.type(
                    handle,
                    output);
        }

        return layout.value(
            type,
            output);
    }

    void record_object_failure(
        std::uint32_t stage,
        object_handle object,
        type_ref type,
        construction_value construction) noexcept {

        if (telemetry == nullptr ||
            telemetry->failure_stage != 0) {

            return;
        }

        telemetry->failure_stage = stage;
        telemetry->failure_object_slot = object.value();
        telemetry->failure_type_slot = 0;
        telemetry->failure_member_local = 0;
        telemetry->failure_member_global = 0;
        telemetry->failure_member_type = type.value();
        telemetry->failure_construction_kind =
            static_cast<std::uint32_t>(construction.kind);
        telemetry->failure_construction_operand = construction.operand;
    }

    void record_failure(
        std::uint32_t stage,
        type_handle record,
        std::uint32_t local,
        std::size_t global,
        type_ref member_type,
        construction_value construction) noexcept {

        if (telemetry == nullptr ||
            telemetry->failure_stage != 0) {

            return;
        }

        telemetry->failure_stage = stage;
        telemetry->failure_object_slot = current_object_slot;
        telemetry->failure_type_slot = record.value();
        telemetry->failure_member_local = local;
        telemetry->failure_member_global =
            static_cast<std::uint64_t>(global);
        telemetry->failure_member_type = member_type.value();
        telemetry->failure_construction_kind =
            static_cast<std::uint32_t>(construction.kind);
        telemetry->failure_construction_operand =
            construction.operand;
    }

    [[nodiscard]] shm_materialization_result
    value(
        type_ref type,
        construction_value construction,
        std::byte* target) noexcept {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return intrinsic(
                static_cast<intrinsic_type>(
                    type.payload()),
                construction,
                target);

        case type_ref_kind::named: {
            const auto handle =
                project.type_location(
                    type);

            if (!handle) {
                record_failure(
                    20,
                    {},
                    0,
                    0,
                    type,
                    construction);

                return shm_materialization_result::
                    invalid_input;
            }

            type_entry named;

            if (!project.type(
                    handle,
                    named)) {

                record_failure(
                    21,
                    handle,
                    0,
                    0,
                    type,
                    construction);

                return shm_materialization_result::
                    invalid_input;
            }

            if (!named.valid_kind()) {
                record_failure(
                    22,
                    handle,
                    0,
                    0,
                    type,
                    construction);

                return shm_materialization_result::
                    invalid_input;
            }

            // SHM-NAMED-ALIAS-01:
            // named identity may denote either a record or intrinsic alias.
            if (named.kind ==
                graph_type_kind::
                    intrinsic_alias) {

                return intrinsic(
                    named.alias_intrinsic(),
                    construction,
                    target);
            }

            if (construction.kind !=
                construction_kind::zero) {

                record_failure(
                    23,
                    handle,
                    0,
                    0,
                    type,
                    construction);

                return shm_materialization_result::
                    invalid_input;
            }

            return record(
                handle,
                target);
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(
                    type,
                    derived)) {

                return shm_materialization_result::
                    invalid_input;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                return value(
                    derived.child,
                    construction,
                    target);

            case derived_type_kind::pointer:
                if (!zero_pointer_construction(
                        construction)) {

                    return shm_materialization_result::
                        invalid_input;
                }

                if (telemetry != nullptr) {
                    ++telemetry->zero_noops;
                }

                return shm_materialization_result::
                    success;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference: {
                // SHM-TOP-REFERENCE-01:
                // References are complete type_ref values, including
                // top-level Project Objects. Zero/default construction
                // binds directly to canonical unconnected<T> storage.
                if (construction.kind !=
                    construction_kind::zero) {

                    return shm_materialization_result::
                        invalid_input;
                }

                shm_offset source_offset = 0;

                if (!layout.unconnected_offset(
                        derived.child,
                        source_offset)) {

                    return shm_materialization_result::
                        invalid_input;
                }

                auto* source =
                    address(source_offset);

                const auto native =
                    native_address(source);

                const auto written =
                    write_reference(
                        target,
                        native);

                if (written ==
                        shm_materialization_result::success &&
                    telemetry != nullptr) {

                    ++telemetry->references;
                    ++telemetry->reference_unconnected;
                }

                return written;
            }

            case derived_type_kind::bounded_array: {
                if (construction.kind !=
                        construction_kind::zero ||
                    derived.payload == 0) {

                    return shm_materialization_result::
                        invalid_input;
                }

                shm_value_layout child_layout;

                if (!value_layout(
                        derived.child,
                        child_layout)) {

                    return shm_materialization_result::
                        invalid_input;
                }

                if (telemetry != nullptr) {
                    ++telemetry->arrays;
                    telemetry->array_elements +=
                        derived.payload;
                }

                for (std::uint64_t index = 0;
                     index <
                         derived.payload;
                     ++index) {

                    const auto result =
                        value(
                            derived.child,
                            {},
                            target +
                                static_cast<std::size_t>(
                                    index *
                                    child_layout.size));

                    if (result !=
                        shm_materialization_result::
                            success) {

                        return result;
                    }
                }

                return shm_materialization_result::
                    success;
            }

            case derived_type_kind::unbounded_array:
                return shm_materialization_result::
                    unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_materialization_result::
            invalid_input;
    }

    [[nodiscard]] shm_materialization_result
    intrinsic(
        intrinsic_type type,
        construction_value construction,
        std::byte* target) noexcept {

        if (construction.kind ==
            construction_kind::zero) {

            if (telemetry != nullptr) {
                ++telemetry->zero_noops;
            }

            return type ==
                    intrinsic_type::void_type
                ? shm_materialization_result::
                      unsupported_type
                : type ==
                    intrinsic_type::none
                    ? shm_materialization_result::
                          invalid_input
                    : shm_materialization_result::
                          success;
        }

        switch (type) {
        case intrinsic_type::bool_type: {
            bool output = false;

            switch (construction.kind) {
            case construction_kind::signed_integer:
                output =
                    std::bit_cast<std::int64_t>(
                        construction.bits()) != 0;
                break;

            case construction_kind::unsigned_integer:
                output =
                    construction.bits() != 0;
                break;

            case construction_kind::real:
                output =
                    std::bit_cast<double>(
                        construction.bits()) != 0.0;
                break;

            default:
                return shm_materialization_result::
                    invalid_input;
            }

            store_native(
                target,
                output);
            break;
        }

        case intrinsic_type::char_type: {
            char output{};
            if (!integer_value(
                    construction,
                    output)) {
                return shm_materialization_result::
                    invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::signed_char: {
            signed char output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::unsigned_char: {
            unsigned char output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::wchar_type: {
            wchar_t output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::char8_type: {
            char8_t output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::char16_type: {
            char16_t output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::char32_type: {
            char32_t output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::signed_short: {
            short output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::unsigned_short: {
            unsigned short output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::signed_int: {
            int output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::unsigned_int: {
            unsigned int output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::signed_long: {
            long output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::unsigned_long: {
            unsigned long output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::signed_long_long: {
            long long output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::unsigned_long_long: {
            unsigned long long output{};
            if (!integer_value(construction, output)) {
                return shm_materialization_result::invalid_input;
            }
            store_native(target, output);
            break;
        }

        case intrinsic_type::float_type:
            return real<float>(
                construction,
                target);

        case intrinsic_type::double_type:
            return real<double>(
                construction,
                target);

        case intrinsic_type::long_double_type:
            return real<long double>(
                construction,
                target);

        case intrinsic_type::nullptr_type:
            if (!zero_pointer_construction(
                    construction)) {

                return shm_materialization_result::
                    invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->zero_noops;
            }

            return shm_materialization_result::
                success;

        case intrinsic_type::void_type:
            return shm_materialization_result::
                unsupported_type;

        case intrinsic_type::none:
            return shm_materialization_result::
                invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->scalar_writes;
        }

        return shm_materialization_result::
            success;
    }

    template <typename T>
    [[nodiscard]] shm_materialization_result
    real(
        construction_value construction,
        std::byte* target) noexcept {

        T output{};

        switch (construction.kind) {
        case construction_kind::signed_integer:
            output =
                static_cast<T>(
                    std::bit_cast<std::int64_t>(
                        construction.bits()));
            break;

        case construction_kind::unsigned_integer:
            output =
                static_cast<T>(
                    construction.bits());
            break;

        case construction_kind::real:
            output =
                static_cast<T>(
                    std::bit_cast<double>(
                        construction.bits()));
            break;

        default:
            return shm_materialization_result::
                invalid_input;
        }

        store_native(
            target,
            output);

        if (telemetry != nullptr) {
            ++telemetry->scalar_writes;
        }

        return shm_materialization_result::
            success;
    }

    [[nodiscard]] shm_materialization_result
    record(
        type_handle handle,
        std::byte* base) noexcept {

        type_entry type;

        if (!project.type(
                handle,
                type)) {

            record_failure(
                30,
                handle,
                0,
                0,
                {},
                {});

            return shm_materialization_result::
                invalid_input;
        }

        if (!type.defined() ||
            type.kind !=
                graph_type_kind::record) {

            record_failure(
                31,
                handle,
                0,
                0,
                {},
                {});

            return shm_materialization_result::
                invalid_input;
        }

        if (type.record_kind ==
            graph_record_kind::union_type) {

            record_failure(
                32,
                handle,
                0,
                0,
                {},
                {});

            return shm_materialization_result::
                unsupported_type;
        }

        if (type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {

            record_failure(
                33,
                handle,
                0,
                0,
                {},
                {});

            return shm_materialization_result::
                invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->records;
        }

        for (std::uint32_t local = 0;
             local <
                 type.bases.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.bases.begin) +
                local;

            base_record child;
            shm_record_offset offset = 0;

            if (!project.base_at(
                    global,
                    child)) {

                record_failure(
                    40,
                    handle,
                    local,
                    global,
                    {},
                    {});

                return shm_materialization_result::
                    invalid_input;
            }

            if (!layout.base_offset(
                    global,
                    offset)) {

                record_failure(
                    41,
                    handle,
                    local,
                    global,
                    {},
                    {});

                return shm_materialization_result::
                    invalid_input;
            }

            if (child.virtual_base()) {
                record_failure(
                    42,
                    handle,
                    local,
                    global,
                    {},
                    {});

                return shm_materialization_result::
                    unsupported_type;
            }

            const auto child_handle =
                project.type_location(
                    child.type);

            if (!child_handle) {
                record_failure(
                    43,
                    handle,
                    local,
                    global,
                    {},
                    {});

                return shm_materialization_result::
                    invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->bases;
            }

            const auto result =
                record(
                    child_handle,
                    base +
                        static_cast<std::size_t>(
                            offset));

            if (result !=
                shm_materialization_result::
                    success) {

                record_failure(
                    44,
                    handle,
                    local,
                    global,
                    {},
                    {});

                return result;
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
            construction_value construction;
            shm_record_offset offset = 0;

            if (!project.member_at(
                    global,
                    member) ||
                !project.construction_at(
                    global,
                    construction) ||
                !layout.member_offset(
                    global,
                    offset)) {

                return shm_materialization_result::
                    invalid_input;
            }

            auto* target =
                base +
                static_cast<std::size_t>(
                    offset);

            if (telemetry != nullptr) {
                ++telemetry->members;
            }

            type_ref referent;

            shm_materialization_result result;

            if (reference_referent(
                    member.type,
                    referent)) {

                result =
                    reference(
                        type,
                        base,
                        local,
                        construction,
                        target);
            }
            else {
                result =
                    value(
                        member.type,
                        construction,
                        target);
            }

            if (result !=
                shm_materialization_result::
                    success) {

                record_failure(
                    1,
                    handle,
                    local,
                    global,
                    member.type,
                    construction);

                return result;
            }
        }

        return shm_materialization_result::
            success;
    }

    [[nodiscard]] shm_materialization_result
    reference(
        const type_entry& record_type,
        std::byte* record_base,
        std::uint32_t local,
        construction_value,
        std::byte* target) noexcept {

        if (telemetry != nullptr) {
            ++telemetry->references;
        }

        std::uint32_t current_local =
            local;

        const auto maximum_steps =
            static_cast<std::uint64_t>(
                record_type.members.count) +
            1;

        for (std::uint64_t step = 0;
             step <
                 maximum_steps;
             ++step) {

            if (current_local >=
                record_type.members.count) {

                return shm_materialization_result::
                    invalid_input;
            }

            const auto global =
                static_cast<std::size_t>(
                    record_type.members.begin) +
                current_local;

            member_record current_member;
            construction_value current_construction;

            if (!project.member_at(
                    global,
                    current_member) ||
                !project.construction_at(
                    global,
                    current_construction)) {

                return shm_materialization_result::
                    invalid_input;
            }

            type_ref current_referent;

            if (!reference_referent(
                    current_member.type,
                    current_referent)) {

                return shm_materialization_result::
                    invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->
                    reference_chain_steps;
            }

            switch (current_construction.kind) {
            case construction_kind::zero: {
                shm_offset source_offset = 0;

                if (!layout.unconnected_offset(
                        current_referent,
                        source_offset)) {

                    return shm_materialization_result::
                        invalid_input;
                }

                auto* source =
                    address(
                        source_offset);

                const auto native =
                    native_address(
                        source);

                const auto written =
                    write_reference(
                        target,
                        native);

                if (written ==
                        shm_materialization_result::
                            success &&
                    telemetry != nullptr) {

                    ++telemetry->
                        reference_unconnected;
                }

                return written;
            }

            case construction_kind::member_binding: {
                if (current_construction.operand == 0) {
                    return shm_materialization_result::
                        invalid_input;
                }

                const auto source_local =
                    current_construction.operand -
                    1;

                if (source_local >=
                    record_type.members.count) {

                    return shm_materialization_result::
                        invalid_input;
                }

                const auto source_global =
                    static_cast<std::size_t>(
                        record_type.members.begin) +
                    source_local;

                member_record source_member;
                shm_record_offset source_offset = 0;

                if (!project.member_at(
                        source_global,
                        source_member) ||
                    !layout.member_offset(
                        source_global,
                        source_offset)) {

                    return shm_materialization_result::
                        invalid_input;
                }

                type_ref source_referent;

                if (!reference_referent(
                        source_member.type,
                        source_referent)) {

                    const auto native =
                        native_address(
                            record_base +
                                static_cast<std::size_t>(
                                    source_offset));

                    const auto written =
                        write_reference(
                            target,
                            native);

                    if (written ==
                            shm_materialization_result::
                                success &&
                        telemetry != nullptr) {

                        ++telemetry->
                            reference_member_bindings;
                    }

                    return written;
                }

                current_local =
                    source_local;
                continue;
            }

            case construction_kind::object_binding: {
                const auto identity =
                    identity_ref::from_raw(
                        current_construction.operand);

                const auto object =
                    project.object_location(
                        identity);

                shm_offset source_offset = 0;

                if (!object ||
                    !layout.object_offset(
                        object,
                        source_offset)) {

                    return shm_materialization_result::
                        invalid_input;
                }

                const auto native =
                    native_address(
                        address(
                            source_offset));

                const auto written =
                    write_reference(
                        target,
                        native);

                if (written ==
                        shm_materialization_result::
                            success &&
                    telemetry != nullptr) {

                    ++telemetry->
                        reference_object_bindings;
                }

                return written;
            }

            case construction_kind::signed_integer:
            case construction_kind::unsigned_integer:
            case construction_kind::real:
            case construction_kind::unsupported:
                return shm_materialization_result::
                    invalid_input;
            }
        }

        return shm_materialization_result::
            invalid_input;
    }

    const compiled_project_view& project;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    std::span<std::byte> shm;
    shm_materialization_telemetry* telemetry = nullptr;

    abi_properties properties{};
    std::uint32_t current_object_slot = 0;
};

}

shm_materialization_result materialize_shm_canonical(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_materialization_telemetry* telemetry) noexcept {

    direct_shm_materializer materializer{
        project,
        abi,
        layout,
        shm,
        telemetry,
    };

    return materializer.canonical();
}

shm_materialization_result materialize_shm_objects(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_materialization_telemetry* telemetry) noexcept {

    direct_shm_materializer materializer{
        project,
        abi,
        layout,
        shm,
        telemetry,
    };

    return materializer.objects();
}

}
