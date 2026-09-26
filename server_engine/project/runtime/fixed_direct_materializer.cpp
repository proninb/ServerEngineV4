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

struct reference_representation_probe final {
    int& value;
};

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
        std::span<std::byte> runtime) noexcept
        : project(project),
          layout(layout),
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

        if (layout.size() >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {

            return fixed_direct_materialization_result::
                overflow;
        }

        const auto logical_size =
            static_cast<std::size_t>(
                layout.size());

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

        const auto links_materialized =
            materialize_links();

        if (links_materialized !=
            fixed_direct_materialization_result::
                success) {

            return links_materialized;
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

        return fixed_direct_materialization_result::
            success;
    }

private:
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

                const auto handle =
                    project.type_at(
                        type.payload() - 1);

                if (!handle ||
                    handle.value() !=
                        type.payload()) {

                    return false;
                }

                output = handle;
                return true;
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
        std::uintptr_t target) noexcept {

        if (slot == nullptr ||
            sizeof(std::uintptr_t) != 8) {

            return fixed_direct_materialization_result::
                incompatible_abi;
        }

        if (!runtime_address(
                target)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        store_native(
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
            const auto handle =
                project.type_at(
                    type.payload() - 1);

            return canonical_record(
                handle,
                target);
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
                    reinterpret_cast<std::uintptr_t>(
                        value_target));
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

        for (std::uint32_t local = 0;
             local <
                 type.members.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.members.begin) +
                local;

            member_record member;
            std::uint64_t offset = 0;

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
                        reinterpret_cast<std::uintptr_t>(
                            value_target));

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

            const auto handle =
                project.type_at(
                    type.payload() - 1);

            return normal_record(
                handle,
                target,
                top_object);
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

                const std::uintptr_t value = 0;

                store_native(
                    target,
                    value);

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
                    reinterpret_cast<std::uintptr_t>(
                        value_target));
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

            const std::uintptr_t value = 0;

            store_native(
                target,
                value);

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

    [[nodiscard]] fixed_direct_materialization_result
    normal_record(
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
            std::uint64_t offset = 0;

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

                std::uintptr_t value_target = 0;

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

        return fixed_direct_materialization_result::success;
    }

    struct reference_state final {
        type_handle record_type{};
        std::byte* record_base = nullptr;
        object_handle link_object{};
        std::uint32_t local = 0;
        std::byte* slot = nullptr;
    };

    // During construction a native reference slot is also its state:
    // 0 = unresolved, pending = Graph link target, visiting = cycle detection,
    // Runtime address = resolved.
    static constexpr std::uintptr_t
        reference_visiting_marker =
            (std::numeric_limits<std::uintptr_t>::max)();

    static constexpr std::uintptr_t
        reference_link_pending_marker =
            reference_visiting_marker - 1;

    [[nodiscard]] bool runtime_address(
        std::uintptr_t value) const noexcept {

        if (value == 0 ||
            runtime.data() == nullptr) {

            return false;
        }

        const auto base =
            reinterpret_cast<std::uintptr_t>(
                runtime.data());

        return value >= base &&
            value - base <
                layout.size();
    }

    [[nodiscard]] bool read_reference_slot(
        const std::byte* slot,
        std::uintptr_t& value) const noexcept {

        value = 0;

        if (slot == nullptr ||
            sizeof(std::uintptr_t) != 8) {

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

        store_native(
            slot,
            reference_visiting_marker);
    }

    [[nodiscard]] fixed_direct_materialization_result
    endpoint_reference_or_value(
        object_endpoint endpoint,
        reference_state& next,
        bool& has_next,
        std::uintptr_t& output) noexcept {

        next = {};
        has_next = false;
        output = 0;

        object_entry object;
        std::uint64_t object_offset = 0;

        if (!endpoint.object ||
            !endpoint.member ||
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

        type_entry record;

        const auto local =
            endpoint.member.value();

        if (!project.type(
                record_type_value,
                record) ||
            local >=
                record.members.count) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        member_record member;
        std::uint64_t member_offset = 0;

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

        auto* base =
            address(
                object_offset);

        if (base == nullptr) {
            return fixed_direct_materialization_result::
                invalid_input;
        }

        auto* member_address =
            base +
            static_cast<std::size_t>(
                member_offset);

        type_ref referent;

        if (reference_referent(
                member.type,
                referent)) {

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
            reinterpret_cast<std::uintptr_t>(
                member_address);

        return fixed_direct_materialization_result::
            success;
    }

    [[nodiscard]] fixed_direct_materialization_result
    advance_reference(
        const reference_state& current,
        reference_state& next,
        bool& has_next,
        std::uintptr_t& output) noexcept {

        next = {};
        has_next = false;
        output = 0;

        member_record member;
        construction_value construction;
        type_entry record;

        if (!current.record_type ||
            current.record_base == nullptr ||
            current.slot == nullptr ||
            !project.type(
                current.record_type,
                record) ||
            current.local >=
                record.members.count ||
            !project.member(
                current.record_type,
                current.local,
                member) ||
            !project.construction(
                current.record_type,
                current.local,
                construction)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        const auto global =
            static_cast<std::size_t>(
                record.members.begin) +
            current.local;

        std::uint64_t member_offset = 0;

        if (!layout.member_offset(
                global,
                member_offset) ||
            current.record_base +
                static_cast<std::size_t>(
                    member_offset) !=
                current.slot) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        type_ref referent;

        if (!reference_referent(
                member.type,
                referent)) {

            return fixed_direct_materialization_result::
                invalid_input;
        }

        if (current.link_object) {
            const auto link =
                project.find_link_target(
                    current.link_object,
                    current.local);

            if (link) {
                link_record value;

                if (!project.link(
                        link,
                        value) ||
                    value.target.object !=
                        current.link_object ||
                    value.target.member.value() !=
                        current.local) {

                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                return endpoint_reference_or_value(
                    value.source,
                    next,
                    has_next,
                    output);
            }
        }

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
                reinterpret_cast<std::uintptr_t>(
                    target);

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
                record.members.count) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            member_record source;
            std::uint64_t source_offset = 0;

            const auto source_global =
                static_cast<std::size_t>(
                    record.members.begin) +
                source_local;

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

                has_next = true;

                return fixed_direct_materialization_result::
                    success;
            }

            output =
                reinterpret_cast<std::uintptr_t>(
                    source_address);

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
                    reinterpret_cast<std::uintptr_t>(
                        target);

                return fixed_direct_materialization_result::
                    success;
            }

            output =
                reinterpret_cast<std::uintptr_t>(
                    source_address);

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
    resolve_reference_member(
        type_handle record_type_value,
        std::byte* record_base,
        object_handle link_object,
        std::uint32_t local,
        std::byte* slot,
        std::uintptr_t& output) noexcept {

        output = 0;
        resolution_path.clear();

        reference_state current{
            record_type_value,
            record_base,
            link_object,
            local,
            slot,
        };

        const auto maximum_steps =
            layout.size() /
                sizeof(std::uintptr_t) +
            1;

        for (std::uint64_t step = 0;
             step <
                 maximum_steps;
             ++step) {

            std::uintptr_t stored = 0;

            if (!read_reference_slot(
                    current.slot,
                    stored)) {

                return fixed_direct_materialization_result::
                    incompatible_abi;
            }

            if (stored ==
                reference_visiting_marker) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            const auto link_pending =
                stored ==
                    reference_link_pending_marker;

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
            bool has_next = false;
            std::uintptr_t target = 0;

            fixed_direct_materialization_result advanced;

            if (link_pending) {
                if (!current.link_object) {
                    return fixed_direct_materialization_result::
                        invalid_input;
                }

                const auto link =
                    project.find_link_target(
                        current.link_object,
                        current.local);

                link_record value;

                if (!link ||
                    !project.link(
                        link,
                        value) ||
                    value.target.object !=
                        current.link_object ||
                    value.target.member.value() !=
                        current.local) {

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
            else {
                advanced =
                    advance_reference(
                        current,
                        next,
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
        }

        return fixed_direct_materialization_result::
            invalid_input;
    }

    [[nodiscard]] fixed_direct_materialization_result
    resolve_endpoint(
        object_endpoint endpoint,
        std::uintptr_t& output) noexcept {

        output = 0;

        reference_state next;
        bool has_next = false;

        const auto resolved =
            endpoint_reference_or_value(
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
            std::uintptr_t target_value = 0;

            const auto target_resolved =
                endpoint_reference_or_value(
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

            std::uintptr_t stored = 0;

            if (!read_reference_slot(
                    target.slot,
                    stored) ||
                stored != 0) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            store_native(
                target.slot,
                reference_link_pending_marker);
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

            reference_state target;
            bool target_is_reference = false;
            std::uintptr_t target_value = 0;

            const auto target_resolved =
                endpoint_reference_or_value(
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

            std::uintptr_t stored = 0;

            if (!read_reference_slot(
                    target.slot,
                    stored)) {

                return fixed_direct_materialization_result::
                    incompatible_abi;
            }

            if (runtime_address(
                    stored)) {

                continue;
            }

            if (stored !=
                reference_link_pending_marker) {

                return fixed_direct_materialization_result::
                    invalid_input;
            }

            std::uintptr_t source = 0;

            const auto source_resolved =
                resolve_endpoint(
                    link.source,
                    source);

            if (source_resolved !=
                fixed_direct_materialization_result::
                    success) {

                return source_resolved;
            }

            const auto written =
                write_address(
                    target.slot,
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
    std::span<std::byte> runtime;
    std::vector<std::uintptr_t> resolution_path;
};

}

bool fixed_direct_host_compatible(
    const server_abi_configuration& abi) noexcept {

    if (sizeof(void*) != 8 ||
        sizeof(std::uintptr_t) != 8 ||
        std::endian::native !=
            std::endian::little ||
        sizeof(bool) != 1 ||
        sizeof(short) != 2 ||
        sizeof(int) != 4 ||
        sizeof(long long) != 8 ||
        sizeof(float) != 4 ||
        sizeof(double) != 8 ||
        sizeof(std::nullptr_t) != 8 ||
        sizeof(reference_representation_probe) != 8 ||
        alignof(reference_representation_probe) != 8) {

        return false;
    }

#if defined(_WIN32)
    if (abi.target !=
            abi_target::windows_x64 ||
        sizeof(wchar_t) != 2 ||
        sizeof(long) != 4 ||
        sizeof(long double) != 8 ||
        alignof(long double) != 8) {

        return false;
    }
#else
    if (abi.target !=
            abi_target::posix_x64 ||
        sizeof(wchar_t) != 4 ||
        sizeof(long) != 8 ||
        sizeof(long double) != 16 ||
        alignof(long double) != 16) {

        return false;
    }
#endif

    void* null_pointer = nullptr;
    std::uintptr_t null_bits =
        (std::numeric_limits<std::uintptr_t>::max)();

    std::memcpy(
        &null_bits,
        &null_pointer,
        sizeof(null_bits));

    if (null_bits != 0) {
        return false;
    }

    int value = 0;

    reference_representation_probe probe{
        value};

    std::uintptr_t reference_bits = 0;

    std::memcpy(
        &reference_bits,
        &probe,
        sizeof(reference_bits));

    return reference_bits ==
        reinterpret_cast<std::uintptr_t>(
            &value);
}

fixed_direct_materialization_result
materialize_fixed_direct(
    const compiled_project_view& project,
    const runtime_layout& layout,
    const server_abi_configuration& abi,
    std::span<std::byte> runtime) noexcept {

    if (!fixed_direct_host_compatible(
            abi)) {

        return fixed_direct_materialization_result::
            incompatible_abi;
    }

    fixed_direct_materializer materializer{
        project,
        layout,
        runtime,
    };

    return materializer.run();
}

}
