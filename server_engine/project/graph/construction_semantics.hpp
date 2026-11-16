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
#include "graph.hpp"

namespace cw::server {

// Address links use the existing relocatable endpoint/link representation.
// Derived-to-base conversion is made explicit in the source endpoint path.
template<class Types>
bool qualified_object_compatible(const Types& types, type_ref pointee, type_ref source, bool allow_void = false) noexcept {
    derived_type_record d;
    if (types.derived(source, d) && (d.kind == derived_type_kind::lvalue_reference || d.kind == derived_type_kind::rvalue_reference)) source = d.child;
    bool tc = false, tv = false, sc = false, sv = false;
    while (types.derived(pointee, d) && (d.kind == derived_type_kind::const_qualified || d.kind == derived_type_kind::volatile_qualified)) {
        tc |= d.kind == derived_type_kind::const_qualified; tv |= d.kind == derived_type_kind::volatile_qualified; pointee = d.child;
    }
    while (types.derived(source, d) && (d.kind == derived_type_kind::const_qualified || d.kind == derived_type_kind::volatile_qualified)) {
        sc |= d.kind == derived_type_kind::const_qualified; sv |= d.kind == derived_type_kind::volatile_qualified; source = d.child;
    }
    return (!sc || tc) && (!sv || tv) && (pointee == source ||
        (allow_void && pointee.kind() == type_ref_kind::intrinsic && pointee.payload() == static_cast<std::uint32_t>(intrinsic_type::void_type)));
}

template<class Types>
bool pointer_address_compatible(const Types& types, type_ref target, type_ref source) noexcept {
    derived_type_record d;
    while (types.derived(target, d) && (d.kind == derived_type_kind::const_qualified || d.kind == derived_type_kind::volatile_qualified)) target = d.child;
    return types.derived(target, d) && d.kind == derived_type_kind::pointer &&
        qualified_object_compatible(types, d.child, source, true);
}

// String literal backing is an internal narrow-char array, never a persisted
// process address. Only a pointer to const char may bind to its first byte.
template<class Types>
bool const_char_pointer(const Types& types, type_ref type) noexcept {
    derived_type_record d;
    while (types.derived(type, d) && (d.kind == derived_type_kind::const_qualified ||
            d.kind == derived_type_kind::volatile_qualified)) type = d.child;
    if (!types.derived(type, d) || d.kind != derived_type_kind::pointer) return false;
    type = d.child;
    bool is_const = false;
    while (types.derived(type, d) && (d.kind == derived_type_kind::const_qualified ||
            d.kind == derived_type_kind::volatile_qualified)) {
        is_const |= d.kind == derived_type_kind::const_qualified; type = d.child;
    }
    return is_const && type.kind()==type_ref_kind::intrinsic && type.payload()==static_cast<std::uint32_t>(intrinsic_type::char_type);
}

template<class Types>
bool literal_pointer_binding_compatible(const Types& types, type_ref target, type_ref source) noexcept {
    if (!const_char_pointer(types, target)) return false;
    derived_type_record d;
    if (!types.derived(source, d) || d.kind != derived_type_kind::bounded_array || !d.payload) return false;
    return d.child.kind()==type_ref_kind::intrinsic && d.child.payload()==static_cast<std::uint32_t>(intrinsic_type::char_type);
}

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

    if (value.kind == construction_kind::byte_patch)
        return target.kind() == type_ref_kind::intrinsic &&
            target.payload() == static_cast<std::uint32_t>(intrinsic_type::char_type);

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
                (value.kind == construction_kind::object_binding && const_char_pointer(types, target)) ||
                (construction_semantics_detail::
                     integer_constant(value) &&
                 value.bits() == 0);

        case derived_type_kind::bounded_array:
            return derived.payload != 0 &&
                (value.kind == construction_kind::zero ||
                 (value.kind == construction_kind::byte_array && derived.payload == value.operand &&
                  derived.child.kind() == type_ref_kind::intrinsic &&
                  derived.child.payload() == static_cast<std::uint32_t>(intrinsic_type::char_type)));

        case derived_type_kind::unbounded_array:
            return false;

        case derived_type_kind::const_qualified:
        case derived_type_kind::volatile_qualified:
            break;
        }

        return false;
    }

    intrinsic_type nominal_storage = intrinsic_type::none;
    if (target.kind() ==
        type_ref_kind::named) {

        type_entry entry;
        const auto identity = identity_ref::from_raw((static_cast<std::uint32_t>(identity_kind::type) << identity_ref::kind_shift) | target.payload());
        if (types.type(types.find_type(identity), entry) &&
            entry.kind == graph_type_kind::enumeration) {
            return value.kind == construction_kind::zero ||
                construction_semantics_detail::integer_constant(value);
        }
        if (types.type(types.find_type(identity), entry) && entry.kind == graph_type_kind::nominal_scalar)
            nominal_storage = entry.storage_intrinsic();
        else return value.kind == construction_kind::zero;
    }

    if (target.kind() !=
        type_ref_kind::intrinsic && nominal_storage == intrinsic_type::none) {

        return false;
    }

    const auto intrinsic = nominal_storage != intrinsic_type::none ? nominal_storage :
        static_cast<intrinsic_type>(target.payload());

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
