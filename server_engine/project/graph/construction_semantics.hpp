/*
 * ABI-independent construction semantics shared by Parser/Graph/persistence.
 *
 * This layer decides whether a normalized construction_value is meaningful for
 * a target type. ABI-dependent range/representation checks remain at Runtime
 * materialization because compiled.bin itself is ABI-independent.
 */
#pragma once

#include "construction_value.hpp"
#include "type_ref.hpp"

namespace cw::server {

namespace construction_semantics_detail {

[[nodiscard]] constexpr bool integral_intrinsic(
    intrinsic_type type) noexcept {

    switch (type) {
    case intrinsic_type::bool_type:
    case intrinsic_type::char_type:
    case intrinsic_type::signed_char:
    case intrinsic_type::unsigned_char:
    case intrinsic_type::wchar_type:
    case intrinsic_type::char8_type:
    case intrinsic_type::char16_type:
    case intrinsic_type::char32_type:
    case intrinsic_type::signed_short:
    case intrinsic_type::unsigned_short:
    case intrinsic_type::signed_int:
    case intrinsic_type::unsigned_int:
    case intrinsic_type::signed_long:
    case intrinsic_type::unsigned_long:
    case intrinsic_type::signed_long_long:
    case intrinsic_type::unsigned_long_long:
        return true;

    case intrinsic_type::none:
    case intrinsic_type::void_type:
    case intrinsic_type::float_type:
    case intrinsic_type::double_type:
    case intrinsic_type::long_double_type:
    case intrinsic_type::nullptr_type:
        return false;
    }

    return false;
}

[[nodiscard]] constexpr bool floating_intrinsic(
    intrinsic_type type) noexcept {

    return type == intrinsic_type::float_type ||
        type == intrinsic_type::double_type ||
        type == intrinsic_type::long_double_type;
}

[[nodiscard]] constexpr bool integer_constant(
    construction_value value) noexcept {

    return value.kind ==
            construction_kind::signed_integer ||
        value.kind ==
            construction_kind::unsigned_integer;
}

}

// TypeView must provide:
//   bool derived(type_ref, derived_type_record&) const noexcept;
template <typename TypeView>
[[nodiscard]] bool construction_compatible(
    const TypeView& types,
    type_ref target,
    construction_value value) noexcept {

    if (!target ||
        !valid_construction(value) ||
        value.kind ==
            construction_kind::unsupported) {

        return false;
    }

    for (;;) {
        if (target.kind() !=
            type_ref_kind::derived) {

            break;
        }

        derived_type_record derived;

        if (!types.derived(
                target,
                derived)) {

            return false;
        }

        if (derived.kind ==
                derived_type_kind::const_qualified ||
            derived.kind ==
                derived_type_kind::volatile_qualified) {

            target = derived.child;
            continue;
        }

        switch (derived.kind) {
        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference:
            return value.kind ==
                    construction_kind::zero ||
                value.kind ==
                    construction_kind::member_binding ||
                value.kind ==
                    construction_kind::object_binding;

        case derived_type_kind::pointer:
            return value.kind ==
                    construction_kind::zero ||
                (construction_semantics_detail::
                     integer_constant(value) &&
                 value.bits() == 0);

        case derived_type_kind::bounded_array:
            return derived.payload != 0 &&
                value.kind ==
                    construction_kind::zero;

        case derived_type_kind::unbounded_array:
            return false;

        case derived_type_kind::const_qualified:
        case derived_type_kind::volatile_qualified:
            break;
        }

        return false;
    }

    if (target.kind() ==
        type_ref_kind::named) {

        return value.kind ==
            construction_kind::zero;
    }

    if (target.kind() !=
        type_ref_kind::intrinsic) {

        return false;
    }

    const auto intrinsic =
        static_cast<intrinsic_type>(
            target.payload());

    if (intrinsic <=
            intrinsic_type::none ||
        intrinsic >
            intrinsic_type::nullptr_type ||
        intrinsic ==
            intrinsic_type::void_type) {

        return false;
    }

    if (intrinsic ==
        intrinsic_type::nullptr_type) {

        return value.kind ==
                construction_kind::zero ||
            (construction_semantics_detail::
                 integer_constant(value) &&
             value.bits() == 0);
    }

    if (construction_semantics_detail::
        floating_intrinsic(intrinsic)) {

        return value.kind ==
                construction_kind::zero ||
            value.kind ==
                construction_kind::signed_integer ||
            value.kind ==
                construction_kind::unsigned_integer ||
            value.kind ==
                construction_kind::real;
    }

    if (construction_semantics_detail::
        integral_intrinsic(intrinsic)) {

        return value.kind ==
                construction_kind::zero ||
            value.kind ==
                construction_kind::signed_integer ||
            value.kind ==
                construction_kind::unsigned_integer;
    }

    return false;
}

}
