#include "fixed_direct_materializer.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace cw::server {
namespace {

template <typename T>
void store_native(
    std::byte* target,
    const T& value) noexcept {

    std::memcpy(
        target,
        &value,
        sizeof(T));
}

template <typename T>
[[nodiscard]] bool integer_value(
    construction_value construction,
    T& output) noexcept {

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

        if constexpr (
            std::is_signed_v<T>) {

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

template <typename T>
[[nodiscard]] fixed_direct_materialization_result
write_integer(
    construction_value construction,
    std::byte* target) noexcept {

    T value{};

    if (!integer_value(
            construction,
            value)) {

        return fixed_direct_materialization_result::
            invalid_input;
    }

    store_native(
        target,
        value);

    return fixed_direct_materialization_result::success;
}

template <typename T>
[[nodiscard]] fixed_direct_materialization_result
write_real(
    construction_value construction,
    std::byte* target) noexcept {

    T value{};

    switch (construction.kind) {
    case construction_kind::zero:
        break;

    case construction_kind::signed_integer:
        value =
            static_cast<T>(
                std::bit_cast<std::int64_t>(
                    construction.bits()));
        break;

    case construction_kind::unsigned_integer:
        value =
            static_cast<T>(
                construction.bits());
        break;

    case construction_kind::real:
        value =
            static_cast<T>(
                std::bit_cast<double>(
                    construction.bits()));
        break;

    case construction_kind::member_binding:
    case construction_kind::object_binding:
    case construction_kind::unsupported:
        return fixed_direct_materialization_result::
            invalid_input;
    }

    store_native(
        target,
        value);

    return fixed_direct_materialization_result::success;
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


class fixed_direct_materializer final {
public:
    fixed_direct_materializer(
        const compiled_project_view& project,
        const runtime_layout& layout,
        const server_abi_configuration& abi,
        std::uint64_t target_base_address,
        std::span<std::byte> runtime) noexcept
        : project(project),
          layout(layout),
          abi(abi),
          target_base_address(target_base_address),
          runtime(runtime) {
    }

    [[nodiscard]] fixed_direct_materialization_result
    run() noexcept {

        if (!project.valid() ||
            layout.size() >
                runtime.size() ||
            (layout.size() != 0 &&
             runtime.data() == nullptr)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (!fixed_direct_host_compatible(
                abi) ||
            layout.target() !=
                abi.target) {

            return fixed_direct_materialization_result::
                incompatible_abi;
        }

        if (!abi_layout_properties(
                abi.target,
                properties)) {

            return fixed_direct_materialization_result::
                incompatible_abi;
        }

        if (!fixed_direct_target_range_compatible(
                abi,
                target_base_address,
                layout.size())) {

            return fixed_direct_materialization_result::
                overflow;
        }

        if (layout.size() >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {

            return fixed_direct_materialization_result::
                overflow;
        }

        const auto logical_size =
            static_cast<std::size_t>(
                layout.size());

        try {
            record_plans.resize(
                project.type_count());

            link_target_slots.resize(
                project.link_count());
        }
        catch (...) {
            return fixed_direct_materialization_result::
                failed;
        }

        std::fill_n(
            runtime.data(),
            logical_size,
            std::byte{0});

        for (std::size_t index = 0;
             index <
                 layout.unconnected_count();
             ++index) {

            const auto type =
                layout.unconnected_type(
                    index);

            std::uint64_t offset = 0;

            if (!type ||
                !layout.unconnected_offset(
                    type,
                    offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* target =
                address(
                    offset);

            if (target == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto materialized =
                canonical_value(
                    type,
                    target);

            if (materialized !=
                fixed_direct_materialization_result::
                    success) {

                return materialized;
            }
        }

        const auto links_marked =
            mark_link_targets();

        if (links_marked !=
            fixed_direct_materialization_result::
                success) {

            return links_marked;
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

            std::uint64_t offset = 0;

            if (!handle ||
                !project.object(
                    handle,
                    object) ||
                !project.construction(
                    handle,
                    construction) ||
                !layout.object_offset(
                    handle,
                    offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* target =
                address(
                    offset);

            if (target == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto materialized =
                normal_value(
                    object.type,
                    construction,
                    target,
                    handle);

            if (materialized !=
                fixed_direct_materialization_result::
                    success) {

                return materialized;
            }
        }

        const auto initialized =
            apply_object_initializations();

        if (initialized !=
            fixed_direct_materialization_result::
                success) {

            return initialized;
        }

        const auto links_materialized =
            materialize_links();

        if (links_materialized !=
            fixed_direct_materialization_result::
                success) {

            return links_materialized;
        }

        return fixed_direct_materialization_result::
            success;
    }

private:
    [[nodiscard]] std::uint64_t target_word_mask() const noexcept {
        return properties.reference_size == 4
            ? static_cast<std::uint64_t>(
                (std::numeric_limits<std::uint32_t>::max)())
            : (std::numeric_limits<std::uint64_t>::max)();
    }

    void store_target_word(
        std::byte* slot,
        std::uint64_t value) const noexcept {

        if (properties.reference_size == 4) {
            const auto narrowed =
                static_cast<std::uint32_t>(
                    value);

            store_native(
                slot,
                narrowed);
            return;
        }

        store_native(
            slot,
            value);
    }

    [[nodiscard]] std::uint64_t target_address(
        const std::byte* value) const noexcept {

        if (value == nullptr ||
            runtime.data() == nullptr) {

            return 0;
        }

        const auto host_base =
            reinterpret_cast<std::uintptr_t>(
                runtime.data());

        const auto host_value =
            reinterpret_cast<std::uintptr_t>(
                value);

        if (host_value < host_base) {
            return 0;
        }

        const auto offset =
            static_cast<std::uint64_t>(
                host_value - host_base);

        if (offset >=
                layout.size() ||
            target_base_address >
                (std::numeric_limits<std::uint64_t>::max)() -
                    offset) {

            return 0;
        }

        return
            target_base_address +
            offset;
    }

    [[nodiscard]] std::byte* address(
        std::uint64_t offset) const noexcept {

        if (offset >=
            layout.size()) {

            return nullptr;
        }

        return runtime.data() +
            static_cast<std::size_t>(
                offset);
    }

    [[nodiscard]] bool value_fits(
        std::uint64_t offset,
        std::uint64_t size) const noexcept {

        return offset <=
                layout.size() &&
            size <=
                layout.size() - offset;
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
                 derived_type_kind::lvalue_reference &&
             derived.kind !=
                 derived_type_kind::rvalue_reference)) {

            return false;
        }

        output = derived.child;
        return static_cast<bool>(
            output);
    }

    [[nodiscard]] bool record_type(
        type_ref type,
        type_handle& output) const noexcept {

        output = {};

        for (;;) {
            if (type.kind() ==
                type_ref_kind::named) {

                return project.named(
                    type,
                    output);
            }

            if (type.kind() !=
                type_ref_kind::derived) {

                return false;
            }

            derived_type_record derived;

            if (!project.derived(
                    type,
                    derived) ||
                (derived.kind !=
                     derived_type_kind::const_qualified &&
                 derived.kind !=
                     derived_type_kind::volatile_qualified)) {

                return false;
            }

            type = derived.child;
        }
    }

    [[nodiscard]] fixed_direct_materialization_result
    write_address(
        std::byte* slot,
        std::uint64_t target) noexcept {

        if (slot == nullptr) {
            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (!runtime_address(
                target)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (target >
            target_word_mask()) {

            return fixed_direct_materialization_result::
                overflow;
        }

        store_target_word(
            slot,
            target);

        return fixed_direct_materialization_result::success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    canonical_value(
        type_ref type,
        std::byte* target) noexcept {

        runtime_value_layout value;

        if (!layout.value(
                type,
                value)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            std::fill_n(
                target,
                static_cast<std::size_t>(
                    value.size),
                std::byte{0});

            return fixed_direct_materialization_result::
                success;

        case type_ref_kind::named: {
            type_handle handle;

            return project.named(
                       type,
                       handle)
                ? canonical_record(
                    handle,
                    target)
                : fixed_direct_materialization_result::
                    invalid_input;
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(
                    type,
                    derived)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                return canonical_value(
                    derived.child,
                    target);

            case derived_type_kind::pointer:
                std::fill_n(
                    target,
                    static_cast<std::size_t>(
                        value.size),
                    std::byte{0});

                return fixed_direct_materialization_result::
                    success;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference: {
                std::uint64_t offset = 0;

                if (!layout.unconnected_offset(
                        derived.child,
                        offset)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto* value_target =
                    address(
                        offset);

                if (value_target == nullptr) {
                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                return write_address(
                    target,
                    target_address(value_target));
            }

            case derived_type_kind::bounded_array: {
                if (derived.payload == 0) {
                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                runtime_value_layout child;

                if (!layout.value(
                        derived.child,
                        child)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                for (std::uint64_t index = 0;
                     index <
                         derived.payload;
                     ++index) {

                    const auto materialized =
                        canonical_value(
                            derived.child,
                            target +
                                static_cast<std::size_t>(
                                    index *
                                    child.size));

                    if (materialized !=
                        fixed_direct_materialization_result::
                            success) {

                        return materialized;
                    }
                }

                return fixed_direct_materialization_result::
                    success;
            }

            case derived_type_kind::unbounded_array:
                return fixed_direct_materialization_result::
                    unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return fixed_direct_materialization_result::
            invalid_input;
    }

    [[nodiscard]] fixed_direct_materialization_result
    canonical_bases(
        const type_entry& type,
        std::byte* base) noexcept {

        for (std::uint32_t local = 0;
             local <
                 type.bases.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.bases.begin) +
                local;

            base_record base_record_value;
            record_offset offset = 0;

            if (!project.base_at(
                    global,
                    base_record_value)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            if (base_record_value.virtual_base()) {
                return fixed_direct_materialization_result::
                    unsupported_type;
            }

            if (!layout.base_offset(
                    global,
                    offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto materialized =
                canonical_record(
                    base_record_value.type,
                    base +
                        static_cast<std::size_t>(
                            offset));

            if (materialized !=
                fixed_direct_materialization_result::
                    success) {

                return materialized;
            }
        }

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    canonical_record(
        type_handle handle,
        std::byte* base) noexcept {

        type_entry type;

        if (!handle ||
            !project.type(
                handle,
                type) ||
            !type.defined() ||
            type.kind !=
                graph_type_kind::record) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (type.record_kind ==
            graph_record_kind::union_type) {

            return fixed_direct_materialization_result::
                unsupported_type;
        }

        if (type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (type.bases.count != 0) {
            const auto bases_materialized =
                canonical_bases(
                    type,
                    base);

            if (bases_materialized !=
                fixed_direct_materialization_result::
                    success) {

                return bases_materialized;
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
            record_offset offset = 0;

            if (!project.member_at(
                    global,
                    member) ||
                !layout.member_offset(
                    global,
                    offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* target =
                base +
                static_cast<std::size_t>(
                    offset);

            type_ref referent;

            if (reference_referent(
                    member.type,
                    referent)) {

                std::uint64_t sentinel = 0;

                if (!layout.unconnected_offset(
                        referent,
                        sentinel)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto* value_target =
                    address(
                        sentinel);

                if (value_target == nullptr) {
                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto written =
                    write_address(
                        target,
                        target_address(value_target));

                if (written !=
                    fixed_direct_materialization_result::
                        success) {

                    return written;
                }

                continue;
            }

            const auto materialized =
                canonical_value(
                    member.type,
                    target);

            if (materialized !=
                fixed_direct_materialization_result::
                    success) {

                return materialized;
            }
        }

        return fixed_direct_materialization_result::success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    normal_value(
        type_ref type,
        construction_value construction,
        std::byte* target,
        object_handle top_object) noexcept {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return normal_intrinsic(
                static_cast<intrinsic_type>(
                    type.payload()),
                construction,
                target);

        case type_ref_kind::named: {
            if (construction.kind !=
                construction_kind::zero) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            type_handle handle;

            return project.named(
                       type,
                       handle)
                ? normal_record(
                    handle,
                    target,
                    top_object)
                : fixed_direct_materialization_result::
                    invalid_input;
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(
                    type,
                    derived)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                return normal_value(
                    derived.child,
                    construction,
                    target,
                    top_object);

            case derived_type_kind::pointer: {
                if (!zero_pointer_construction(
                        construction)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                store_target_word(
                    target,
                    0);

                return fixed_direct_materialization_result::
                    success;
            }

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference: {
                if (construction.kind !=
                    construction_kind::zero) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                std::uint64_t sentinel = 0;

                if (!layout.unconnected_offset(
                        derived.child,
                        sentinel)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto* value_target =
                    address(
                        sentinel);

                if (value_target == nullptr) {
                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                return write_address(
                    target,
                    target_address(value_target));
            }

            case derived_type_kind::bounded_array: {
                if (construction.kind !=
                        construction_kind::zero ||
                    derived.payload == 0) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                runtime_value_layout child;

                if (!layout.value(
                        derived.child,
                        child)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                for (std::uint64_t index = 0;
                     index <
                         derived.payload;
                     ++index) {

                    const auto materialized =
                        normal_value(
                            derived.child,
                            {},
                            target +
                                static_cast<std::size_t>(
                                    index *
                                    child.size),
                            {});

                    if (materialized !=
                        fixed_direct_materialization_result::
                            success) {

                        return materialized;
                    }
                }

                return fixed_direct_materialization_result::
                    success;
            }

            case derived_type_kind::unbounded_array:
                return fixed_direct_materialization_result::
                    unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return fixed_direct_materialization_result::
            invalid_input;
    }

    [[nodiscard]] fixed_direct_materialization_result
    normal_intrinsic(
        intrinsic_type type,
        construction_value construction,
        std::byte* target) noexcept {

        switch (type) {
        case intrinsic_type::bool_type: {
            bool value = false;

            switch (construction.kind) {
            case construction_kind::zero:
                break;

            case construction_kind::signed_integer:
                value =
                    std::bit_cast<std::int64_t>(
                        construction.bits()) != 0;
                break;

            case construction_kind::unsigned_integer:
                value =
                    construction.bits() != 0;
                break;

            case construction_kind::real:
                value =
                    std::bit_cast<double>(
                        construction.bits()) != 0.0;
                break;

            case construction_kind::member_binding:
            case construction_kind::object_binding:
            case construction_kind::unsupported:
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            store_native(
                target,
                value);

            return fixed_direct_materialization_result::
                success;
        }

        case intrinsic_type::char_type:
            return write_integer<char>(
                construction,
                target);

        case intrinsic_type::signed_char:
            return write_integer<signed char>(
                construction,
                target);

        case intrinsic_type::unsigned_char:
            return write_integer<unsigned char>(
                construction,
                target);

        case intrinsic_type::wchar_type:
            return write_integer<wchar_t>(
                construction,
                target);

        case intrinsic_type::char8_type:
            return write_integer<char8_t>(
                construction,
                target);

        case intrinsic_type::char16_type:
            return write_integer<char16_t>(
                construction,
                target);

        case intrinsic_type::char32_type:
            return write_integer<char32_t>(
                construction,
                target);

        case intrinsic_type::signed_short:
            return write_integer<short>(
                construction,
                target);

        case intrinsic_type::unsigned_short:
            return write_integer<unsigned short>(
                construction,
                target);

        case intrinsic_type::signed_int:
            return write_integer<int>(
                construction,
                target);

        case intrinsic_type::unsigned_int:
            return write_integer<unsigned int>(
                construction,
                target);

        case intrinsic_type::signed_long:
            return write_integer<long>(
                construction,
                target);

        case intrinsic_type::unsigned_long:
            return write_integer<unsigned long>(
                construction,
                target);

        case intrinsic_type::signed_long_long:
            return write_integer<long long>(
                construction,
                target);

        case intrinsic_type::unsigned_long_long:
            return write_integer<unsigned long long>(
                construction,
                target);

        case intrinsic_type::float_type:
            return write_real<float>(
                construction,
                target);

        case intrinsic_type::double_type:
            return write_real<double>(
                construction,
                target);

        case intrinsic_type::long_double_type:
            return write_real<long double>(
                construction,
                target);

        case intrinsic_type::nullptr_type: {
            if (!zero_pointer_construction(
                    construction)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            store_target_word(
                target,
                0);

            return fixed_direct_materialization_result::
                success;
        }

        case intrinsic_type::void_type:
            return fixed_direct_materialization_result::
                unsupported_type;

        case intrinsic_type::none:
            break;
        }

        return fixed_direct_materialization_result::
            invalid_input;
    }

    enum class materialization_action : std::uint8_t {
        materialize = 0,
        none,
        reference,
    };

    struct planned_member final {
        std::size_t offset = 0;
        construction_value construction{};
        type_ref type{};
        materialization_action action =
            materialization_action::materialize;
    };

    static_assert(sizeof(planned_member) == 32);

    [[nodiscard]] materialization_action
    classify_materialization(
        type_ref type,
        construction_value construction,
        type_ref& plan_type) const noexcept {

        type_ref referent;

        if (reference_referent(
                type,
                referent)) {

            plan_type =
                referent;

            return materialization_action::
                reference;
        }

        plan_type =
            type;

        for (;;) {
            if (type.kind() ==
                type_ref_kind::intrinsic) {

                const auto intrinsic =
                    static_cast<intrinsic_type>(
                        type.payload());

                if (intrinsic >
                        intrinsic_type::void_type &&
                    intrinsic <
                        intrinsic_type::nullptr_type &&
                    construction.kind ==
                        construction_kind::zero) {

                    return materialization_action::
                        none;
                }

                if (intrinsic ==
                        intrinsic_type::nullptr_type &&
                    zero_pointer_construction(
                        construction)) {

                    return materialization_action::
                        none;
                }

                return materialization_action::
                    materialize;
            }

            if (type.kind() !=
                type_ref_kind::derived) {

                return materialization_action::
                    materialize;
            }

            derived_type_record derived;

            if (!project.derived(
                    type,
                    derived)) {

                return materialization_action::
                    materialize;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                type =
                    derived.child;
                continue;

            case derived_type_kind::pointer:
                return zero_pointer_construction(
                           construction)
                    ? materialization_action::none
                    : materialization_action::materialize;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference:
            case derived_type_kind::bounded_array:
            case derived_type_kind::unbounded_array:
                return materialization_action::
                    materialize;
            }

            return materialization_action::
                materialize;
        }
    }

    enum class record_plan_state : std::uint32_t {
        empty = 0,
        seen,
        ready,
        ready_bases,
    };

    struct record_plan final {
        std::uint32_t begin = 0;
        std::uint32_t count = 0;
        record_plan_state state =
            record_plan_state::empty;
    };

    static_assert(sizeof(record_plan) == 12);

    [[nodiscard]] record_plan* plan(
        type_handle handle) noexcept {

        if (!handle ||
            handle.value() >
                record_plans.size()) {

            return nullptr;
        }

        return &record_plans[
            handle.value() - 1];
    }

    [[nodiscard]] const planned_member*
    planned_member_at(
        type_handle handle,
        std::uint32_t local) const noexcept {

        if (!handle ||
            handle.value() >
                record_plans.size()) {

            return nullptr;
        }

        const auto& record =
            record_plans[
                handle.value() - 1];

        const auto begin =
            static_cast<std::size_t>(
                record.begin);

        if ((record.state !=
                 record_plan_state::ready &&
             record.state !=
                 record_plan_state::ready_bases) ||
            local >=
                record.count ||
            begin >
                planned_members.size() ||
            local >
                planned_members.size() -
                    begin - 1) {

            return nullptr;
        }

        return &planned_members[
            begin +
            local];
    }

    [[nodiscard]] fixed_direct_materialization_result
    normal_bases(
        type_handle handle,
        std::byte* base,
        object_handle link_object) noexcept {

        type_entry type;

        if (!handle ||
            !project.type(
                handle,
                type) ||
            !type.defined() ||
            type.kind !=
                graph_type_kind::record) {

            return fixed_direct_materialization_result::
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

            base_record base_record_value;
            record_offset offset = 0;

            if (!project.base_at(
                    global,
                    base_record_value)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            if (base_record_value.virtual_base()) {
                return fixed_direct_materialization_result::
                    unsupported_type;
            }

            if (!layout.base_offset(
                    global,
                    offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto materialized =
                normal_record(
                    base_record_value.type,
                    base +
                        static_cast<std::size_t>(
                            offset),
                    link_object);

            if (materialized !=
                fixed_direct_materialization_result::
                    success) {

                return materialized;
            }
        }

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    prepare_record_plan(
        type_handle handle,
        record_plan& output) noexcept {

        type_entry type;

        if (!handle ||
            !project.type(
                handle,
                type) ||
            !type.defined() ||
            type.kind !=
                graph_type_kind::record) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (type.record_kind ==
            graph_record_kind::union_type) {

            return fixed_direct_materialization_result::
                unsupported_type;
        }

        if (type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        const auto old_count =
            planned_members.size();

        const auto maximum =
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)());

        if (old_count >
                maximum ||
            type.members.count >
                maximum - old_count) {

            return fixed_direct_materialization_result::
                overflow;
        }

        try {
            // Let push_back grow geometrically. Reserving the exact next
            // record size here repeatedly copies all earlier record plans
            // when a project contains many distinct record types.

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
                record_offset offset = 0;

                if (!project.member_at(
                        global,
                        member) ||
                    !project.construction(
                        handle,
                        local,
                        construction) ||
                    !layout.member_offset(
                        global,
                        offset)) {

                    planned_members.resize(
                        old_count);

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                type_ref plan_type;

                const auto action =
                    classify_materialization(
                        member.type,
                        construction,
                        plan_type);

                planned_members.push_back({
                    offset,
                    construction,
                    plan_type,
                    action,
                });
            }
        }
        catch (...) {
            planned_members.resize(
                old_count);

            return fixed_direct_materialization_result::
                failed;
        }

        output.begin =
            static_cast<std::uint32_t>(
                old_count);

        output.count =
            type.members.count;

        output.state =
            type.bases.count == 0
            ? record_plan_state::ready
            : record_plan_state::ready_bases;

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    observe_endpoint_plan(
        type_handle handle,
        std::uint32_t member_count) noexcept {

        auto* record =
            plan(
                handle);

        if (record == nullptr ||
            member_count == 0) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (record->state ==
                record_plan_state::ready ||
            record->state ==
                record_plan_state::ready_bases) {

            return fixed_direct_materialization_result::
                success;
        }

        // Before publication count is temporary endpoint-use pressure.
        // Build the full record plan only after point lookups have done at
        // least one record-width of repeated semantic decoding.
        if (record->count <
            member_count) {

            ++record->count;
        }

        if (record->count <
            member_count) {

            return fixed_direct_materialization_result::
                success;
        }

        return prepare_record_plan(
            handle,
            *record);
    }

    [[nodiscard]] fixed_direct_materialization_result
    normal_record_planned(
        type_handle handle,
        std::byte* base,
        object_handle link_object,
        const record_plan& record) noexcept {

        for (std::uint32_t local = 0;
             local <
                 record.count;
             ++local) {

            const auto& member =
                planned_members[
                    static_cast<std::size_t>(
                        record.begin) +
                    local];

            if (member.action ==
                materialization_action::none) {

                continue;
            }

            auto* target =
                base +
                static_cast<std::size_t>(
                    member.offset);

            if (member.action ==
                materialization_action::reference) {

                std::uint64_t stored = 0;

                if (!read_reference_slot(
                        target,
                        stored)) {

                    return fixed_direct_materialization_result::
                        incompatible_abi;
                }

                if (runtime_address(
                        stored) ||
                    is_pending_link(
                        stored)) {

                    continue;
                }

                std::uint64_t value_target = 0;

                const auto resolved =
                    resolve_reference_member(
                        handle,
                        base,
                        link_object,
                        local,
                        target,
                        value_target);

                if (resolved !=
                    fixed_direct_materialization_result::
                        success) {

                    return resolved;
                }

                const auto written =
                    write_address(
                        target,
                        value_target);

                if (written !=
                    fixed_direct_materialization_result::
                        success) {

                    return written;
                }

                continue;
            }

            const auto materialized =
                normal_value(
                    member.type,
                    member.construction,
                    target,
                    {});

            if (materialized !=
                fixed_direct_materialization_result::
                    success) {

                return materialized;
            }
        }

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    normal_record_unplanned(
        type_handle handle,
        std::byte* base,
        object_handle link_object) noexcept {

        type_entry type;

        if (!handle ||
            !project.type(
                handle,
                type) ||
            !type.defined() ||
            type.kind !=
                graph_type_kind::record) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (type.record_kind ==
            graph_record_kind::union_type) {

            return fixed_direct_materialization_result::
                unsupported_type;
        }

        if (type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (type.bases.count != 0) {
            const auto bases_materialized =
                normal_bases(
                    handle,
                    base,
                    link_object);

            if (bases_materialized !=
                fixed_direct_materialization_result::
                    success) {

                return bases_materialized;
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
            record_offset offset = 0;

            if (!project.member_at(
                    global,
                    member) ||
                !project.construction(
                    handle,
                    local,
                    construction) ||
                !layout.member_offset(
                    global,
                    offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* target =
                base +
                static_cast<std::size_t>(
                    offset);

            type_ref referent;

            if (reference_referent(
                    member.type,
                    referent)) {

                std::uint64_t stored = 0;

                if (!read_reference_slot(
                        target,
                        stored)) {

                    return fixed_direct_materialization_result::
                        incompatible_abi;
                }

                if (runtime_address(
                        stored) ||
                    is_pending_link(
                        stored)) {

                    continue;
                }

                std::uint64_t value_target = 0;

                const auto resolved =
                    resolve_reference_member(
                        handle,
                        base,
                        link_object,
                        local,
                        target,
                        value_target);

                if (resolved !=
                    fixed_direct_materialization_result::
                        success) {

                    return resolved;
                }

                const auto written =
                    write_address(
                        target,
                        value_target);

                if (written !=
                    fixed_direct_materialization_result::
                        success) {

                    return written;
                }

                continue;
            }

            const auto materialized =
                normal_value(
                    member.type,
                    construction,
                    target,
                    {});

            if (materialized !=
                fixed_direct_materialization_result::
                    success) {

                return materialized;
            }
        }

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    normal_record(
        type_handle handle,
        std::byte* base,
        object_handle link_object) noexcept {

        auto* record =
            plan(
                handle);

        if (record == nullptr) {
            return fixed_direct_materialization_result::
                invalid_input;
        }

        switch (record->state) {
        case record_plan_state::ready:
            return normal_record_planned(
                handle,
                base,
                link_object,
                *record);

        case record_plan_state::ready_bases: {
            const auto bases_materialized =
                normal_bases(
                    handle,
                    base,
                    link_object);

            if (bases_materialized !=
                fixed_direct_materialization_result::
                    success) {

                return bases_materialized;
            }

            return normal_record_planned(
                handle,
                base,
                link_object,
                *record);
        }

        case record_plan_state::seen: {
            const auto prepared =
                prepare_record_plan(
                    handle,
                    *record);

            if (prepared !=
                fixed_direct_materialization_result::
                    success) {

                return prepared;
            }

            if (record->state ==
                record_plan_state::ready_bases) {

                const auto bases_materialized =
                    normal_bases(
                        handle,
                        base,
                        link_object);

                if (bases_materialized !=
                    fixed_direct_materialization_result::
                        success) {

                    return bases_materialized;
                }
            }

            return normal_record_planned(
                handle,
                base,
                link_object,
                *record);
        }

        case record_plan_state::empty:
            record->state =
                record_plan_state::seen;

            return normal_record_unplanned(
                handle,
                base,
                link_object);
        }

        return fixed_direct_materialization_result::
            invalid_input;
    }

    struct reference_state final {
        type_handle record_type{};
        std::byte* record_base = nullptr;
        object_handle link_object{};
        std::uint32_t local = 0;
        std::byte* slot = nullptr;
    };

    // Construction-only sidecar for consecutive unplanned reference hops.
    // A ready value contains only metadata already validated for current.slot.
    struct unplanned_reference_metadata final {
        std::size_t global = 0;
        std::uint32_t member_count = 0;
        type_ref referent{};
        bool ready = false;
    };

    // During construction a target-native reference slot is also its state:
    // 0 = unresolved, ~link_handle = pending static link,
    // target-width max = cycle detection, target address = resolved.
    [[nodiscard]] std::uint64_t
    reference_visiting_marker() const noexcept {

        return target_word_mask();
    }

    [[nodiscard]] std::uint64_t encode_pending_link(
        std::uint32_t link) const noexcept {

        return
            (~static_cast<std::uint64_t>(
                link)) &
            target_word_mask();
    }

    [[nodiscard]] bool decode_pending_link(
        std::uint64_t value,
        std::uint32_t& output) const noexcept {

        output = 0;

        const auto mask =
            target_word_mask();

        if (value > mask ||
            value ==
                reference_visiting_marker()) {

            return false;
        }

        const auto decoded =
            (~value) & mask;

        if (decoded == 0 ||
            decoded >
                link_handle::maximum_slot) {

            return false;
        }

        output =
            static_cast<std::uint32_t>(
                decoded);

        return true;
    }

    [[nodiscard]] bool is_pending_link(
        std::uint64_t value) const noexcept {

        std::uint32_t ignored = 0;

        return decode_pending_link(
            value,
            ignored);
    }

    [[nodiscard]] bool runtime_address(
        std::uint64_t value) const noexcept {

        return
            value >= target_base_address &&
            value - target_base_address <
                layout.size();
    }

    [[nodiscard]] bool read_reference_slot(
        const std::byte* slot,
        std::uint64_t& value) const noexcept {

        value = 0;

        if (slot == nullptr) {
            return false;
        }

        if (properties.reference_size == 4) {
            std::uint32_t narrowed = 0;

            std::memcpy(
                &narrowed,
                slot,
                sizeof(narrowed));

            value = narrowed;
            return true;
        }

        if (properties.reference_size != 8) {
            return false;
        }

        std::memcpy(
            &value,
            slot,
            sizeof(value));

        return true;
    }

    void mark_reference_visiting(
        std::byte* slot) noexcept {

        store_target_word(
            slot,
            reference_visiting_marker());
    }


    [[nodiscard]] fixed_direct_materialization_result
    endpoint_path_reference_or_value(
        object_endpoint endpoint,
        reference_state& next,
        bool& has_next,
        std::uint64_t& output) noexcept {

        next = {};
        has_next = false;
        output = 0;

        if (!endpoint.object ||
            !endpoint.member.is_path()) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        object_entry object;
        std::uint64_t object_offset = 0;

        endpoint_path_record path;

        if (!project.object(
                endpoint.object,
                object) ||
            !layout.object_offset(
                endpoint.object,
                object_offset) ||
            !project.endpoint_path(
                endpoint.member.path(),
                path) ||
            path.root_type !=
                object.type) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        auto* current_address =
            address(
                object_offset);

        if (current_address == nullptr) {
            return fixed_direct_materialization_result::
                invalid_input;
        }

        auto current_type =
            object.type;

        type_handle final_record{};
        std::byte* final_record_base = nullptr;
        std::uint32_t final_local = 0;

        for (std::uint32_t local_step = 0;
             local_step <
                path.steps.count;
             ++local_step) {

            endpoint_path_step step;

            if (!project.endpoint_path_step_at(
                    static_cast<std::size_t>(
                        path.steps.begin) +
                        local_step,
                    step)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            derived_type_record qualified;

            while (project.derived(
                       current_type,
                       qualified) &&
                   (qualified.kind ==
                        derived_type_kind::const_qualified ||
                    qualified.kind ==
                        derived_type_kind::volatile_qualified)) {

                current_type =
                    qualified.child;
            }

            if (step.kind ==
                endpoint_path_step_kind::array_index) {

                derived_type_record array;

                if (!project.derived(
                        current_type,
                        array) ||
                    array.kind !=
                        derived_type_kind::bounded_array ||
                    step.value >=
                        array.payload) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                runtime_value_layout child;

                if (!layout.value(
                        array.child,
                        child) ||
                    (child.size != 0 &&
                     step.value >
                        (std::numeric_limits<std::uint64_t>::max)() /
                            child.size)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto delta =
                    step.value *
                    child.size;

                const auto current_offset =
                    static_cast<std::uint64_t>(
                        current_address -
                        runtime.data());

                if (!value_fits(
                        current_offset,
                        delta) ||
                    delta >
                        (std::numeric_limits<std::size_t>::max)()) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                current_address +=
                    static_cast<std::size_t>(
                        delta);

                current_type =
                    array.child;

                final_record = {};
                final_record_base = nullptr;
                final_local = 0;

                continue;
            }

            if (step.kind !=
                    endpoint_path_step_kind::member ||
                step.value >
                    (std::numeric_limits<std::uint32_t>::max)()) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            if (current_type.kind() !=
                type_ref_kind::named) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            type_handle record;
            type_entry record_entry;

            if (!project.named(
                    current_type,
                    record) ||
                !project.type(
                    record,
                    record_entry) ||
                step.value >=
                    record_entry.members.count) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto local =
                static_cast<std::uint32_t>(
                    step.value);

            const auto global =
                static_cast<std::size_t>(
                    record_entry.members.begin) +
                local;

            member_record member;
            record_offset offset = 0;

            if (!project.member_at(
                    global,
                    member) ||
                !layout.member_offset(
                    global,
                    offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            final_record =
                record;

            final_record_base =
                current_address;

            final_local =
                local;

            current_address +=
                static_cast<std::size_t>(
                    offset);

            current_type =
                member.type;
        }

        if (current_type !=
            path.value_type) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        type_ref referent;

        if (reference_referent(
                current_type,
                referent)) {

            if (!final_record ||
                final_record_base == nullptr) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            next = {
                final_record,
                final_record_base,
                endpoint.object,
                final_local,
                current_address,
            };

            has_next = true;

            return fixed_direct_materialization_result::
                success;
        }

        runtime_value_layout value;

        const auto offset =
            static_cast<std::uint64_t>(
                current_address -
                runtime.data());

        if (!layout.value(
                current_type,
                value) ||
            !value_fits(
                offset,
                value.size)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        output =
            target_address(current_address);

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    direct_endpoint_reference_or_value(
        object_endpoint endpoint,
        reference_state& next,
        bool& has_next,
        std::uint64_t& output) noexcept {

        next = {};
        has_next = false;
        output = 0;

        object_entry object;
        std::uint64_t object_offset = 0;

        if (!endpoint.object ||
            !project.object(
                endpoint.object,
                object) ||
            !layout.object_offset(
                endpoint.object,
                object_offset)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        type_handle record_type_value;

        if (!record_type(
                object.type,
                record_type_value)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        // Caller has already classified this endpoint as a direct member.
        // Use the raw zero-based local index so the common link path matches
        // the pre-SUBOBJECT resolver without another tag dispatch.
        const auto local =
            endpoint.member.value();

        auto* base =
            address(
                object_offset);

        if (base == nullptr) {
            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (const auto* member =
                planned_member_at(
                    record_type_value,
                    local);
            member != nullptr) {

            auto* member_address =
                base +
                static_cast<std::size_t>(
                    member->offset);

            if (member->action ==
                materialization_action::reference) {
                next = {
                    record_type_value,
                    base,
                    endpoint.object,
                    local,
                    member_address,
                };

                has_next = true;

                return fixed_direct_materialization_result::
                    success;
            }

            output =
                target_address(member_address);

            return fixed_direct_materialization_result::
                success;
        }

        type_entry record;

        if (!project.type(
                record_type_value,
                record) ||
            local >=
                record.members.count) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        member_record member;
        record_offset member_offset = 0;

        const auto global =
            static_cast<std::size_t>(
                record.members.begin) +
            local;

        if (!project.member_at(
                global,
                member) ||
            !layout.member_offset(
                global,
                member_offset)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        auto* member_address =
            base +
            static_cast<std::size_t>(
                member_offset);

        type_ref referent;

        const auto reference =
            reference_referent(
                member.type,
                referent);

        const auto observed =
            observe_endpoint_plan(
                record_type_value,
                record.members.count);

        if (observed !=
            fixed_direct_materialization_result::
                success) {

            return observed;
        }

        if (reference) {
            next = {
                record_type_value,
                base,
                endpoint.object,
                local,
                member_address,
            };

            has_next = true;

            return fixed_direct_materialization_result::
                success;
        }

        output =
            target_address(member_address);

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    endpoint_reference_or_value(
        object_endpoint endpoint,
        reference_state& next,
        bool& has_next,
        std::uint64_t& output) noexcept {

        if (endpoint.member.is_path()) {
            return endpoint_path_reference_or_value(
                endpoint,
                next,
                has_next,
                output);
        }

        return direct_endpoint_reference_or_value(
            endpoint,
            next,
            has_next,
            output);
    }

    [[nodiscard]] fixed_direct_materialization_result
    advance_reference_planned(
        const reference_state& current,
        const planned_member& member,
        reference_state& next,
        bool& has_next,
        std::uint64_t& output) noexcept {

        next = {};
        has_next = false;
        output = 0;

        if (current.record_base == nullptr ||
            current.slot == nullptr ||
            member.action !=
                materialization_action::reference ||
            current.record_base +
                static_cast<std::size_t>(
                    member.offset) !=
                current.slot) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        const auto& construction =
            member.construction;

        const auto referent =
            member.type;

        switch (construction.kind) {
        case construction_kind::zero: {
            std::uint64_t sentinel = 0;

            if (!layout.unconnected_offset(
                    referent,
                    sentinel)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto* target =
                address(
                    sentinel);

            if (target == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            output =
                target_address(target);

            return fixed_direct_materialization_result::
                success;
        }

        case construction_kind::member_binding: {
            if (construction.operand == 0) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto source_local =
                construction.operand - 1;

            const auto* source =
                planned_member_at(
                    current.record_type,
                    source_local);

            if (source == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* source_address =
                current.record_base +
                static_cast<std::size_t>(
                    source->offset);

            if (source->action ==
                materialization_action::reference) {
                next = {
                    current.record_type,
                    current.record_base,
                    current.link_object,
                    source_local,
                    source_address,
                };

                has_next = true;

                return fixed_direct_materialization_result::
                    success;
            }

            output =
                target_address(source_address);

            return fixed_direct_materialization_result::
                success;
        }

        case construction_kind::object_binding: {
            if (construction.operand == 0) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto object =
                project.object_at(
                    construction.operand - 1);

            object_entry source;
            std::uint64_t source_offset = 0;

            if (!object ||
                object.value() !=
                    construction.operand ||
                !project.object(
                    object,
                    source) ||
                !layout.object_offset(
                    object,
                    source_offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* source_address =
                address(
                    source_offset);

            if (source_address == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            type_ref source_referent;

            if (reference_referent(
                    source.type,
                    source_referent)) {

                construction_value source_construction;

                if (!project.construction(
                        object,
                        source_construction) ||
                    source_construction.kind !=
                        construction_kind::zero) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                std::uint64_t sentinel = 0;

                if (!layout.unconnected_offset(
                        source_referent,
                        sentinel)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto* target =
                    address(
                        sentinel);

                if (target == nullptr) {
                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                output =
                    target_address(target);

                return fixed_direct_materialization_result::
                    success;
            }

            output =
                target_address(source_address);

            return fixed_direct_materialization_result::
                success;
        }

        case construction_kind::signed_integer:
        case construction_kind::unsigned_integer:
        case construction_kind::real:
        case construction_kind::unsupported:
            return fixed_direct_materialization_result::
                invalid_input;
        }

        return fixed_direct_materialization_result::
            invalid_input;
    }

    [[nodiscard]] fixed_direct_materialization_result
    advance_reference_unplanned(
        const reference_state& current,
        const unplanned_reference_metadata& metadata,
        reference_state& next,
        unplanned_reference_metadata& next_metadata,
        bool& has_next,
        std::uint64_t& output) noexcept {

        next = {};
        next_metadata = {};
        has_next = false;
        output = 0;

        if (!current.record_type ||
            current.record_base == nullptr ||
            current.slot == nullptr) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        std::size_t global = 0;
        std::size_t member_begin = 0;
        std::uint32_t member_count = 0;
        type_ref referent;

        if (metadata.ready) {
            if (current.local >=
                    metadata.member_count ||
                metadata.global <
                    current.local ||
                !metadata.referent) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            global =
                metadata.global;

            member_begin =
                metadata.global -
                current.local;

            member_count =
                metadata.member_count;

            referent =
                metadata.referent;
        }
        else {
            type_entry record;

            if (!project.type(
                    current.record_type,
                    record) ||
                current.local >=
                    record.members.count) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            member_begin =
                static_cast<std::size_t>(
                    record.members.begin);

            member_count =
                record.members.count;

            global =
                member_begin +
                current.local;

            member_record member;
            record_offset member_offset = 0;

            if (!project.member_at(
                    global,
                    member) ||
                !layout.member_offset(
                    global,
                    member_offset) ||
                current.record_base +
                    static_cast<std::size_t>(
                        member_offset) !=
                    current.slot ||
                !reference_referent(
                    member.type,
                    referent)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }
        }

        construction_value construction;

        if (!project.construction_at(
                global,
                construction)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        // Dense link prebinding owns Graph-link semantics. A zero slot that
        // reaches this path is therefore governed only by type construction.
        switch (construction.kind) {
        case construction_kind::zero: {
            std::uint64_t sentinel = 0;

            if (!layout.unconnected_offset(
                    referent,
                    sentinel)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto* target =
                address(
                    sentinel);

            if (target == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            output =
                target_address(target);

            return fixed_direct_materialization_result::
                success;
        }

        case construction_kind::member_binding: {
            if (construction.operand == 0) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto source_local =
                construction.operand - 1;

            if (source_local >=
                member_count) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto source_global =
                member_begin +
                source_local;

            member_record source;
            record_offset source_offset = 0;

            if (!project.member_at(
                    source_global,
                    source) ||
                !layout.member_offset(
                    source_global,
                    source_offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* source_address =
                current.record_base +
                static_cast<std::size_t>(
                    source_offset);

            type_ref source_referent;

            if (reference_referent(
                    source.type,
                    source_referent)) {

                next = {
                    current.record_type,
                    current.record_base,
                    current.link_object,
                    source_local,
                    source_address,
                };

                next_metadata = {
                    source_global,
                    member_count,
                    source_referent,
                    true,
                };

                has_next = true;

                return fixed_direct_materialization_result::
                    success;
            }

            output =
                target_address(source_address);

            return fixed_direct_materialization_result::
                success;
        }

        case construction_kind::object_binding: {
            if (construction.operand == 0) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto object =
                project.object_at(
                    construction.operand - 1);

            object_entry source;
            std::uint64_t source_offset = 0;

            if (!object ||
                object.value() !=
                    construction.operand ||
                !project.object(
                    object,
                    source) ||
                !layout.object_offset(
                    object,
                    source_offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* source_address =
                address(
                    source_offset);

            if (source_address == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            type_ref source_referent;

            if (reference_referent(
                    source.type,
                    source_referent)) {

                construction_value source_construction;

                if (!project.construction(
                        object,
                        source_construction) ||
                    source_construction.kind !=
                        construction_kind::zero) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                std::uint64_t sentinel = 0;

                if (!layout.unconnected_offset(
                        source_referent,
                        sentinel)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto* target =
                    address(
                        sentinel);

                if (target == nullptr) {
                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                output =
                    target_address(target);

                return fixed_direct_materialization_result::
                    success;
            }

            output =
                target_address(source_address);

            return fixed_direct_materialization_result::
                success;
        }

        case construction_kind::signed_integer:
        case construction_kind::unsigned_integer:
        case construction_kind::real:
        case construction_kind::unsupported:
            return fixed_direct_materialization_result::
                invalid_input;
        }

        return fixed_direct_materialization_result::
            invalid_input;
    }


    [[nodiscard]] fixed_direct_materialization_result
    resolve_unplanned_member_binding_chain(
        reference_state& current,
        unplanned_reference_metadata& metadata,
        std::uint64_t maximum_steps,
        std::uint64_t& consumed,
        bool& completed,
        std::uint64_t& output) noexcept {

        consumed = 0;
        completed = false;
        output = 0;

        if (!metadata.ready ||
            !current.record_type ||
            current.record_base == nullptr ||
            current.slot == nullptr ||
            current.local >=
                metadata.member_count ||
            metadata.global <
                current.local ||
            !metadata.referent ||
            maximum_steps == 0) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        const auto member_begin =
            metadata.global -
            current.local;

        const auto member_count =
            metadata.member_count;

        auto local =
            current.local;

        auto global =
            metadata.global;

        auto referent =
            metadata.referent;

        auto* slot =
            current.slot;

        for (;;) {
            if (consumed >=
                maximum_steps) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            construction_value construction;

            if (!project.construction_at(
                    global,
                    construction)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            if (construction.kind !=
                construction_kind::member_binding) {

                current.local = local;
                current.slot = slot;

                metadata = {
                    global,
                    member_count,
                    referent,
                    true,
                };

                return fixed_direct_materialization_result::
                    success;
            }

            if (construction.operand == 0) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto source_local =
                construction.operand - 1;

            if (source_local >=
                member_count) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto source_global =
                member_begin +
                source_local;

            member_record source;
            record_offset source_offset = 0;

            if (!project.member_at(
                    source_global,
                    source) ||
                !layout.member_offset(
                    source_global,
                    source_offset)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* source_address =
                current.record_base +
                static_cast<std::size_t>(
                    source_offset);

            type_ref source_referent;

            const auto source_is_reference =
                reference_referent(
                    source.type,
                    source_referent);

            mark_reference_visiting(
                slot);

            try {
                // The first generic hop already proved a second reference.
                // Reserve only when this tight walker proves one more
                // same-record reference hop.
                if (source_is_reference &&
                    resolution_path.size() == 1) {

                    const auto reserve_count =
                        static_cast<std::size_t>(
                            member_count);

                    const auto reserve_limit =
                        layout.size() /
                            properties.reference_size +
                        1;

                    if (reserve_count >
                            resolution_path.capacity() &&
                        reserve_count <=
                            project.member_count() &&
                        reserve_count <=
                            reserve_limit) {

                        resolution_path.reserve(
                            reserve_count);
                    }
                }

                resolution_path.push_back(
                    reinterpret_cast<std::uintptr_t>(
                        slot));
            }
            catch (...) {
                return fixed_direct_materialization_result::
                    failed;
            }

            ++consumed;

            if (!source_is_reference) {
                const auto target =
                    target_address(source_address);

                if (!runtime_address(
                        target)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                output = target;

                for (const auto pending :
                     resolution_path) {

                    auto* pending_slot =
                        reinterpret_cast<std::byte*>(
                            pending);

                    const auto written =
                        write_address(
                            pending_slot,
                            output);

                    if (written !=
                        fixed_direct_materialization_result::
                            success) {

                        return written;
                    }
                }

                completed = true;

                return fixed_direct_materialization_result::
                    success;
            }

            // A reference source itself consumes a resolver step when its slot
            // is inspected. Preserve the generic maximum-step bound.
            if (consumed >=
                maximum_steps) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            std::uint64_t stored = 0;

            if (!read_reference_slot(
                    source_address,
                    stored)) {

                return fixed_direct_materialization_result::
                    incompatible_abi;
            }

            if (stored ==
                reference_visiting_marker()) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            if (is_pending_link(
                    stored)) {

                current.local =
                    source_local;

                current.slot =
                    source_address;

                metadata = {
                    source_global,
                    member_count,
                    source_referent,
                    true,
                };

                return fixed_direct_materialization_result::
                    success;
            }

            if (stored != 0) {
                if (!runtime_address(
                        stored)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                output = stored;

                for (const auto pending :
                     resolution_path) {

                    auto* pending_slot =
                        reinterpret_cast<std::byte*>(
                            pending);

                    const auto written =
                        write_address(
                            pending_slot,
                            output);

                    if (written !=
                        fixed_direct_materialization_result::
                            success) {

                        return written;
                    }
                }

                completed = true;

                return fixed_direct_materialization_result::
                    success;
            }

            local =
                source_local;

            global =
                source_global;

            referent =
                source_referent;

            slot =
                source_address;
        }
    }

    [[nodiscard]] fixed_direct_materialization_result
    resolve_reference_member(
        type_handle record_type_value,
        std::byte* record_base,
        object_handle link_object,
        std::uint32_t local,
        std::byte* slot,
        std::uint64_t& output) noexcept {

        output = 0;
        resolution_path.clear();

        reference_state current{
            record_type_value,
            record_base,
            link_object,
            local,
            slot,
        };

        unplanned_reference_metadata
            current_metadata;

        const auto maximum_steps =
            layout.size() /
                properties.reference_size +
            1;

        for (std::uint64_t step = 0;
             step <
                 maximum_steps;
             ++step) {

            std::uint64_t stored = 0;

            if (!read_reference_slot(
                    current.slot,
                    stored)) {

                return fixed_direct_materialization_result::
                    incompatible_abi;
            }

            if (stored ==
                reference_visiting_marker()) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            std::uint32_t raw_link = 0;

            const auto link_pending =
                decode_pending_link(
                    stored,
                    raw_link);

            if (stored != 0 &&
                !link_pending) {

                if (!runtime_address(
                        stored)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                output = stored;

                for (const auto pending :
                     resolution_path) {

                    auto* pending_slot =
                        reinterpret_cast<std::byte*>(
                            pending);

                    const auto written =
                        write_address(
                            pending_slot,
                            output);

                    if (written !=
                        fixed_direct_materialization_result::
                            success) {

                        return written;
                    }
                }

                return fixed_direct_materialization_result::
                    success;
            }

            reference_state next;
            unplanned_reference_metadata
                next_metadata;
            bool has_next = false;
            std::uint64_t target = 0;

            fixed_direct_materialization_result advanced;

            if (link_pending) {
                if (raw_link == 0 ||
                    raw_link >
                        project.link_count()) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto link =
                    project.link_at(
                        raw_link - 1);

                link_record value;

                if (!link ||
                    link.value() !=
                        raw_link ||
                    !project.link(
                        link,
                        value) ||
                    raw_link >
                        link_target_slots.size() ||
                    link_target_slots[
                        raw_link - 1] !=
                        current.slot) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                advanced =
                    endpoint_reference_or_value(
                        value.source,
                        next,
                        has_next,
                        target);
            }
            else if (const auto* member =
                         planned_member_at(
                             current.record_type,
                             current.local);
                     member != nullptr) {

                advanced =
                    advance_reference_planned(
                        current,
                        *member,
                        next,
                        has_next,
                        target);
            }
            else {
                if (current_metadata.ready) {
                    std::uint64_t consumed = 0;
                    bool completed = false;

                    const auto chained =
                        resolve_unplanned_member_binding_chain(
                            current,
                            current_metadata,
                            maximum_steps - step,
                            consumed,
                            completed,
                            target);

                    if (chained !=
                        fixed_direct_materialization_result::
                            success) {

                        return chained;
                    }

                    if (completed) {
                        output = target;
                        return fixed_direct_materialization_result::
                            success;
                    }

                    if (consumed != 0) {
                        step +=
                            consumed - 1;

                        continue;
                    }
                }

                advanced =
                    advance_reference_unplanned(
                        current,
                        current_metadata,
                        next,
                        next_metadata,
                        has_next,
                        target);
            }

            if (advanced !=
                fixed_direct_materialization_result::
                    success) {

                return advanced;
            }

            // Direct T& -> T is the dominant case and needs no path storage.
            if (!has_next &&
                resolution_path.empty()) {

                if (!runtime_address(
                        target)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                output = target;
                return fixed_direct_materialization_result::
                    success;
            }

            mark_reference_visiting(
                current.slot);

            try {
                // Reserve only after two consecutive unplanned reference hops
                // prove this is a real chain. Persisted counts are never
                // trusted beyond already-bound Project/Runtime limits.
                if (current_metadata.ready &&
                    next_metadata.ready &&
                    resolution_path.size() == 1) {

                    const auto reserve_count =
                        static_cast<std::size_t>(
                            next_metadata.member_count);

                    if (reserve_count >
                            resolution_path.capacity() &&
                        reserve_count <=
                            project.member_count() &&
                        reserve_count <=
                            maximum_steps) {

                        resolution_path.reserve(
                            reserve_count);
                    }
                }

                resolution_path.push_back(
                    reinterpret_cast<std::uintptr_t>(
                        current.slot));
            }
            catch (...) {
                return fixed_direct_materialization_result::
                    failed;
            }

            if (!has_next) {
                if (!runtime_address(
                        target)) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                output = target;

                for (const auto pending :
                     resolution_path) {

                    auto* pending_slot =
                        reinterpret_cast<std::byte*>(
                            pending);

                    const auto written =
                        write_address(
                            pending_slot,
                            output);

                    if (written !=
                        fixed_direct_materialization_result::
                            success) {

                        return written;
                    }
                }

                return fixed_direct_materialization_result::
                    success;
            }

            current = next;
            current_metadata =
                next_metadata;
        }

        return fixed_direct_materialization_result::
            invalid_input;
    }

    [[nodiscard]] fixed_direct_materialization_result
    resolve_direct_endpoint(
        object_endpoint endpoint,
        std::uint64_t& output) noexcept {

        output = 0;

        reference_state next;
        bool has_next = false;

        const auto resolved =
            direct_endpoint_reference_or_value(
                endpoint,
                next,
                has_next,
                output);

        if (resolved !=
            fixed_direct_materialization_result::
                success ||
            !has_next) {

            return resolved;
        }

        return resolve_reference_member(
            next.record_type,
            next.record_base,
            next.link_object,
            next.local,
            next.slot,
            output);
    }

    [[nodiscard]] fixed_direct_materialization_result
    resolve_path_endpoint(
        object_endpoint endpoint,
        std::uint64_t& output) noexcept {

        output = 0;

        reference_state next;
        bool has_next = false;

        const auto resolved =
            endpoint_path_reference_or_value(
                endpoint,
                next,
                has_next,
                output);

        if (resolved !=
            fixed_direct_materialization_result::
                success ||
            !has_next) {

            return resolved;
        }

        return resolve_reference_member(
            next.record_type,
            next.record_base,
            next.link_object,
            next.local,
            next.slot,
            output);
    }

    [[nodiscard]] bool endpoint_value_type(
        object_endpoint endpoint,
        type_ref& output) const noexcept {

        output = {};

        object_entry object;

        if (!endpoint.object ||
            !endpoint.member ||
            !project.object(
                endpoint.object,
                object)) {

            return false;
        }

        if (endpoint.member.is_path()) {
            endpoint_path_record path;

            if (!project.endpoint_path(
                    endpoint.member.path(),
                    path) ||
                path.root_type !=
                    object.type) {

                return false;
            }

            output =
                path.value_type;

            return static_cast<bool>(
                output);
        }

        type_handle record;

        if (!record_type(
                object.type,
                record)) {

            return false;
        }

        member_record member;

        if (!project.member(
                record,
                endpoint.member.direct_member(),
                member)) {

            return false;
        }

        output =
            member.type;

        return static_cast<bool>(
            output);
    }

    [[nodiscard]] fixed_direct_materialization_result
    apply_object_initializations() noexcept {

        for (std::size_t index = 0;
             index <
                 project.initialization_count();
             ++index) {

            object_initialization_record initialization;

            if (!project.initialization_at(
                    index,
                    initialization)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            reference_state next;
            bool reference = false;
            std::uint64_t target_address_value = 0;

            const auto resolved =
                endpoint_reference_or_value(
                    initialization.target,
                    next,
                    reference,
                    target_address_value);

            if (resolved !=
                    fixed_direct_materialization_result::
                        success ||
                reference ||
                !runtime_address(
                    target_address_value)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            type_ref target_type;

            if (!endpoint_value_type(
                    initialization.target,
                    target_type)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto offset =
                target_address_value -
                target_base_address;

            auto* target =
                address(
                    offset);

            if (target == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto materialized =
                normal_value(
                    target_type,
                    initialization.value,
                    target,
                    {});

            if (materialized !=
                fixed_direct_materialization_result::
                    success) {

                return materialized;
            }
        }

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    mark_link_targets() noexcept {

        for (std::size_t index = 0;
             index <
                 project.link_count();
             ++index) {

            const auto handle =
                project.link_at(
                    index);

            link_record link;

            if (!handle ||
                !project.link(
                    handle,
                    link)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            reference_state target;
            bool target_is_reference = false;
            std::uint64_t target_value = 0;

            const auto target_resolved =
                link.target.member.is_path()
                ? endpoint_path_reference_or_value(
                    link.target,
                    target,
                    target_is_reference,
                    target_value)
                : direct_endpoint_reference_or_value(
                    link.target,
                    target,
                    target_is_reference,
                    target_value);

            if (target_resolved !=
                    fixed_direct_materialization_result::
                        success ||
                !target_is_reference ||
                target.slot == nullptr) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            std::uint64_t stored = 0;

            if (!read_reference_slot(
                    target.slot,
                    stored) ||
                stored != 0) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto raw_link =
                handle.value();

            if (raw_link == 0 ||
                raw_link >
                    link_handle::maximum_slot) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            link_target_slots[index] =
                target.slot;

            store_target_word(
                target.slot,
                encode_pending_link(
                    raw_link));
        }

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    materialize_links() noexcept {

        for (std::size_t index = 0;
             index <
                 project.link_count();
             ++index) {

            const auto handle =
                project.link_at(
                    index);

            link_record link;

            if (!handle ||
                !project.link(
                    handle,
                    link)) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            if (index >=
                link_target_slots.size()) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            auto* target_slot =
                link_target_slots[index];

            if (target_slot == nullptr) {
                return fixed_direct_materialization_result::
                    invalid_input;
            }

            std::uint64_t stored = 0;

            if (!read_reference_slot(
                    target_slot,
                    stored)) {

                return fixed_direct_materialization_result::
                    incompatible_abi;
            }

            if (runtime_address(
                    stored)) {

                continue;
            }

            std::uint32_t raw_pending = 0;

            if (!decode_pending_link(
                    stored,
                    raw_pending) ||
                raw_pending !=
                    handle.value()) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            std::uint64_t source = 0;

            const auto source_resolved =
                link.source.member.is_path()
                ? resolve_path_endpoint(
                    link.source,
                    source)
                : resolve_direct_endpoint(
                    link.source,
                    source);

            if (source_resolved !=
                fixed_direct_materialization_result::
                    success) {

                return source_resolved;
            }

            const auto written =
                write_address(
                    target_slot,
                    source);

            if (written !=
                fixed_direct_materialization_result::
                    success) {

                return written;
            }
        }

        return fixed_direct_materialization_result::
            success;
    }

    const compiled_project_view& project;
    const runtime_layout& layout;
    const server_abi_configuration& abi;
    std::uint64_t target_base_address = 0;
    abi_properties properties;
    std::span<std::byte> runtime;
    std::vector<std::uintptr_t> resolution_path;
    std::vector<record_plan> record_plans;
    std::vector<planned_member> planned_members;

    // PASS 1 owns target validation. PASS 2 reuses the exact validated SHM
    // slot without repeating endpoint resolution. Pending-link identity lives
    // temporarily in the native reference slot itself.
    std::vector<std::byte*> link_target_slots;
};

}

bool fixed_direct_host_compatible(
    const server_abi_configuration& abi) noexcept {

    if (std::endian::native !=
            std::endian::little ||
        sizeof(bool) != 1 ||
        sizeof(char8_t) != 1 ||
        sizeof(char16_t) != 2 ||
        sizeof(char32_t) != 4 ||
        sizeof(short) != 2 ||
        sizeof(int) != 4 ||
        sizeof(long long) != 8 ||
        sizeof(float) != 4 ||
        sizeof(double) != 8) {

        return false;
    }

#if defined(_WIN32)
    return
        (abi.target ==
             abi_target::windows_x86 ||
         abi.target ==
             abi_target::windows_x64) &&
        sizeof(wchar_t) == 2 &&
        sizeof(long) == 4 &&
        sizeof(long double) == 8;
#else
    return
        abi.target ==
            abi_target::posix_x64 &&
        sizeof(void*) == 8 &&
        sizeof(wchar_t) == 4 &&
        sizeof(long) == 8 &&
        sizeof(long double) == 16;
#endif
}

bool fixed_direct_target_range_compatible(
    const server_abi_configuration& abi,
    std::uint64_t target_base_address,
    std::uint64_t runtime_size) noexcept {

    abi_properties properties;

    if (!abi_layout_properties(
            abi.target,
            properties) ||
        target_base_address == 0) {

        return false;
    }

    const auto mask =
        properties.reference_size == 4
        ? static_cast<std::uint64_t>(
            (std::numeric_limits<std::uint32_t>::max)())
        : (std::numeric_limits<std::uint64_t>::max)();

    if (target_base_address > mask) {
        return false;
    }

    std::uint64_t last_address =
        target_base_address;

    if (runtime_size != 0) {
        const auto tail =
            runtime_size - 1;

        if (tail >
            mask - target_base_address) {

            return false;
        }

        last_address += tail;
    }

    const auto reserved_begin =
        mask -
        static_cast<std::uint64_t>(
            link_handle::maximum_slot);

    return last_address <
        reserved_begin;
}

fixed_direct_materialization_result
materialize_fixed_direct(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::uint64_t target_base_address,
    std::span<std::byte> runtime) noexcept {

    fixed_direct_materializer materializer{
        project,
        layout,
        abi,
        target_base_address,
        runtime,
    };

    return materializer.run();
}

fixed_direct_materialization_result
materialize_fixed_direct(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> runtime) noexcept {

    return materialize_fixed_direct(
        project,
        layout,
        abi,
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(
                runtime.data())),
        runtime);
}

}
