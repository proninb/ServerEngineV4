#include "shm_type_batch.hpp"

#include "../abi/abi_layout.hpp"
#include "../persistence/compiled_project.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cw::server {
namespace {

[[nodiscard]] bool host_compatible(
    abi_target target) noexcept {

#if defined(_WIN32)
    if constexpr (sizeof(void*) == 8) {
        return target == abi_target::windows_x64;
    }
    else {
        return target == abi_target::windows_x86;
    }
#else
    return sizeof(void*) == 8 &&
        target == abi_target::posix_x64;
#endif
}

template <typename T>
[[nodiscard]] bool integer_value(
    construction_value construction,
    T& output) noexcept {

    static_assert(std::is_integral_v<T>);
    output = {};

    if (construction.kind == construction_kind::zero) {
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
                static_cast<std::uint64_t>(value) >
                    static_cast<std::uint64_t>(
                        (std::numeric_limits<T>::max)())) {
                return false;
            }
        }

        output = static_cast<T>(value);
        return true;
    }

    if (construction.kind ==
        construction_kind::unsigned_integer) {

        const auto value = construction.bits();

        if (value >
            static_cast<std::uint64_t>(
                (std::numeric_limits<T>::max)())) {
            return false;
        }

        output = static_cast<T>(value);
        return true;
    }

    return false;
}

[[nodiscard]] bool zero_pointer_construction(
    construction_value construction) noexcept {

    if (construction.kind == construction_kind::zero) {
        return true;
    }

    return
        (construction.kind ==
             construction_kind::signed_integer ||
         construction.kind ==
             construction_kind::unsigned_integer) &&
        construction.bits() == 0;
}

enum class type_batch_mode : std::uint8_t {
    canonical = 0,
    defaults,
};

enum class type_batch_build_policy : std::uint8_t {
    local = 0,
    inline_leaf_08,
    inline_subtree_08,
    inline_leaf_16,
    inline_leaf_32,
    inline_leaf_64,
};

inline constexpr std::uint32_t type_batch_inline_leaf_limit = 8;
inline constexpr std::uint32_t type_batch_inline_subtree_limit = 8;

[[nodiscard]] constexpr std::uint32_t
type_batch_inline_leaf_limit_for_policy(
    type_batch_build_policy policy) noexcept {

    switch (policy) {
    case type_batch_build_policy::inline_leaf_08:
        return 8;
    case type_batch_build_policy::inline_leaf_16:
        return 16;
    case type_batch_build_policy::inline_leaf_32:
        return 32;
    case type_batch_build_policy::inline_leaf_64:
        return 64;
    case type_batch_build_policy::local:
    case type_batch_build_policy::inline_subtree_08:
        return 0;
    }

    return 0;
}

enum class cache_state : std::uint8_t {
    empty = 0,
    preparing,
    ready,
};

[[nodiscard]] constexpr shm_offset invalid_where() noexcept {
    return (std::numeric_limits<shm_offset>::max)();
}

}

class shm_type_batch_builder final {
public:
    shm_type_batch_builder(
        const compiled_project_view& project,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        shm_type_batch& output,
        shm_type_batch_prepare_telemetry* telemetry,
        type_batch_build_policy policy =
            type_batch_build_policy::local)
        : project(project),
          abi(abi),
          layout(layout),
          output(output),
          telemetry(telemetry),
          policy(policy) {
    }

    [[nodiscard]] shm_type_batch_result build() {

        output.reset();

        if (!project.valid() ||
            layout.target() != abi.target ||
            !host_compatible(abi.target)) {
            return shm_type_batch_result::incompatible_abi;
        }

        abi_properties properties;

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {
            return shm_type_batch_result::incompatible_abi;
        }

        named_where.resize(
            project.identity_count(),
            0);

        object_identity_to_slot.resize(
            project.identity_count(),
            0);

        canonical_named.resize(
            project.type_slot_count());
        default_named.resize(
            project.type_slot_count());
        canonical_derived.resize(
            project.derived_type_count());
        default_derived.resize(
            project.derived_type_count());

        output.object_where.assign(
            project.object_slot_count(),
            invalid_where());

        // Objects drive default Type API discovery and publish all physical
        // Object WHERE values. Semantic identities are resolved only here.
        auto result = prepare_objects();
        if (result != shm_type_batch_result::success) {
            output.reset();
            return result;
        }

        // Canonical unconnected<T> may require additional Runtime-only APIs.
        result = prepare_canonical_roots();
        if (result != shm_type_batch_result::success) {
            output.reset();
            return result;
        }

        // The persisted production program is final at this point. Share
        // exact relative-reference programs across Type APIs without changing
        // their record representation or Runtime execution path.
        if (policy ==
                type_batch_build_policy::
                    inline_leaf_64) {

            result =
                canonicalize_relative_reference_programs();

            if (result !=
                shm_type_batch_result::success) {

                output.reset();
                return result;
            }
        }

        result = validate_object_references();
        if (result != shm_type_batch_result::success) {
            output.reset();
            return result;
        }

        result = build_root_groups();
        if (result != shm_type_batch_result::success) {
            output.reset();
            return result;
        }

        output.target_value = abi.target;
        output.layout_size = layout.size();
        output.prepared_value = true;

        fill_telemetry();
        return shm_type_batch_result::success;
    }

private:
    struct cache_slot final {
        std::uint32_t type_api = 0;
        cache_state state = cache_state::empty;
    };

    struct area_begin final {
        std::size_t relative_references = 0;
        std::size_t absolute_references = 0;
        std::size_t object_references = 0;
        std::size_t stores = 0;
        std::size_t children = 0;
        std::size_t repeats = 0;
        std::size_t post_stores = 0;
    };

    [[nodiscard]] std::vector<cache_slot>&
    named_cache(type_batch_mode mode) noexcept {
        return mode == type_batch_mode::canonical
            ? canonical_named
            : default_named;
    }

    [[nodiscard]] std::vector<cache_slot>&
    derived_cache(type_batch_mode mode) noexcept {
        return mode == type_batch_mode::canonical
            ? canonical_derived
            : default_derived;
    }

    [[nodiscard]] area_begin begin_area() const noexcept {
        return {
            output.relative_references.size(),
            output.absolute_references.size(),
            output.object_references.size(),
            output.stores.size(),
            output.children.size(),
            output.repeats.size(),
            output.post_stores.size(),
        };
    }

    [[nodiscard]] bool make_range(
        std::size_t begin,
        std::size_t end,
        shm_type_batch::range& output_range) const noexcept {

        if (end < begin ||
            begin >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) ||
            end - begin >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)())) {
            return false;
        }

        output_range.begin =
            static_cast<std::uint32_t>(begin);
        output_range.count =
            static_cast<std::uint32_t>(end - begin);
        return true;
    }

    [[nodiscard]] shm_type_batch_result finish_api(
        area_begin begin,
        std::uint32_t& output_api) {

        output_api = 0;

        shm_type_batch::type_api api;

        if (!make_range(
                begin.relative_references,
                output.relative_references.size(),
                api.relative_references) ||
            !make_range(
                begin.absolute_references,
                output.absolute_references.size(),
                api.absolute_references) ||
            !make_range(
                begin.object_references,
                output.object_references.size(),
                api.object_references) ||
            !make_range(
                begin.stores,
                output.stores.size(),
                api.stores) ||
            !make_range(
                begin.children,
                output.children.size(),
                api.children) ||
            !make_range(
                begin.repeats,
                output.repeats.size(),
                api.repeats) ||
            !make_range(
                begin.post_stores,
                output.post_stores.size(),
                api.post_stores)) {
            return shm_type_batch_result::overflow;
        }

        if (api.relative_references.count == 0 &&
            api.absolute_references.count == 0 &&
            api.object_references.count == 0 &&
            api.stores.count == 0 &&
            api.children.count == 0 &&
            api.repeats.count == 0 &&
            api.post_stores.count == 0) {
            return shm_type_batch_result::success;
        }

        if (output.type_apis.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_type_batch_result::overflow;
        }

        output.type_apis.push_back(api);
        output_api =
            static_cast<std::uint32_t>(
                output.type_apis.size());

        return shm_type_batch_result::success;
    }

    [[nodiscard]] bool add_record_offset(
        shm_record_offset left,
        shm_record_offset right,
        shm_record_offset& output_value) const noexcept {

        if (right >
            (std::numeric_limits<
                shm_record_offset>::max)() -
                left) {
            output_value = 0;
            return false;
        }

        output_value = left + right;
        return true;
    }

    [[nodiscard]] shm_type_batch_result resolve_named(
        type_ref type,
        type_handle& output_handle) {

        output_handle = {};

        if (type.kind() != type_ref_kind::named ||
            type.payload() == 0 ||
            type.payload() > named_where.size()) {
            return shm_type_batch_result::invalid_input;
        }

        auto& where =
            named_where[type.payload() - 1];

        if (where == 0) {
            const auto handle =
                project.type_location(type);

            if (!handle) {
                return shm_type_batch_result::invalid_input;
            }

            where = handle.value();

            if (telemetry != nullptr) {
                ++telemetry->semantic_identity_resolutions;
            }
        }

        output_handle =
            project.type_at(where - 1);

        return output_handle
            ? shm_type_batch_result::success
            : shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] shm_type_batch_result resolve_named(
        identity_ref identity,
        type_handle& output_handle) {

        output_handle = {};

        if (!identity ||
            identity.slot() == 0 ||
            identity.slot() > named_where.size()) {
            return shm_type_batch_result::invalid_input;
        }

        auto& where =
            named_where[identity.slot() - 1];

        if (where == 0) {
            const auto handle =
                project.type_location(identity);

            if (!handle) {
                return shm_type_batch_result::invalid_input;
            }

            where = handle.value();

            if (telemetry != nullptr) {
                ++telemetry->semantic_identity_resolutions;
            }
        }

        output_handle =
            project.type_at(where - 1);

        return output_handle
            ? shm_type_batch_result::success
            : shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] shm_type_batch_result resolve_object(
        identity_ref identity,
        object_handle& output_object) {

        output_object = {};

        if (!identity ||
            identity.slot() == 0 ||
            identity.slot() >
                object_identity_to_slot.size()) {
            return shm_type_batch_result::invalid_input;
        }

        auto& slot =
            object_identity_to_slot[
                identity.slot() - 1];

        if (slot == 0) {
            const auto object =
                project.object_location(identity);

            if (!object) {
                return shm_type_batch_result::invalid_input;
            }

            slot = object.value();

            if (telemetry != nullptr) {
                ++telemetry->semantic_identity_resolutions;
                ++telemetry->object_binding_resolutions;
            }
        }

        output_object =
            project.object_at(slot - 1);

        return output_object
            ? shm_type_batch_result::success
            : shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] shm_type_batch_result append_relative_reference(
        shm_record_offset target,
        shm_record_offset source) {

        output.relative_references.push_back({
            target,
            source,
        });

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result append_absolute_reference(
        shm_record_offset target,
        shm_offset source) {

        if (source >= layout.size()) {
            return shm_type_batch_result::invalid_input;
        }

        output.absolute_references.push_back({
            target,
            0,
            source,
        });

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result append_object_reference(
        shm_record_offset target,
        std::uint32_t object_slot) {

        if (object_slot == 0 ||
            object_slot > project.object_slot_count()) {
            return shm_type_batch_result::invalid_input;
        }

        output.object_references.push_back({
            target,
            object_slot,
        });

        return shm_type_batch_result::success;
    }

    template <typename T>
    [[nodiscard]] shm_type_batch_result append_store_to(
        std::vector<shm_type_batch::store_operation>& operations,
        shm_record_offset target,
        T value) {

        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 16);

        if (output.constants.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_type_batch_result::overflow;
        }

        std::array<std::byte, 16> constant{};
        std::memcpy(
            constant.data(),
            &value,
            sizeof(T));

        output.constants.push_back(constant);

        operations.push_back({
            target,
            static_cast<std::uint32_t>(
                output.constants.size() - 1),
            static_cast<std::uint8_t>(sizeof(T)),
            {},
        });

        return shm_type_batch_result::success;
    }

    template <typename T>
    [[nodiscard]] shm_type_batch_result append_store(
        shm_record_offset target,
        T value) {

        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 16);

        // RUNTIME-SHM-ZERO-STORE-ELIDE-09D-08B:
        // Ordinary scalar construction writes into fresh-zero FIXED_DIRECT
        // object storage. Type Batch rejects unions, and link markers occupy
        // reference slots rather than scalar-store slots. If the complete
        // target-ABI object representation is already all zero, persisting a
        // regular store would only replay bytes that are guaranteed to be
        // zero. Elide it here, before the constant/store columns are built.
        //
        // Constructor post_stores intentionally do NOT use this rule: they
        // execute after child/repeat construction and may be required to
        // overwrite an earlier non-zero value with zero.
        std::array<std::byte, 16> bytes{};

        std::memcpy(
            bytes.data(),
            &value,
            sizeof(T));

        bool zero = true;

        for (std::size_t index = 0;
             index < sizeof(T);
             ++index) {

            if (bytes[index] !=
                std::byte{0}) {

                zero = false;
                break;
            }
        }

        if (zero) {
            if (telemetry != nullptr) {
                ++telemetry->
                    zero_operations_elided;
            }

            return shm_type_batch_result::
                success;
        }

        return append_store_to(
            output.stores,
            target,
            value);
    }

    template <typename T>
    [[nodiscard]] shm_type_batch_result append_post_store(
        shm_record_offset target,
        T value) {

        return append_store_to(
            output.post_stores,
            target,
            value);
    }

    [[nodiscard]] shm_type_batch_result append_repeat(
        shm_record_offset target,
        std::uint32_t type_api,
        shm_offset stride,
        std::uint64_t count) {

        if (type_api == 0) {
            if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }
            return shm_type_batch_result::success;
        }

        if (type_api > output.type_apis.size() ||
            stride == 0 ||
            count == 0) {
            return shm_type_batch_result::invalid_input;
        }

        output.repeats.push_back({
            target,
            type_api,
            stride,
            count,
        });

        return shm_type_batch_result::success;
    }

    template <typename T>
    [[nodiscard]] shm_type_batch_result append_integer(
        shm_record_offset target,
        construction_value construction) {

        T value{};

        if (!integer_value(construction, value)) {
            return shm_type_batch_result::invalid_input;
        }

        return append_store(target, value);
    }

    template <typename T>
    [[nodiscard]] shm_type_batch_result append_real(
        shm_record_offset target,
        construction_value construction) {

        T value{};

        switch (construction.kind) {
        case construction_kind::signed_integer:
            value =
                static_cast<T>(
                    std::bit_cast<std::int64_t>(
                        construction.bits()));
            break;
        case construction_kind::unsigned_integer:
            value = static_cast<T>(construction.bits());
            break;
        case construction_kind::real:
            value =
                static_cast<T>(
                    std::bit_cast<double>(
                        construction.bits()));
            break;
        default:
            return shm_type_batch_result::invalid_input;
        }

        return append_store(target, value);
    }

    [[nodiscard]] shm_type_batch_result append_intrinsic(
        shm_record_offset target,
        intrinsic_type type,
        construction_value construction) {

        if (construction.kind == construction_kind::zero) {
            if (type == intrinsic_type::void_type) {
                return shm_type_batch_result::unsupported_type;
            }

            if (type == intrinsic_type::none) {
                return shm_type_batch_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }

            return shm_type_batch_result::success;
        }

        switch (type) {
        case intrinsic_type::bool_type: {
            bool value = false;

            switch (construction.kind) {
            case construction_kind::signed_integer:
                value =
                    std::bit_cast<std::int64_t>(
                        construction.bits()) != 0;
                break;
            case construction_kind::unsigned_integer:
                value = construction.bits() != 0;
                break;
            case construction_kind::real:
                value =
                    std::bit_cast<double>(
                        construction.bits()) != 0.0;
                break;
            default:
                return shm_type_batch_result::invalid_input;
            }

            return append_store(target, value);
        }

        case intrinsic_type::char_type:
            return append_integer<char>(target, construction);
        case intrinsic_type::signed_char:
            return append_integer<signed char>(target, construction);
        case intrinsic_type::unsigned_char:
            return append_integer<unsigned char>(target, construction);
        case intrinsic_type::wchar_type:
            return append_integer<wchar_t>(target, construction);
        case intrinsic_type::char8_type:
            return append_integer<char8_t>(target, construction);
        case intrinsic_type::char16_type:
            return append_integer<char16_t>(target, construction);
        case intrinsic_type::char32_type:
            return append_integer<char32_t>(target, construction);
        case intrinsic_type::signed_short:
            return append_integer<short>(target, construction);
        case intrinsic_type::unsigned_short:
            return append_integer<unsigned short>(target, construction);
        case intrinsic_type::signed_int:
            return append_integer<int>(target, construction);
        case intrinsic_type::unsigned_int:
            return append_integer<unsigned int>(target, construction);
        case intrinsic_type::signed_long:
            return append_integer<long>(target, construction);
        case intrinsic_type::unsigned_long:
            return append_integer<unsigned long>(target, construction);
        case intrinsic_type::signed_long_long:
            return append_integer<long long>(target, construction);
        case intrinsic_type::unsigned_long_long:
            return append_integer<unsigned long long>(
                target,
                construction);
        case intrinsic_type::float_type:
            return append_real<float>(target, construction);
        case intrinsic_type::double_type:
            return append_real<double>(target, construction);
        case intrinsic_type::long_double_type:
            return append_real<long double>(target, construction);

        case intrinsic_type::nullptr_type:
            if (!zero_pointer_construction(construction)) {
                return shm_type_batch_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }

            return shm_type_batch_result::success;

        case intrinsic_type::void_type:
            return shm_type_batch_result::unsupported_type;
        case intrinsic_type::none:
            return shm_type_batch_result::invalid_input;
        }

        return shm_type_batch_result::invalid_input;
    }

    template <typename T>
    [[nodiscard]] shm_type_batch_result
    append_constructor_integer(
        shm_record_offset target,
        construction_value construction) {

        T value{};

        if (!integer_value(
                construction,
                value)) {

            return shm_type_batch_result::invalid_input;
        }

        return append_post_store(
            target,
            value);
    }

    template <typename T>
    [[nodiscard]] shm_type_batch_result
    append_constructor_real(
        shm_record_offset target,
        construction_value construction) {

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
            return shm_type_batch_result::invalid_input;
        }

        return append_post_store(
            target,
            value);
    }

    [[nodiscard]] shm_type_batch_result
    append_constructor_intrinsic(
        shm_record_offset target,
        intrinsic_type type,
        construction_value construction) {

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
                return shm_type_batch_result::invalid_input;
            }

            return append_post_store(
                target,
                value);
        }

        case intrinsic_type::char_type:
            return append_constructor_integer<char>(
                target, construction);
        case intrinsic_type::signed_char:
            return append_constructor_integer<signed char>(
                target, construction);
        case intrinsic_type::unsigned_char:
            return append_constructor_integer<unsigned char>(
                target, construction);
        case intrinsic_type::wchar_type:
            return append_constructor_integer<wchar_t>(
                target, construction);
        case intrinsic_type::char8_type:
            return append_constructor_integer<char8_t>(
                target, construction);
        case intrinsic_type::char16_type:
            return append_constructor_integer<char16_t>(
                target, construction);
        case intrinsic_type::char32_type:
            return append_constructor_integer<char32_t>(
                target, construction);
        case intrinsic_type::signed_short:
            return append_constructor_integer<short>(
                target, construction);
        case intrinsic_type::unsigned_short:
            return append_constructor_integer<unsigned short>(
                target, construction);
        case intrinsic_type::signed_int:
            return append_constructor_integer<int>(
                target, construction);
        case intrinsic_type::unsigned_int:
            return append_constructor_integer<unsigned int>(
                target, construction);
        case intrinsic_type::signed_long:
            return append_constructor_integer<long>(
                target, construction);
        case intrinsic_type::unsigned_long:
            return append_constructor_integer<unsigned long>(
                target, construction);
        case intrinsic_type::signed_long_long:
            return append_constructor_integer<long long>(
                target, construction);
        case intrinsic_type::unsigned_long_long:
            return append_constructor_integer<unsigned long long>(
                target, construction);

        case intrinsic_type::float_type:
            return append_constructor_real<float>(
                target, construction);
        case intrinsic_type::double_type:
            return append_constructor_real<double>(
                target, construction);
        case intrinsic_type::long_double_type:
            return append_constructor_real<long double>(
                target, construction);

        case intrinsic_type::nullptr_type: {
            if (!zero_pointer_construction(
                    construction)) {

                return shm_type_batch_result::invalid_input;
            }

            abi_properties properties;

            if (!abi_layout_properties(
                    abi.target,
                    properties)) {

                return shm_type_batch_result::invalid_input;
            }

            if (properties.pointer_size == 4) {
                return append_post_store(
                    target,
                    std::uint32_t{0});
            }

            if (properties.pointer_size == 8) {
                return append_post_store(
                    target,
                    std::uint64_t{0});
            }

            return shm_type_batch_result::incompatible_abi;
        }

        case intrinsic_type::void_type:
            return shm_type_batch_result::unsupported_type;

        case intrinsic_type::none:
            return shm_type_batch_result::invalid_input;
        }

        return shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] shm_type_batch_result
    append_constructor_defaults(
        type_handle handle) {

        const auto owner =
            project.identity(
                handle);

        shm_value_layout root_layout;

        if (!owner ||
            !layout.type(
                handle,
                root_layout)) {

            return shm_type_batch_result::invalid_input;
        }

        std::size_t first = 0;
        std::size_t last =
            project.constructor_default_count();

        while (first < last) {
            const auto middle =
                first +
                (last - first) / 2;

            constructor_default entry;

            if (!project.constructor_default_at(
                    middle,
                    entry)) {

                return shm_type_batch_result::invalid_input;
            }

            if (entry.owner.value() <
                owner.value()) {

                first =
                    middle + 1;
            }
            else {
                last =
                    middle;
            }
        }

        for (;
             first <
                 project.constructor_default_count();
             ++first) {

            constructor_default entry;

            if (!project.constructor_default_at(
                    first,
                    entry)) {

                return shm_type_batch_result::invalid_input;
            }

            if (entry.owner != owner) {
                break;
            }

            constructor_path_reader path{
                project.string(
                    entry.path)};

            auto record =
                handle;

            shm_offset destination = 0;
            type_ref target_type{};

            while (!path.remaining.empty()) {
                std::string_view field;
                std::uint64_t index = 0;

                if (!path.next(
                        field,
                        index)) {

                    return shm_type_batch_result::invalid_input;
                }

                if (field.empty()) {
                    derived_type_record array;
                    shm_value_layout child;

                    if (!project.derived(
                            target_type,
                            array) ||
                        array.kind !=
                            derived_type_kind::
                                bounded_array ||
                        index >=
                            array.payload ||
                        !value_layout(
                            array.child,
                            child) ||
                        (child.size != 0 &&
                         index >
                            (std::numeric_limits<
                                shm_offset>::max)() /
                                child.size)) {

                        return shm_type_batch_result::invalid_input;
                    }

                    const auto delta =
                        index *
                        child.size;

                    if (destination >
                            root_layout.size ||
                        delta >
                            root_layout.size -
                                destination) {

                        return shm_type_batch_result::invalid_input;
                    }

                    destination += delta;
                    target_type =
                        array.child;

                    continue;
                }

                if (target_type &&
                    !project.named(
                        target_type,
                        record)) {

                    return shm_type_batch_result::invalid_input;
                }

                const auto name =
                    project.find_string(
                        field);

                const auto local =
                    project.find_member(
                        record,
                        name);

                type_entry type;
                member_record member;
                shm_record_offset offset = 0;

                if (!name ||
                    !local ||
                    !project.type(
                        record,
                        type) ||
                    !project.member_at(
                        static_cast<std::size_t>(
                            type.members.begin) +
                            local.value(),
                        member) ||
                    !layout.member_offset(
                        static_cast<std::size_t>(
                            type.members.begin) +
                            local.value(),
                        offset) ||
                    destination >
                        root_layout.size ||
                    static_cast<shm_offset>(
                        offset) >
                        root_layout.size -
                            destination) {

                    return shm_type_batch_result::invalid_input;
                }

                destination +=
                    static_cast<shm_offset>(
                        offset);

                target_type =
                    member.type;
            }

            if (target_type.kind() !=
                    type_ref_kind::intrinsic ||
                destination >=
                    static_cast<shm_offset>(
                        (std::numeric_limits<
                            shm_record_offset>::max)())) {

                return shm_type_batch_result::invalid_input;
            }

            const auto appended =
                append_constructor_intrinsic(
                    static_cast<shm_record_offset>(
                        destination),
                    static_cast<intrinsic_type>(
                        target_type.payload()),
                    entry.value);

            if (appended !=
                shm_type_batch_result::success) {

                return appended;
            }

            if (telemetry != nullptr) {
                ++telemetry->constructor_defaults;
            }
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] bool value_layout(
        type_ref type,
        shm_value_layout& value) {

        value = {};

        if (type.kind() == type_ref_kind::named) {
            type_handle handle;

            if (resolve_named(type, handle) !=
                shm_type_batch_result::success) {
                return false;
            }

            return layout.type(handle, value);
        }

        return layout.value(type, value);
    }

    [[nodiscard]] bool reference_referent(
        type_ref type,
        type_ref& output_type) const noexcept {

        output_type = {};
        derived_type_record derived;

        if (!project.derived(type, derived) ||
            (derived.kind !=
                 derived_type_kind::lvalue_reference &&
             derived.kind !=
                 derived_type_kind::rvalue_reference)) {
            return false;
        }

        output_type = derived.child;
        return static_cast<bool>(output_type);
    }

    [[nodiscard]] shm_type_batch_result append_reference_zero(
        shm_record_offset target,
        type_ref referent) {

        shm_offset source = 0;

        if (!layout.unconnected_offset(
                referent,
                source)) {
            return shm_type_batch_result::invalid_input;
        }

        return append_absolute_reference(
            target,
            source);
    }

    [[nodiscard]] shm_type_batch_result append_member_reference(
        const type_entry& record_type,
        std::uint32_t target_local,
        shm_record_offset target_offset) {

        auto current_local = target_local;

        const auto maximum_steps =
            static_cast<std::uint64_t>(
                record_type.members.count) +
            1;

        for (std::uint64_t step = 0;
             step < maximum_steps;
             ++step) {

            if (current_local >=
                record_type.members.count) {
                return shm_type_batch_result::invalid_input;
            }

            const auto global =
                static_cast<std::size_t>(
                    record_type.members.begin) +
                current_local;

            member_record member;
            construction_value construction;

            if (!project.member_at(global, member) ||
                !project.construction_at(
                    global,
                    construction)) {
                return shm_type_batch_result::invalid_input;
            }

            type_ref referent;

            if (!reference_referent(
                    member.type,
                    referent)) {
                return shm_type_batch_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_chain_steps_resolved;
            }

            switch (construction.kind) {
            case construction_kind::zero:
                return append_reference_zero(
                    target_offset,
                    referent);

            case construction_kind::member_binding: {
                if (construction.operand == 0) {
                    return shm_type_batch_result::invalid_input;
                }

                const auto source_local =
                    construction.operand - 1;

                if (source_local >=
                    record_type.members.count) {
                    return shm_type_batch_result::invalid_input;
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
                    return shm_type_batch_result::invalid_input;
                }

                type_ref source_referent;

                if (!reference_referent(
                        source_member.type,
                        source_referent)) {
                    return append_relative_reference(
                        target_offset,
                        source_offset);
                }

                current_local = source_local;
                continue;
            }

            case construction_kind::object_binding: {
                const auto identity =
                    identity_ref::from_raw(
                        construction.operand);

                object_handle object;

                const auto resolved =
                    resolve_object(
                        identity,
                        object);

                if (resolved !=
                    shm_type_batch_result::success) {
                    return resolved;
                }

                return append_object_reference(
                    target_offset,
                    object.value());
            }

            case construction_kind::signed_integer:
            case construction_kind::unsigned_integer:
            case construction_kind::real:
            case construction_kind::unsupported:
                return shm_type_batch_result::invalid_input;
            }
        }

        return shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] bool inline_leaf_candidate(
        std::uint32_t type_api,
        shm_type_batch::type_api& api,
        std::uint32_t& operation_count) const noexcept {

        api = {};
        operation_count = 0;

        const auto inline_limit =
            type_batch_inline_leaf_limit_for_policy(
                policy);

        if (inline_limit == 0 ||
            type_api == 0 ||
            type_api > output.type_apis.size()) {

            return false;
        }

        api =
            output.type_apis[
                type_api - 1];

        if (api.children.count != 0 ||
            api.repeats.count != 0 ||
            api.post_stores.count != 0) {

            return false;
        }

        const auto total =
            static_cast<std::uint64_t>(
                api.relative_references.count) +
            api.absolute_references.count +
            api.object_references.count +
            api.stores.count;

        if (total == 0 ||
            total > inline_limit) {

            return false;
        }

        operation_count =
            static_cast<std::uint32_t>(
                total);

        return true;
    }

    [[nodiscard]] bool valid_inline_range(
        shm_type_batch::range range,
        std::size_t size) const noexcept {

        return range.begin <= size &&
            range.count <=
                size - range.begin;
    }

    [[nodiscard]] shm_type_batch_result
    append_inline_local_operations(
        shm_record_offset target,
        const shm_type_batch::type_api& api) {

        if (!valid_inline_range(
                api.relative_references,
                output.relative_references.size()) ||
            !valid_inline_range(
                api.absolute_references,
                output.absolute_references.size()) ||
            !valid_inline_range(
                api.object_references,
                output.object_references.size()) ||
            !valid_inline_range(
                api.stores,
                output.stores.size())) {

            return shm_type_batch_result::invalid_input;
        }

        const auto relative_count =
            api.relative_references.count;
        const auto absolute_count =
            api.absolute_references.count;
        const auto object_count =
            api.object_references.count;
        const auto store_count =
            api.stores.count;

        const auto relative_begin =
            api.relative_references.begin;
        const auto absolute_begin =
            api.absolute_references.begin;
        const auto object_begin =
            api.object_references.begin;
        const auto store_begin =
            api.stores.begin;

        for (std::uint32_t index = 0;
             index < relative_count;
             ++index) {

            const auto source_operation =
                output.relative_references[
                    static_cast<std::size_t>(
                        relative_begin) +
                    index];

            shm_record_offset new_target = 0;
            shm_record_offset new_source = 0;

            if (!add_record_offset(
                    target,
                    source_operation.target,
                    new_target) ||
                !add_record_offset(
                    target,
                    source_operation.source,
                    new_source)) {

                return shm_type_batch_result::overflow;
            }

            output.relative_references.push_back({
                new_target,
                new_source,
            });
        }

        for (std::uint32_t index = 0;
             index < absolute_count;
             ++index) {

            auto operation =
                output.absolute_references[
                    static_cast<std::size_t>(
                        absolute_begin) +
                    index];

            shm_record_offset new_target = 0;

            if (!add_record_offset(
                    target,
                    operation.target,
                    new_target)) {

                return shm_type_batch_result::overflow;
            }

            operation.target = new_target;
            output.absolute_references.push_back(
                operation);
        }

        for (std::uint32_t index = 0;
             index < object_count;
             ++index) {

            auto operation =
                output.object_references[
                    static_cast<std::size_t>(
                        object_begin) +
                    index];

            shm_record_offset new_target = 0;

            if (!add_record_offset(
                    target,
                    operation.target,
                    new_target)) {

                return shm_type_batch_result::overflow;
            }

            operation.target = new_target;
            output.object_references.push_back(
                operation);
        }

        for (std::uint32_t index = 0;
             index < store_count;
             ++index) {

            auto operation =
                output.stores[
                    static_cast<std::size_t>(
                        store_begin) +
                    index];

            shm_record_offset new_target = 0;

            if (!add_record_offset(
                    target,
                    operation.target,
                    new_target)) {

                return shm_type_batch_result::overflow;
            }

            operation.target = new_target;
            output.stores.push_back(
                operation);
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result
    append_inline_leaf(
        shm_record_offset target,
        const shm_type_batch::type_api& api,
        std::uint32_t operation_count) {

        if (api.children.count != 0 ||
            api.repeats.count != 0) {

            return shm_type_batch_result::invalid_input;
        }

        const auto appended =
            append_inline_local_operations(
                target,
                api);

        if (appended !=
            shm_type_batch_result::success) {

            return appended;
        }

        if (telemetry != nullptr) {
            ++telemetry->inline_leaf_children;
            telemetry->inline_leaf_operations +=
                operation_count;
            telemetry->inline_leaf_relative_references +=
                api.relative_references.count;
            telemetry->inline_leaf_absolute_references +=
                api.absolute_references.count;
            telemetry->inline_leaf_object_references +=
                api.object_references.count;
            telemetry->inline_leaf_stores +=
                api.stores.count;
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] bool subtree_inline_cost(
        std::uint32_t type_api,
        std::uint32_t parent_api,
        std::uint32_t& operation_count) const noexcept {

        operation_count = 0;

        if (policy !=
                type_batch_build_policy::inline_subtree_08 ||
            type_api == 0 ||
            type_api > output.type_apis.size()) {

            return false;
        }

        if (parent_api != 0 &&
            type_api >= parent_api) {

            return false;
        }

        const auto& api =
            output.type_apis[
                type_api - 1];

        if (!valid_inline_range(
                api.children,
                output.children.size()) ||
            !valid_inline_range(
                api.repeats,
                output.repeats.size()) ||
            !valid_inline_range(
                api.post_stores,
                output.post_stores.size()) ||
            api.repeats.count != 0 ||
            api.post_stores.count != 0) {

            return false;
        }

        std::uint64_t total =
            static_cast<std::uint64_t>(
                api.relative_references.count) +
            api.absolute_references.count +
            api.object_references.count +
            api.stores.count;

        if (total >
            type_batch_inline_subtree_limit) {

            return false;
        }

        for (std::uint32_t index = 0;
             index < api.children.count;
             ++index) {

            const auto& child =
                output.children[
                    static_cast<std::size_t>(
                        api.children.begin) +
                    index];

            std::uint32_t child_count = 0;

            if (!subtree_inline_cost(
                    child.type_api,
                    type_api,
                    child_count)) {

                return false;
            }

            total += child_count;

            if (total >
                type_batch_inline_subtree_limit) {

                return false;
            }
        }

        if (total == 0) {
            return false;
        }

        operation_count =
            static_cast<std::uint32_t>(
                total);

        return true;
    }

    [[nodiscard]] shm_type_batch_result
    append_inline_subtree(
        shm_record_offset target,
        std::uint32_t type_api) {

        if (type_api == 0 ||
            type_api > output.type_apis.size()) {

            return shm_type_batch_result::invalid_input;
        }

        const auto api =
            output.type_apis[
                type_api - 1];

        if (!valid_inline_range(
                api.children,
                output.children.size()) ||
            api.repeats.count != 0) {

            return shm_type_batch_result::invalid_input;
        }

        const auto appended =
            append_inline_local_operations(
                target,
                api);

        if (appended !=
            shm_type_batch_result::success) {

            return appended;
        }

        const auto child_begin =
            api.children.begin;
        const auto child_count =
            api.children.count;

        for (std::uint32_t index = 0;
             index < child_count;
             ++index) {

            const auto child =
                output.children[
                    static_cast<std::size_t>(
                        child_begin) +
                    index];

            shm_record_offset child_target = 0;

            if (!add_record_offset(
                    target,
                    child.target,
                    child_target)) {

                return shm_type_batch_result::overflow;
            }

            const auto result =
                append_inline_subtree(
                    child_target,
                    child.type_api);

            if (result !=
                shm_type_batch_result::success) {

                return result;
            }
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result append_child(
        shm_record_offset target,
        std::uint32_t type_api) {

        if (type_api == 0) {
            if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }
            return shm_type_batch_result::success;
        }

        if (type_api > output.type_apis.size()) {
            return shm_type_batch_result::invalid_input;
        }

        if (policy ==
            type_batch_build_policy::inline_subtree_08) {

            std::uint32_t operation_count = 0;

            if (subtree_inline_cost(
                    type_api,
                    0,
                    operation_count)) {

                const auto appended =
                    append_inline_subtree(
                        target,
                        type_api);

                if (appended !=
                    shm_type_batch_result::success) {

                    return appended;
                }

                if (telemetry != nullptr) {
                    ++telemetry->inline_subtree_children;
                    telemetry->inline_subtree_operations +=
                        operation_count;
                }

                return shm_type_batch_result::success;
            }
        }

        shm_type_batch::type_api leaf;
        std::uint32_t operation_count = 0;

        if (inline_leaf_candidate(
                type_api,
                leaf,
                operation_count)) {

            return append_inline_leaf(
                target,
                leaf,
                operation_count);
        }

        output.children.push_back({
            target,
            type_api,
        });

        if (telemetry != nullptr) {
            ++telemetry->child_edges;
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result prepare_value_dependency(
        type_ref type,
        construction_value construction,
        type_batch_mode mode) {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return shm_type_batch_result::success;

        case type_ref_kind::named: {
            type_handle handle;

            const auto resolved =
                resolve_named(type, handle);

            if (resolved !=
                shm_type_batch_result::success) {
                return resolved;
            }

            type_entry entry;

            if (!project.type(handle, entry) ||
                !entry.valid_kind()) {
                return shm_type_batch_result::invalid_input;
            }

            if (entry.kind ==
                graph_type_kind::intrinsic_alias) {
                return shm_type_batch_result::success;
            }

            if (construction.kind !=
                construction_kind::zero) {
                return shm_type_batch_result::invalid_input;
            }

            std::uint32_t ignored = 0;
            return ensure_named(
                handle,
                mode,
                ignored);
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(type, derived)) {
                return shm_type_batch_result::invalid_input;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                return prepare_value_dependency(
                    derived.child,
                    construction,
                    mode);

            case derived_type_kind::pointer:
                return zero_pointer_construction(construction)
                    ? shm_type_batch_result::success
                    : shm_type_batch_result::invalid_input;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference:
                return construction.kind ==
                        construction_kind::zero
                    ? shm_type_batch_result::success
                    : shm_type_batch_result::invalid_input;

            case derived_type_kind::bounded_array: {
                if (construction.kind !=
                        construction_kind::zero ||
                    derived.payload == 0) {
                    return shm_type_batch_result::invalid_input;
                }

                std::uint32_t ignored = 0;
                return ensure_zero_api(
                    derived.child,
                    mode,
                    ignored);
            }

            case derived_type_kind::unbounded_array:
                return shm_type_batch_result::unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] shm_type_batch_result ready_named(
        type_handle handle,
        type_batch_mode mode,
        std::uint32_t& output_api) const noexcept {

        output_api = 0;

        if (!handle ||
            handle.value() > project.type_slot_count()) {
            return shm_type_batch_result::invalid_input;
        }

        const auto& slot =
            mode == type_batch_mode::canonical
            ? canonical_named[handle.value() - 1]
            : default_named[handle.value() - 1];

        if (slot.state != cache_state::ready) {
            return shm_type_batch_result::invalid_input;
        }

        output_api = slot.type_api;
        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result ready_zero_api(
        type_ref type,
        type_batch_mode mode,
        std::uint32_t& output_api) {

        output_api = 0;

        switch (type.kind()) {
        case type_ref_kind::intrinsic: {
            const auto intrinsic =
                static_cast<intrinsic_type>(
                    type.payload());

            if (intrinsic == intrinsic_type::void_type) {
                return shm_type_batch_result::unsupported_type;
            }

            return intrinsic == intrinsic_type::none
                ? shm_type_batch_result::invalid_input
                : shm_type_batch_result::success;
        }

        case type_ref_kind::named: {
            type_handle handle;
            const auto resolved =
                resolve_named(type, handle);

            return resolved ==
                    shm_type_batch_result::success
                ? ready_named(handle, mode, output_api)
                : resolved;
        }

        case type_ref_kind::derived: {
            if (type.payload() == 0 ||
                type.payload() > project.derived_type_count()) {
                return shm_type_batch_result::invalid_input;
            }

            const auto& slot =
                mode == type_batch_mode::canonical
                ? canonical_derived[type.payload() - 1]
                : default_derived[type.payload() - 1];

            if (slot.state != cache_state::ready) {
                return shm_type_batch_result::invalid_input;
            }

            output_api = slot.type_api;
            return shm_type_batch_result::success;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] shm_type_batch_result append_value(
        shm_record_offset target,
        type_ref type,
        construction_value construction,
        type_batch_mode mode) {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return append_intrinsic(
                target,
                static_cast<intrinsic_type>(
                    type.payload()),
                construction);

        case type_ref_kind::named: {
            type_handle handle;

            const auto resolved =
                resolve_named(type, handle);

            if (resolved !=
                shm_type_batch_result::success) {
                return resolved;
            }

            type_entry entry;

            if (!project.type(handle, entry) ||
                !entry.valid_kind()) {
                return shm_type_batch_result::invalid_input;
            }

            if (entry.kind ==
                graph_type_kind::intrinsic_alias) {
                return append_intrinsic(
                    target,
                    entry.alias_intrinsic(),
                    construction);
            }

            if (construction.kind !=
                construction_kind::zero) {
                return shm_type_batch_result::invalid_input;
            }

            std::uint32_t child_api = 0;
            const auto ready =
                ready_named(
                    handle,
                    mode,
                    child_api);

            return ready ==
                    shm_type_batch_result::success
                ? append_child(target, child_api)
                : ready;
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(type, derived)) {
                return shm_type_batch_result::invalid_input;
            }

            switch (derived.kind) {
            case derived_type_kind::const_qualified:
            case derived_type_kind::volatile_qualified:
                return append_value(
                    target,
                    derived.child,
                    construction,
                    mode);

            case derived_type_kind::pointer:
                if (!zero_pointer_construction(construction)) {
                    return shm_type_batch_result::invalid_input;
                }

                if (telemetry != nullptr) {
                    ++telemetry->zero_operations_elided;
                }

                return shm_type_batch_result::success;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference:
                if (construction.kind !=
                    construction_kind::zero) {
                    return shm_type_batch_result::invalid_input;
                }

                return append_reference_zero(
                    target,
                    derived.child);

            case derived_type_kind::bounded_array: {
                if (construction.kind !=
                        construction_kind::zero ||
                    derived.payload == 0) {
                    return shm_type_batch_result::invalid_input;
                }

                shm_value_layout child_layout;

                if (!value_layout(
                        derived.child,
                        child_layout) ||
                    child_layout.size == 0) {
                    return shm_type_batch_result::invalid_input;
                }

                std::uint32_t child_api = 0;
                const auto ready =
                    ready_zero_api(
                        derived.child,
                        mode,
                        child_api);

                if (ready !=
                    shm_type_batch_result::success) {
                    return ready;
                }

                return append_repeat(
                    target,
                    child_api,
                    child_layout.size,
                    derived.payload);
            }

            case derived_type_kind::unbounded_array:
                return shm_type_batch_result::unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] shm_type_batch_result ensure_zero_api(
        type_ref type,
        type_batch_mode mode,
        std::uint32_t& output_api) {

        output_api = 0;

        switch (type.kind()) {
        case type_ref_kind::intrinsic: {
            const auto intrinsic =
                static_cast<intrinsic_type>(
                    type.payload());

            if (intrinsic == intrinsic_type::void_type) {
                return shm_type_batch_result::unsupported_type;
            }

            return intrinsic == intrinsic_type::none
                ? shm_type_batch_result::invalid_input
                : shm_type_batch_result::success;
        }

        case type_ref_kind::named: {
            type_handle handle;
            const auto resolved =
                resolve_named(type, handle);

            return resolved ==
                    shm_type_batch_result::success
                ? ensure_named(
                    handle,
                    mode,
                    output_api)
                : resolved;
        }

        case type_ref_kind::derived:
            return ensure_derived(
                type,
                mode,
                output_api);

        case type_ref_kind::invalid:
            break;
        }

        return shm_type_batch_result::invalid_input;
    }

    [[nodiscard]] shm_type_batch_result ensure_derived(
        type_ref type,
        type_batch_mode mode,
        std::uint32_t& output_api) {

        output_api = 0;

        if (type.kind() != type_ref_kind::derived ||
            type.payload() == 0 ||
            type.payload() > project.derived_type_count()) {
            return shm_type_batch_result::invalid_input;
        }

        auto& slot =
            derived_cache(mode)[type.payload() - 1];

        if (slot.state == cache_state::ready) {
            output_api = slot.type_api;
            return shm_type_batch_result::success;
        }

        if (slot.state == cache_state::preparing) {
            return shm_type_batch_result::invalid_input;
        }

        slot.state = cache_state::preparing;

        derived_type_record derived;

        if (!project.derived(type, derived)) {
            slot = {};
            return shm_type_batch_result::invalid_input;
        }

        std::uint32_t result_api = 0;

        switch (derived.kind) {
        case derived_type_kind::const_qualified:
        case derived_type_kind::volatile_qualified: {
            const auto result =
                ensure_zero_api(
                    derived.child,
                    mode,
                    result_api);

            if (result !=
                shm_type_batch_result::success) {
                slot = {};
                return result;
            }
            break;
        }

        case derived_type_kind::pointer:
            break;

        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference: {
            const auto begin = begin_area();

            const auto appended =
                append_reference_zero(
                    0,
                    derived.child);

            if (appended !=
                shm_type_batch_result::success) {
                slot = {};
                return appended;
            }

            const auto finished =
                finish_api(
                    begin,
                    result_api);

            if (finished !=
                shm_type_batch_result::success) {
                slot = {};
                return finished;
            }
            break;
        }

        case derived_type_kind::bounded_array: {
            if (derived.payload == 0) {
                slot = {};
                return shm_type_batch_result::invalid_input;
            }

            shm_value_layout child_layout;

            if (!value_layout(
                    derived.child,
                    child_layout) ||
                child_layout.size == 0) {
                slot = {};
                return shm_type_batch_result::invalid_input;
            }

            std::uint32_t child_api = 0;
            const auto prepared =
                ensure_zero_api(
                    derived.child,
                    mode,
                    child_api);

            if (prepared !=
                shm_type_batch_result::success) {
                slot = {};
                return prepared;
            }

            if (child_api != 0) {
                const auto begin = begin_area();

                const auto appended =
                    append_repeat(
                        0,
                        child_api,
                        child_layout.size,
                        derived.payload);

                if (appended !=
                    shm_type_batch_result::success) {
                    slot = {};
                    return appended;
                }

                const auto finished =
                    finish_api(
                        begin,
                        result_api);

                if (finished !=
                    shm_type_batch_result::success) {
                    slot = {};
                    return finished;
                }
            }
            else if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }

            break;
        }

        case derived_type_kind::unbounded_array:
            slot = {};
            return shm_type_batch_result::unsupported_type;
        }

        slot.type_api = result_api;
        slot.state = cache_state::ready;
        output_api = result_api;

        if (telemetry != nullptr) {
            ++telemetry->derived_types_prepared;
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result ensure_named(
        type_handle handle,
        type_batch_mode mode,
        std::uint32_t& output_api) {

        output_api = 0;

        if (!handle ||
            handle.value() > project.type_slot_count()) {
            return shm_type_batch_result::invalid_input;
        }

        auto& slot =
            named_cache(mode)[handle.value() - 1];

        if (slot.state == cache_state::ready) {
            output_api = slot.type_api;
            return shm_type_batch_result::success;
        }

        if (slot.state == cache_state::preparing) {
            return shm_type_batch_result::invalid_input;
        }

        slot.state = cache_state::preparing;

        type_entry type;

        if (!project.type(handle, type) ||
            !type.defined() ||
            !type.valid_kind()) {
            slot = {};
            return shm_type_batch_result::invalid_input;
        }

        if (type.kind ==
            graph_type_kind::intrinsic_alias) {
            slot.state = cache_state::ready;

            if (telemetry != nullptr) {
                ++telemetry->named_types_prepared;
            }

            return shm_type_batch_result::success;
        }

        if (type.record_kind ==
            graph_record_kind::union_type) {
            slot = {};
            return shm_type_batch_result::unsupported_type;
        }

        if (type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {
            slot = {};
            return shm_type_batch_result::invalid_input;
        }

        // Dependency pass. No parent physical ranges have started yet.
        for (std::uint32_t local = 0;
             local < type.bases.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.bases.begin) +
                local;

            base_record base;

            if (!project.base_at(global, base) ||
                base.virtual_base()) {
                slot = {};
                return base.virtual_base()
                    ? shm_type_batch_result::unsupported_type
                    : shm_type_batch_result::invalid_input;
            }

            type_handle child;
            const auto resolved =
                resolve_named(
                    base.type,
                    child);

            if (resolved !=
                shm_type_batch_result::success) {
                slot = {};
                return resolved;
            }

            std::uint32_t ignored = 0;
            const auto prepared =
                ensure_named(
                    child,
                    mode,
                    ignored);

            if (prepared !=
                shm_type_batch_result::success) {
                slot = {};
                return prepared;
            }
        }

        for (std::uint32_t local = 0;
             local < type.members.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.members.begin) +
                local;

            member_record member;

            if (!project.member_at(global, member)) {
                slot = {};
                return shm_type_batch_result::invalid_input;
            }

            type_ref referent;

            if (reference_referent(
                    member.type,
                    referent)) {
                continue;
            }

            construction_value construction{};

            if (mode == type_batch_mode::defaults &&
                !project.construction_at(
                    global,
                    construction)) {
                slot = {};
                return shm_type_batch_result::invalid_input;
            }

            const auto prepared =
                prepare_value_dependency(
                    member.type,
                    construction,
                    mode);

            if (prepared !=
                shm_type_batch_result::success) {
                slot = {};
                return prepared;
            }
        }

        const auto begin = begin_area();

        // Emit only local physical data plus explicit child edges.
        // Nested record/base APIs are retained once and traversed per batch.
        for (std::uint32_t local = 0;
             local < type.bases.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.bases.begin) +
                local;

            base_record base;
            shm_record_offset offset = 0;

            if (!project.base_at(global, base) ||
                !layout.base_offset(global, offset)) {
                slot = {};
                return shm_type_batch_result::invalid_input;
            }

            type_handle child;
            const auto resolved =
                resolve_named(
                    base.type,
                    child);

            if (resolved !=
                shm_type_batch_result::success) {
                slot = {};
                return resolved;
            }

            std::uint32_t child_api = 0;
            const auto ready =
                ready_named(
                    child,
                    mode,
                    child_api);

            if (ready !=
                shm_type_batch_result::success) {
                slot = {};
                return ready;
            }

            const auto appended =
                append_child(
                    offset,
                    child_api);

            if (appended !=
                shm_type_batch_result::success) {
                slot = {};
                return appended;
            }
        }

        for (std::uint32_t local = 0;
             local < type.members.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.members.begin) +
                local;

            member_record member;
            shm_record_offset offset = 0;

            if (!project.member_at(global, member) ||
                !layout.member_offset(global, offset)) {
                slot = {};
                return shm_type_batch_result::invalid_input;
            }

            type_ref referent;
            shm_type_batch_result appended;

            if (reference_referent(
                    member.type,
                    referent)) {

                appended =
                    mode == type_batch_mode::canonical
                    ? append_reference_zero(
                        offset,
                        referent)
                    : append_member_reference(
                        type,
                        local,
                        offset);
            }
            else {
                construction_value construction{};

                if (mode == type_batch_mode::defaults &&
                    !project.construction_at(
                        global,
                        construction)) {
                    slot = {};
                    return shm_type_batch_result::invalid_input;
                }

                appended =
                    append_value(
                        offset,
                        member.type,
                        construction,
                        mode);
            }

            if (appended !=
                shm_type_batch_result::success) {
                slot = {};
                return appended;
            }
        }

        if (mode == type_batch_mode::defaults) {
            const auto constructors =
                append_constructor_defaults(
                    handle);

            if (constructors !=
                shm_type_batch_result::success) {

                slot = {};
                return constructors;
            }
        }

        std::uint32_t result_api = 0;
        const auto finished =
            finish_api(
                begin,
                result_api);

        if (finished !=
            shm_type_batch_result::success) {
            slot = {};
            return finished;
        }

        slot.type_api = result_api;
        slot.state = cache_state::ready;
        output_api = result_api;

        if (telemetry != nullptr) {
            ++telemetry->named_types_prepared;
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result prepare_object_patch(
        type_ref type,
        construction_value construction,
        std::uint32_t& output_patch) {

        output_patch = 0;

        const auto relative_before =
            output.relative_references.size();
        const auto absolute_before =
            output.absolute_references.size();
        const auto object_before =
            output.object_references.size();
        const auto stores_before =
            output.stores.size();
        const auto repeats_before =
            output.repeats.size();

        const auto appended =
            append_value(
                0,
                type,
                construction,
                type_batch_mode::defaults);

        if (appended !=
            shm_type_batch_result::success) {
            return appended;
        }

        if (output.relative_references.size() !=
                relative_before ||
            output.absolute_references.size() !=
                absolute_before ||
            output.object_references.size() !=
                object_before ||
            output.repeats.size() !=
                repeats_before) {
            return shm_type_batch_result::invalid_input;
        }

        // A non-default null pointer/nullptr construction still writes no
        // physical bytes because the fresh SHM is already zeroed.
        if (output.stores.size() == stores_before) {
            return shm_type_batch_result::success;
        }

        if (output.stores.size() != stores_before + 1) {
            return shm_type_batch_result::invalid_input;
        }

        if (output.object_patches.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_type_batch_result::overflow;
        }

        const auto patch = output.stores.back();
        output.stores.pop_back();
        output.object_patches.push_back(patch);

        output_patch =
            static_cast<std::uint32_t>(
                output.object_patches.size());

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result prepare_objects() {

        for (std::size_t index = 0;
             index < project.object_slot_count();
             ++index) {

            const auto handle =
                project.object_at(index);

            // Stable Graph WHERE can contain dead slots.
            if (!handle) {
                continue;
            }

            object_entry object;
            construction_value construction;
            shm_offset offset = 0;

            if (!project.object(handle, object) ||
                !project.construction(
                    handle,
                    construction) ||
                !layout.object_offset(
                    handle,
                    offset) ||
                handle.value() == 0 ||
                handle.value() > output.object_where.size()) {
                return shm_type_batch_result::invalid_input;
            }

            output.object_where[
                handle.value() - 1] =
                offset;

            std::uint32_t type_api = 0;
            std::uint32_t patch = 0;

            if (construction.kind ==
                construction_kind::zero) {

                const auto prepared =
                    ensure_zero_api(
                        object.type,
                        type_batch_mode::defaults,
                        type_api);

                if (prepared !=
                    shm_type_batch_result::success) {
                    return prepared;
                }
            }
            else {
                const auto dependency =
                    prepare_value_dependency(
                        object.type,
                        construction,
                        type_batch_mode::defaults);

                if (dependency !=
                    shm_type_batch_result::success) {
                    return dependency;
                }

                const auto patched =
                    prepare_object_patch(
                        object.type,
                        construction,
                        patch);

                if (patched !=
                    shm_type_batch_result::success) {
                    return patched;
                }
            }

            output.objects.push_back({
                offset,
                type_api,
                patch,
            });
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result prepare_canonical_roots() {

        for (std::size_t index = 0;
             index < layout.unconnected_count();
             ++index) {

            const auto type =
                layout.unconnected_type(index);

            shm_offset offset = 0;

            if (!type ||
                !layout.unconnected_offset(
                    type,
                    offset)) {
                return shm_type_batch_result::invalid_input;
            }

            std::uint32_t type_api = 0;
            const auto prepared =
                ensure_zero_api(
                    type,
                    type_batch_mode::canonical,
                    type_api);

            if (prepared !=
                shm_type_batch_result::success) {
                return prepared;
            }

            if (type_api != 0) {
                output.canonical_roots.push_back({
                    offset,
                    type_api,
                    0,
                });
            }
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result
    validate_object_references() const noexcept {

        for (const auto& reference :
             output.object_references) {

            if (reference.object_slot == 0 ||
                reference.object_slot >
                    output.object_where.size() ||
                output.object_where[
                    reference.object_slot - 1] ==
                    invalid_where()) {
                return shm_type_batch_result::invalid_input;
            }
        }

        return shm_type_batch_result::success;
    }

[[nodiscard]] shm_type_batch_result
    canonicalize_relative_reference_programs() {

        // RUNTIME-SHM-REFERENCE-PROGRAM-CANONICALIZATION-09D-12:
        // type_api already addresses its immutable relative-reference program
        // through an independent {begin,count} range. Canonicalize exact
        // duplicate programs once after final INLINE-64 construction. Runtime
        // continues to execute the same {target,source} records directly.
        if (output.relative_references.empty()) {
            return shm_type_batch_result::success;
        }

        const auto original =
            std::span<const shm_type_batch::relative_reference>{
                output.relative_references.data(),
                output.relative_references.size()};

        std::vector<shm_type_batch::range>
            original_ranges;

        original_ranges.reserve(
            output.type_apis.size());

        for (const auto& api : output.type_apis) {
            const auto range =
                api.relative_references;

            const auto begin =
                static_cast<std::size_t>(
                    range.begin);

            const auto count =
                static_cast<std::size_t>(
                    range.count);

            if (begin > original.size() ||
                count > original.size() - begin) {

                return shm_type_batch_result::
                    invalid_input;
            }

            original_ranges.push_back(
                range);
        }

        std::vector<shm_type_batch::relative_reference>
            canonical;

        canonical.reserve(
            output.relative_references.size());

        std::unordered_map<
            std::uint64_t,
            std::vector<shm_type_batch::range>>
            buckets;

        buckets.reserve(
            output.type_apis.size());

        const auto hash_program =
            [&](shm_type_batch::range range) noexcept {

                std::uint64_t hash =
                    1469598103934665603ull;

                const auto mix =
                    [&hash](
                        std::uint32_t value) noexcept {

                        for (std::uint32_t byte = 0;
                             byte < 4;
                             ++byte) {

                            hash ^=
                                static_cast<std::uint8_t>(
                                    value & 0xffu);

                            hash *=
                                1099511628211ull;

                            value >>= 8;
                        }
                    };

                mix(range.count);

                for (std::uint32_t index = 0;
                     index < range.count;
                     ++index) {

                    const auto& reference =
                        original[
                            static_cast<std::size_t>(
                                range.begin) +
                            index];

                    mix(reference.target);
                    mix(reference.source);
                }

                return hash;
            };

        const auto same_program =
            [&](shm_type_batch::range candidate,
                shm_type_batch::range source) noexcept {

                if (candidate.count != source.count ||
                    candidate.begin >
                        canonical.size() ||
                    candidate.count >
                        canonical.size() -
                            candidate.begin ||
                    source.begin >
                        original.size() ||
                    source.count >
                        original.size() -
                            source.begin) {

                    return false;
                }

                for (std::uint32_t index = 0;
                     index < source.count;
                     ++index) {

                    const auto& left =
                        canonical[
                            static_cast<std::size_t>(
                                candidate.begin) +
                            index];

                    const auto& right =
                        original[
                            static_cast<std::size_t>(
                                source.begin) +
                            index];

                    if (left.target != right.target ||
                        left.source != right.source) {

                        return false;
                    }
                }

                return true;
            };

        for (auto& api : output.type_apis) {
            const auto source =
                api.relative_references;

            if (source.count == 0) {
                api.relative_references = {};
                continue;
            }

            const auto hash =
                hash_program(
                    source);

            auto& candidates =
                buckets[hash];

            bool reused = false;

            for (const auto candidate :
                 candidates) {

                if (same_program(
                        candidate,
                        source)) {

                    api.relative_references =
                        candidate;

                    reused = true;
                    break;
                }
            }

            if (reused) {
                continue;
            }

            const auto begin =
                canonical.size();

            canonical.insert(
                canonical.end(),
                original.begin() +
                    static_cast<std::ptrdiff_t>(
                        source.begin),
                original.begin() +
                    static_cast<std::ptrdiff_t>(
                        source.begin) +
                    source.count);

            shm_type_batch::range range;

            if (!make_range(
                    begin,
                    canonical.size(),
                    range)) {

                return shm_type_batch_result::
                    overflow;
            }

            api.relative_references =
                range;

            candidates.push_back(
                range);
        }

        // Transformation proof: every Type API must resolve to exactly the
        // same relative-reference program as before canonicalization.
        if (original_ranges.size() !=
            output.type_apis.size()) {

            return shm_type_batch_result::
                invalid_input;
        }

        for (std::size_t api_index = 0;
             api_index < output.type_apis.size();
             ++api_index) {

            const auto before =
                original_ranges[api_index];

            const auto after =
                output.type_apis[
                    api_index].
                    relative_references;

            if (before.count != after.count ||
                after.begin >
                    canonical.size() ||
                after.count >
                    canonical.size() -
                        after.begin) {

                return shm_type_batch_result::
                    invalid_input;
            }

            for (std::uint32_t index = 0;
                 index < before.count;
                 ++index) {

                const auto& left =
                    original[
                        static_cast<std::size_t>(
                            before.begin) +
                        index];

                const auto& right =
                    canonical[
                        static_cast<std::size_t>(
                            after.begin) +
                        index];

                if (left.target != right.target ||
                    left.source != right.source) {

                    return shm_type_batch_result::
                        invalid_input;
                }
            }
        }

        output.relative_references =
            std::move(canonical);

        return shm_type_batch_result::
            success;
    }


    [[nodiscard]] shm_type_batch_result build_root_groups() {

        output.object_groups.clear();
        output.object_group_offsets.clear();
        output.canonical_groups.clear();
        output.canonical_group_offsets.clear();

        const auto build =
            [&](
                const auto& roots,
                std::vector<shm_type_batch::root_group>& groups,
                std::vector<shm_offset>& offsets)
                -> shm_type_batch_result {

            std::vector<std::uint32_t> counts(
                output.type_apis.size() + 1,
                0);

            std::size_t total = 0;

            for (const auto& root : roots) {
                if (root.type_api == 0) {
                    continue;
                }

                if (root.type_api >
                    output.type_apis.size()) {
                    return shm_type_batch_result::
                        invalid_input;
                }

                auto& count = counts[root.type_api];

                if (count ==
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {
                    return shm_type_batch_result::
                        overflow;
                }

                ++count;

                if (total ==
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {
                    return shm_type_batch_result::
                        overflow;
                }

                ++total;
            }

            offsets.resize(total);

            std::vector<std::uint32_t> cursor(
                counts.size(),
                0);

            std::uint32_t begin = 0;

            for (std::size_t api = 1;
                 api < counts.size();
                 ++api) {

                const auto count = counts[api];

                if (count == 0) {
                    continue;
                }

                if (api >
                    static_cast<std::size_t>(
                        (std::numeric_limits<
                            std::uint32_t>::max)())) {
                    return shm_type_batch_result::
                        overflow;
                }

                groups.push_back({
                    static_cast<std::uint32_t>(api),
                    {
                        begin,
                        count,
                    },
                });

                cursor[api] = begin;

                if (count >
                    (std::numeric_limits<
                        std::uint32_t>::max)() -
                        begin) {
                    return shm_type_batch_result::
                        overflow;
                }

                begin += count;
            }

            if (begin != total) {
                return shm_type_batch_result::
                    invalid_input;
            }

            for (const auto& root : roots) {
                if (root.type_api == 0) {
                    continue;
                }

                auto& position =
                    cursor[root.type_api];

                if (position >= offsets.size()) {
                    return shm_type_batch_result::
                        invalid_input;
                }

                offsets[position++] =
                    root.offset;
            }

            return shm_type_batch_result::success;
        };

        auto result =
            build(
                output.objects,
                output.object_groups,
                output.object_group_offsets);

        if (result !=
            shm_type_batch_result::success) {
            return result;
        }

        result =
            build(
                output.canonical_roots,
                output.canonical_groups,
                output.canonical_group_offsets);

        return result;
    }

    void fill_telemetry() noexcept {

        if (telemetry == nullptr) {
            return;
        }

        telemetry->type_apis = output.type_apis.size();
        telemetry->canonical_roots =
            output.canonical_roots.size();
        telemetry->objects = output.objects.size();
        telemetry->object_patches =
            output.object_patches.size();

        telemetry->relative_references =
            output.relative_references.size();
        telemetry->absolute_references =
            output.absolute_references.size();
        telemetry->object_references =
            output.object_references.size();
        telemetry->stores = output.stores.size();
        telemetry->children = output.children.size();
        telemetry->repeats = output.repeats.size();
        telemetry->object_groups = output.object_groups.size();
        telemetry->canonical_groups = output.canonical_groups.size();
        telemetry->grouped_object_roots =
            output.object_group_offsets.size();
        telemetry->grouped_canonical_roots =
            output.canonical_group_offsets.size();
        telemetry->batch_size =
            shm_type_batch::execution_batch_size;

        telemetry->inline_leaf_limit =
            type_batch_inline_leaf_limit_for_policy(
                policy);

        telemetry->inline_subtree_limit =
            policy ==
                type_batch_build_policy::inline_subtree_08
            ? type_batch_inline_subtree_limit
            : 0;

        telemetry->type_api_bytes =
            output.type_apis.size() *
            sizeof(shm_type_batch::type_api);
        telemetry->relative_reference_bytes =
            output.relative_references.size() *
            sizeof(shm_type_batch::relative_reference);
        telemetry->absolute_reference_bytes =
            output.absolute_references.size() *
            sizeof(shm_type_batch::absolute_reference);
        telemetry->object_reference_bytes =
            output.object_references.size() *
            sizeof(shm_type_batch::object_reference);
        telemetry->store_bytes =
            output.stores.size() *
            sizeof(shm_type_batch::store_operation);
        telemetry->post_store_bytes =
            output.post_stores.size() *
            sizeof(shm_type_batch::store_operation);
        telemetry->child_bytes =
            output.children.size() *
            sizeof(shm_type_batch::child_operation);
        telemetry->repeat_bytes =
            output.repeats.size() *
            sizeof(shm_type_batch::repeat_operation);
        telemetry->constant_bytes =
            output.constants.size() *
            sizeof(output.constants.front());
        telemetry->object_where_bytes =
            output.object_where.size() *
            sizeof(shm_offset);
        telemetry->object_runtime_bytes =
            output.objects.size() *
            sizeof(shm_type_batch::object_runtime);
        telemetry->canonical_root_bytes =
            output.canonical_roots.size() *
            sizeof(shm_type_batch::canonical_root);
        telemetry->object_group_bytes =
            output.object_groups.size() *
            sizeof(shm_type_batch::root_group);
        telemetry->object_group_offset_bytes =
            output.object_group_offsets.size() *
            sizeof(shm_offset);
        telemetry->canonical_group_bytes =
            output.canonical_groups.size() *
            sizeof(shm_type_batch::root_group);
        telemetry->canonical_group_offset_bytes =
            output.canonical_group_offsets.size() *
            sizeof(shm_offset);
        telemetry->object_patch_bytes =
            output.object_patches.size() *
            sizeof(shm_type_batch::store_operation);
        telemetry->resident_bytes =
            output.resident_bytes();
    }

    const compiled_project_view& project;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    shm_type_batch& output;
    shm_type_batch_prepare_telemetry* telemetry = nullptr;
    type_batch_build_policy policy =
        type_batch_build_policy::local;

    // Temporary semantic-WHO -> Graph-WHERE accelerators. They exist only
    // during prepare. The final Type Batch retains no identity_ref values.
    std::vector<std::uint32_t> named_where;
    std::vector<std::uint32_t> object_identity_to_slot;

    std::vector<cache_slot> canonical_named;
    std::vector<cache_slot> default_named;
    std::vector<cache_slot> canonical_derived;
    std::vector<cache_slot> default_derived;
};

class shm_type_batch_executor final {
public:
    shm_type_batch_executor(
        const shm_type_batch& area,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        std::span<std::byte> shm,
        shm_type_batch_execute_telemetry* telemetry) noexcept
        : area(area),
          abi(abi),
          layout(layout),
          shm(shm),
          telemetry(telemetry),
          type_apis(area.type_apis_view()),
          relative_references(area.relative_references_view()),
          absolute_references(area.absolute_references_view()),
          object_references(area.object_references_view()),
          stores(area.stores_view()),
          post_stores(area.post_stores_view()),
          children(area.children_view()),
          repeats(area.repeats_view()),
          constants(area.constants_view()),
          object_where(area.object_where_view()),
          object_records(area.objects_view()),
          canonical_roots(area.canonical_roots_view()),
          object_groups(area.object_groups_view()),
          object_group_offsets(area.object_group_offsets_view()),
          canonical_groups(area.canonical_groups_view()),
          canonical_group_offsets(area.canonical_group_offsets_view()),
          object_patches(area.object_patches_view()) {
    }

    [[nodiscard]] shm_type_batch_result canonical() noexcept {
        const auto valid = validate();
        if (valid != shm_type_batch_result::success) {
            return valid;
        }

        for (const auto& group : canonical_groups) {
            if (!valid_group(
                    group,
                    canonical_group_offsets.size())) {
                return shm_type_batch_result::invalid_input;
            }

            const auto begin =
                static_cast<std::size_t>(
                    group.roots.begin);
            const auto end =
                begin + group.roots.count;

            for (std::size_t position = begin;
                 position < end;
                 position +=
                     shm_type_batch::execution_batch_size) {

                const auto count =
                    (std::min)(
                        static_cast<std::size_t>(
                            shm_type_batch::
                                execution_batch_size),
                        end - position);

                const auto result =
                    apply_api_batch(
                        group.type_api,
                        std::span<const shm_offset>{
                            canonical_group_offsets.data() +
                                position,
                            count},
                        0);

                if (result !=
                    shm_type_batch_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->canonical_batches;
                }
            }
        }

        if (telemetry != nullptr) {
            telemetry->canonical_roots =
                canonical_roots.size();
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result objects() noexcept {
        const auto valid = validate();
        if (valid != shm_type_batch_result::success) {
            return valid;
        }

        for (const auto& group : object_groups) {
            if (!valid_group(
                    group,
                    object_group_offsets.size())) {
                return shm_type_batch_result::invalid_input;
            }

            const auto begin =
                static_cast<std::size_t>(
                    group.roots.begin);
            const auto end =
                begin + group.roots.count;

            for (std::size_t position = begin;
                 position < end;
                 position +=
                     shm_type_batch::execution_batch_size) {

                const auto count =
                    (std::min)(
                        static_cast<std::size_t>(
                            shm_type_batch::
                                execution_batch_size),
                        end - position);

                const auto result =
                    apply_api_batch(
                        group.type_api,
                        std::span<const shm_offset>{
                            object_group_offsets.data() +
                                position,
                            count},
                        0);

                if (result !=
                    shm_type_batch_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->object_batches;
                }
            }
        }

        // Object-specific construction remains a separate physical patch
        // layer. It is intentionally not part of shared Type API state.
        for (const auto& object : object_records) {
            if (object.patch == 0) {
                continue;
            }

            if (object.patch >
                object_patches.size()) {
                return shm_type_batch_result::invalid_input;
            }

            const auto result =
                execute_store(
                    object_patches[
                        object.patch - 1],
                    object.offset);

            if (result !=
                shm_type_batch_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->object_patch_writes;
            }
        }

        if (telemetry != nullptr) {
            telemetry->objects =
                object_records.size();
        }

        return shm_type_batch_result::success;
    }


    [[nodiscard]] shm_type_batch_result
    objects_object_major() noexcept {

        return objects_object_major_range(
            0,
            object_records.size());
    }

    [[nodiscard]] shm_type_batch_result
    objects_object_major_range(
        std::size_t object_begin,
        std::size_t object_count) noexcept {

        const auto valid = validate();

        if (valid !=
            shm_type_batch_result::success) {

            return valid;
        }

        if (object_begin >
                object_records.size() ||
            object_count >
                object_records.size() -
                    object_begin) {

            return shm_type_batch_result::
                invalid_input;
        }

        const auto object_end =
            object_begin +
            object_count;

        for (std::size_t index = object_begin;
             index < object_end;
             ++index) {

            const auto& object =
                object_records[index];

            if (object.offset >=
                area.layout_size) {

                return shm_type_batch_result::
                    invalid_input;
            }

            if (object.type_api != 0) {
                const auto result =
                    apply_api_one(
                        object.type_api,
                        object.offset);

                if (result !=
                    shm_type_batch_result::
                        success) {

                    return result;
                }
            }

            if (object.patch != 0) {
                if (object.patch >
                    object_patches.size()) {

                    return shm_type_batch_result::
                        invalid_input;
                }

                const auto result =
                    execute_store(
                        object_patches[
                            object.patch - 1],
                        object.offset);

                if (result !=
                    shm_type_batch_result::
                        success) {

                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->
                        object_patch_writes;
                }
            }
        }

        if (telemetry != nullptr) {
            telemetry->objects =
                object_count;
        }

        return shm_type_batch_result::
            success;
    }


private:
    [[nodiscard]] shm_type_batch_result validate() noexcept {

        if (!area.prepared_value ||
            area.target_value != abi.target ||
            layout.target() != abi.target ||
            area.layout_size != layout.size() ||
            area.layout_size > shm.size() ||
            (area.layout_size != 0 &&
             shm.data() == nullptr) ||
            !host_compatible(abi.target)) {
            return shm_type_batch_result::incompatible_abi;
        }

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {
            return shm_type_batch_result::incompatible_abi;
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
            (area.layout_size != 0 &&
             area.layout_size - 1 >
                 mask -
                     static_cast<std::uint64_t>(base))) {
            return shm_type_batch_result::overflow;
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] bool valid_range(
        shm_type_batch::range range,
        std::size_t size) const noexcept {

        return range.begin <= size &&
            range.count <= size - range.begin;
    }

    [[nodiscard]] bool valid_group(
        const shm_type_batch::root_group& group,
        std::size_t roots_size) const noexcept {

        return
            group.type_api != 0 &&
            group.type_api <= type_apis.size() &&
            valid_range(
                group.roots,
                roots_size);
    }

    [[nodiscard]] shm_type_batch_result write_reference(
        std::byte* target,
        const std::byte* source) noexcept {

        if (target == nullptr ||
            source == nullptr) {
            return shm_type_batch_result::invalid_input;
        }

        // RUNTIME-V2-LINKS-03:
        // Static link targets are pre-marked with ~link_handle before object
        // construction. Preserve that marker instead of writing the ordinary
        // member/reference default.
        std::uint64_t stored = 0;

        if (properties.reference_size == 4) {
            std::uint32_t word = 0;

            std::memcpy(
                &word,
                target,
                sizeof(word));

            stored = word;
        }
        else if (properties.reference_size == 8) {
            std::memcpy(
                &stored,
                target,
                sizeof(stored));
        }
        else {
            return shm_type_batch_result::incompatible_abi;
        }

        const auto mask =
            properties.reference_size == 4
            ? static_cast<std::uint64_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())
            : (std::numeric_limits<
                std::uint64_t>::max)();

        const auto decoded =
            (~stored) &
            mask;

        if (stored != mask &&
            decoded != 0 &&
            decoded <=
                link_handle::
                    maximum_slot) {

            if (telemetry != nullptr) {
                ++telemetry->
                    pending_link_preserves;
            }

            return shm_type_batch_result::success;
        }

        const auto value =
            reinterpret_cast<std::uintptr_t>(
                source);

        if (properties.reference_size == 4) {
            if (value >
                (std::numeric_limits<
                    std::uint32_t>::max)()) {
                return shm_type_batch_result::overflow;
            }

            const auto narrowed =
                static_cast<std::uint32_t>(value);

            std::memcpy(
                target,
                &narrowed,
                sizeof(narrowed));

            return shm_type_batch_result::success;
        }

        if (properties.reference_size == 8) {
            const auto widened =
                static_cast<std::uint64_t>(value);

            std::memcpy(
                target,
                &widened,
                sizeof(widened));

            return shm_type_batch_result::success;
        }

        return shm_type_batch_result::incompatible_abi;
    }

    [[nodiscard]] shm_type_batch_result execute_store(
        const shm_type_batch::store_operation& operation,
        shm_offset base_offset) noexcept {

        if (operation.size == 0 ||
            operation.size > 16 ||
            operation.constant >= constants.size() ||
            base_offset >= area.layout_size ||
            operation.target >
                area.layout_size - base_offset ||
            static_cast<shm_offset>(operation.size) >
                area.layout_size - base_offset -
                    operation.target) {
            return shm_type_batch_result::invalid_input;
        }

        std::memcpy(
            shm.data() +
                static_cast<std::size_t>(
                    base_offset + operation.target),
            constants[operation.constant].data(),
            operation.size);

        if (telemetry != nullptr) {
            ++telemetry->store_writes;
        }

        return shm_type_batch_result::success;
    }

    [[nodiscard]] shm_type_batch_result apply_api_batch(
        std::uint32_t type_api,
        std::span<const shm_offset> roots,
        shm_offset relative_base) noexcept {

        if (type_api == 0 ||
            type_api > type_apis.size() ||
            roots.empty()) {
            return shm_type_batch_result::invalid_input;
        }

        const auto api =
            type_apis[type_api - 1];

        if (!valid_range(
                api.relative_references,
                relative_references.size()) ||
            !valid_range(
                api.absolute_references,
                absolute_references.size()) ||
            !valid_range(
                api.object_references,
                object_references.size()) ||
            !valid_range(
                api.stores,
                stores.size()) ||
            !valid_range(
                api.children,
                children.size()) ||
            !valid_range(
                api.repeats,
                repeats.size()) ||
            !valid_range(
                api.post_stores,
                post_stores.size())) {
            return shm_type_batch_result::invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->api_applications;
            ++telemetry->batch_api_applications;
        }

        // Keep one object subobject local while applying this API's own
        // homogeneous ranges. Structural child traversal happens once per
        // batch below, not once per object.
        for (const auto root : roots) {
            if (root >= area.layout_size ||
                relative_base >
                    area.layout_size - root) {
                return shm_type_batch_result::invalid_input;
            }

            const auto base_offset =
                root + relative_base;

            if (base_offset >= area.layout_size) {
                return shm_type_batch_result::invalid_input;
            }

            auto* const base =
                shm.data() +
                static_cast<std::size_t>(
                    base_offset);

            for (std::uint32_t index = 0;
                 index <
                     api.relative_references.count;
                 ++index) {

                const auto& reference =
                    relative_references[
                        static_cast<std::size_t>(
                            api.relative_references.begin) +
                        index];

                if (reference.target >=
                        area.layout_size - base_offset ||
                    reference.source >=
                        area.layout_size - base_offset) {
                    return shm_type_batch_result::invalid_input;
                }

                const auto result =
                    write_reference(
                        base + reference.target,
                        base + reference.source);

                if (result !=
                    shm_type_batch_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->reference_writes;
                    ++telemetry->
                        relative_reference_writes;
                }
            }

            for (std::uint32_t index = 0;
                 index <
                     api.absolute_references.count;
                 ++index) {

                const auto& reference =
                    absolute_references[
                        static_cast<std::size_t>(
                            api.absolute_references.begin) +
                        index];

                if (reference.target >=
                        area.layout_size - base_offset ||
                    reference.source >=
                        area.layout_size) {
                    return shm_type_batch_result::invalid_input;
                }

                const auto result =
                    write_reference(
                        base + reference.target,
                        shm.data() +
                            static_cast<std::size_t>(
                                reference.source));

                if (result !=
                    shm_type_batch_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->reference_writes;
                    ++telemetry->
                        absolute_reference_writes;
                }
            }

            for (std::uint32_t index = 0;
                 index <
                     api.object_references.count;
                 ++index) {

                const auto& reference =
                    object_references[
                        static_cast<std::size_t>(
                            api.object_references.begin) +
                        index];

                if (reference.target >=
                        area.layout_size - base_offset ||
                    reference.object_slot == 0 ||
                    reference.object_slot >
                        object_where.size()) {
                    return shm_type_batch_result::invalid_input;
                }

                const auto source_offset =
                    object_where[
                        reference.object_slot - 1];

                if (source_offset == invalid_where() ||
                    source_offset >= area.layout_size) {
                    return shm_type_batch_result::invalid_input;
                }

                const auto result =
                    write_reference(
                        base + reference.target,
                        shm.data() +
                            static_cast<std::size_t>(
                                source_offset));

                if (result !=
                    shm_type_batch_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->reference_writes;
                    ++telemetry->
                        object_reference_writes;
                }
            }

            for (std::uint32_t index = 0;
                 index < api.stores.count;
                 ++index) {

                const auto result =
                    execute_store(
                        stores[
                            static_cast<std::size_t>(
                                api.stores.begin) +
                            index],
                        base_offset);

                if (result !=
                    shm_type_batch_result::success) {
                    return result;
                }
            }
        }

        for (std::uint32_t index = 0;
             index < api.children.count;
             ++index) {

            const auto& child =
                children[
                    static_cast<std::size_t>(
                        api.children.begin) +
                    index];

            if (child.type_api == 0 ||
                child.type_api >
                    type_apis.size() ||
                child.target >
                    (std::numeric_limits<
                        shm_offset>::max)() -
                        relative_base) {
                return shm_type_batch_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->child_visits;
            }

            const auto result =
                apply_api_batch(
                    child.type_api,
                    roots,
                    relative_base +
                        child.target);

            if (result !=
                shm_type_batch_result::success) {
                return result;
            }
        }

        for (std::uint32_t index = 0;
             index < api.repeats.count;
             ++index) {

            const auto& repeat =
                repeats[
                    static_cast<std::size_t>(
                        api.repeats.begin) +
                    index];

            if (repeat.type_api == 0 ||
                repeat.type_api >
                    type_apis.size() ||
                repeat.stride == 0 ||
                repeat.count == 0 ||
                repeat.target >
                    (std::numeric_limits<
                        shm_offset>::max)() -
                        relative_base) {
                return shm_type_batch_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->repeat_visits;
                telemetry->repeat_iterations +=
                    repeat.count;
            }

            shm_offset current =
                relative_base +
                repeat.target;

            for (std::uint64_t iteration = 0;
                 iteration < repeat.count;
                 ++iteration) {

                const auto result =
                    apply_api_batch(
                        repeat.type_api,
                        roots,
                        current);

                if (result !=
                    shm_type_batch_result::success) {
                    return result;
                }

                if (iteration + 1 !=
                    repeat.count) {

                    if (repeat.stride >
                        (std::numeric_limits<
                            shm_offset>::max)() -
                            current) {
                        return shm_type_batch_result::overflow;
                    }

                    current += repeat.stride;
                }
            }
        }

        for (const auto root : roots) {
            if (root >= area.layout_size ||
                relative_base >
                    area.layout_size - root) {

                return shm_type_batch_result::invalid_input;
            }

            const auto base_offset =
                root + relative_base;

            for (std::uint32_t index = 0;
                 index < api.post_stores.count;
                 ++index) {

                const auto result =
                    execute_store(
                        post_stores[
                            static_cast<std::size_t>(
                                api.post_stores.begin) +
                            index],
                        base_offset);

                if (result !=
                    shm_type_batch_result::success) {

                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->
                        constructor_default_writes;
                }
            }
        }

        return shm_type_batch_result::success;
    }


    [[nodiscard]] shm_type_batch_result apply_api_one(
        std::uint32_t type_api,
        shm_offset base_offset) noexcept {

        if (type_api == 0 ||
            type_api > type_apis.size() ||
            base_offset >= area.layout_size) {
            return shm_type_batch_result::invalid_input;
        }

        const auto api =
            type_apis[type_api - 1];

        if (!valid_range(
                api.relative_references,
                relative_references.size()) ||
            !valid_range(
                api.absolute_references,
                absolute_references.size()) ||
            !valid_range(
                api.object_references,
                object_references.size()) ||
            !valid_range(
                api.stores,
                stores.size()) ||
            !valid_range(
                api.children,
                children.size()) ||
            !valid_range(
                api.repeats,
                repeats.size()) ||
            !valid_range(
                api.post_stores,
                post_stores.size())) {
            return shm_type_batch_result::invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->api_applications;
        }

        auto* const base =
            shm.data() +
            static_cast<std::size_t>(
                base_offset);

        for (std::uint32_t index = 0;
             index < api.relative_references.count;
             ++index) {

            const auto& reference =
                relative_references[
                    static_cast<std::size_t>(
                        api.relative_references.begin) +
                    index];

            if (reference.target >=
                    area.layout_size - base_offset ||
                reference.source >=
                    area.layout_size - base_offset) {
                return shm_type_batch_result::invalid_input;
            }

            const auto result =
                write_reference(
                    base + reference.target,
                    base + reference.source);

            if (result !=
                shm_type_batch_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->
                    relative_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.absolute_references.count;
             ++index) {

            const auto& reference =
                absolute_references[
                    static_cast<std::size_t>(
                        api.absolute_references.begin) +
                    index];

            if (reference.target >=
                    area.layout_size - base_offset ||
                reference.source >=
                    area.layout_size) {
                return shm_type_batch_result::invalid_input;
            }

            const auto result =
                write_reference(
                    base + reference.target,
                    shm.data() +
                        static_cast<std::size_t>(
                            reference.source));

            if (result !=
                shm_type_batch_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->
                    absolute_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.object_references.count;
             ++index) {

            const auto& reference =
                object_references[
                    static_cast<std::size_t>(
                        api.object_references.begin) +
                    index];

            if (reference.target >=
                    area.layout_size - base_offset ||
                reference.object_slot == 0 ||
                reference.object_slot >
                    object_where.size()) {
                return shm_type_batch_result::invalid_input;
            }

            const auto source_offset =
                object_where[
                    reference.object_slot - 1];

            if (source_offset == invalid_where() ||
                source_offset >= area.layout_size) {
                return shm_type_batch_result::invalid_input;
            }

            const auto result =
                write_reference(
                    base + reference.target,
                    shm.data() +
                        static_cast<std::size_t>(
                            source_offset));

            if (result !=
                shm_type_batch_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->
                    object_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.stores.count;
             ++index) {

            const auto result =
                execute_store(
                    stores[
                        static_cast<std::size_t>(
                            api.stores.begin) +
                        index],
                    base_offset);

            if (result !=
                shm_type_batch_result::success) {
                return result;
            }
        }

        for (std::uint32_t index = 0;
             index < api.children.count;
             ++index) {

            const auto& child =
                children[
                    static_cast<std::size_t>(
                        api.children.begin) +
                    index];

            if (child.type_api == 0 ||
                child.type_api >
                    type_apis.size() ||
                child.target >
                    area.layout_size - base_offset) {
                return shm_type_batch_result::invalid_input;
            }

            const auto child_base =
                base_offset +
                child.target;

            if (child_base >= area.layout_size) {
                return shm_type_batch_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->child_visits;
            }

            const auto result =
                apply_api_one(
                    child.type_api,
                    child_base);

            if (result !=
                shm_type_batch_result::success) {
                return result;
            }
        }

        for (std::uint32_t index = 0;
             index < api.repeats.count;
             ++index) {

            const auto& repeat =
                repeats[
                    static_cast<std::size_t>(
                        api.repeats.begin) +
                    index];

            if (repeat.type_api == 0 ||
                repeat.type_api >
                    type_apis.size() ||
                repeat.stride == 0 ||
                repeat.count == 0 ||
                repeat.target >
                    area.layout_size - base_offset) {
                return shm_type_batch_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->repeat_visits;
                telemetry->repeat_iterations +=
                    repeat.count;
            }

            auto current =
                base_offset +
                repeat.target;

            for (std::uint64_t iteration = 0;
                 iteration < repeat.count;
                 ++iteration) {

                if (current >= area.layout_size) {
                    return shm_type_batch_result::invalid_input;
                }

                const auto result =
                    apply_api_one(
                        repeat.type_api,
                        current);

                if (result !=
                    shm_type_batch_result::success) {
                    return result;
                }

                if (iteration + 1 !=
                    repeat.count) {

                    if (repeat.stride >
                        (std::numeric_limits<
                            shm_offset>::max)() -
                            current) {
                        return shm_type_batch_result::overflow;
                    }

                    current += repeat.stride;
                }
            }
        }

        for (std::uint32_t index = 0;
             index < api.post_stores.count;
             ++index) {

            const auto result =
                execute_store(
                    post_stores[
                        static_cast<std::size_t>(
                            api.post_stores.begin) +
                        index],
                    base_offset);

            if (result !=
                shm_type_batch_result::success) {

                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->
                    constructor_default_writes;
            }
        }

        return shm_type_batch_result::success;
    }

    const shm_type_batch& area;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    std::span<std::byte> shm;
    shm_type_batch_execute_telemetry* telemetry = nullptr;

    std::span<const shm_type_batch::type_api> type_apis;
    std::span<const shm_type_batch::relative_reference> relative_references;
    std::span<const shm_type_batch::absolute_reference> absolute_references;
    std::span<const shm_type_batch::object_reference> object_references;
    std::span<const shm_type_batch::store_operation> stores;
    std::span<const shm_type_batch::store_operation> post_stores;
    std::span<const shm_type_batch::child_operation> children;
    std::span<const shm_type_batch::repeat_operation> repeats;
    std::span<const std::array<std::byte, 16>> constants;
    std::span<const shm_offset> object_where;
    std::span<const shm_type_batch::object_runtime> object_records;
    std::span<const shm_type_batch::canonical_root> canonical_roots;
    std::span<const shm_type_batch::root_group> object_groups;
    std::span<const shm_offset> object_group_offsets;
    std::span<const shm_type_batch::root_group> canonical_groups;
    std::span<const shm_offset> canonical_group_offsets;
    std::span<const shm_type_batch::store_operation> object_patches;

    abi_properties properties{};
};

std::size_t shm_type_batch::resident_bytes() const noexcept {

    if (persisted_view) {
        return 0;
    }

    return
        type_apis.size() * sizeof(type_api) +
        relative_references.size() * sizeof(relative_reference) +
        absolute_references.size() * sizeof(absolute_reference) +
        object_references.size() * sizeof(object_reference) +
        stores.size() * sizeof(store_operation) +
        post_stores.size() * sizeof(store_operation) +
        children.size() * sizeof(child_operation) +
        repeats.size() * sizeof(repeat_operation) +
        constants.size() * sizeof(constants.front()) +
        object_where.size() * sizeof(shm_offset) +
        objects.size() * sizeof(object_runtime) +
        canonical_roots.size() * sizeof(canonical_root) +
        object_groups.size() * sizeof(root_group) +
        object_group_offsets.size() * sizeof(shm_offset) +
        canonical_groups.size() * sizeof(root_group) +
        canonical_group_offsets.size() * sizeof(shm_offset) +
        object_patches.size() * sizeof(store_operation);
}

void shm_type_batch::reset() noexcept {

    type_apis.clear();
    relative_references.clear();
    absolute_references.clear();
    object_references.clear();
    stores.clear();
    post_stores.clear();
    children.clear();
    repeats.clear();
    constants.clear();
    object_where.clear();
    objects.clear();
    canonical_roots.clear();
    object_groups.clear();
    object_group_offsets.clear();
    canonical_groups.clear();
    canonical_group_offsets.clear();
    object_patches.clear();

    persisted_type_apis = {};
    persisted_relative_references = {};
    persisted_absolute_references = {};
    persisted_object_references = {};
    persisted_stores = {};
    persisted_post_stores = {};
    persisted_children = {};
    persisted_repeats = {};
    persisted_constants = {};
    persisted_object_where = {};
    persisted_objects = {};
    persisted_canonical_roots = {};
    persisted_object_groups = {};
    persisted_object_group_offsets = {};
    persisted_canonical_groups = {};
    persisted_canonical_group_offsets = {};
    persisted_object_patches = {};
    persisted_view = false;

    target_value = abi_target::windows_x64;
    layout_size = 0;
    prepared_value = false;
}


namespace {

template <typename T>
[[nodiscard]] std::span<T>
runtime_type_mutable_records(
    std::span<std::byte> bytes) noexcept {

    return {
        reinterpret_cast<T*>(
            bytes.data()),
        bytes.size() /
            sizeof(T),
    };
}

template <typename T>
[[nodiscard]] std::span<const T>
runtime_type_records(
    std::span<const std::byte> bytes) noexcept {

    return {
        reinterpret_cast<const T*>(
            bytes.data()),
        bytes.size() /
            sizeof(T),
    };
}

}

void shm_type_batch_compiled_counts(
    const shm_type_batch& area,
    compiled_project_runtime_type_counts& output) noexcept {

    output = {
        area.type_apis.size(),
        area.relative_references.size(),
        area.absolute_references.size(),
        area.object_references.size(),
        area.stores.size(),
        area.post_stores.size(),
        area.children.size(),
        area.repeats.size(),
        area.constants.size(),
        area.object_where.size(),
        area.objects.size(),
        area.canonical_roots.size(),
        area.object_groups.size(),
        area.object_group_offsets.size(),
        area.canonical_groups.size(),
        area.canonical_group_offsets.size(),
        area.object_patches.size(),
    };
}

shm_type_batch_result
encode_shm_type_batch_physical_columns(
    const shm_type_batch& area,
    std::span<std::byte> compiled_image) noexcept {

    if (!area.prepared_value ||
        area.persisted_view) {

        return shm_type_batch_result::
            invalid_input;
    }

    const auto section =
        [&](compiled_project_section kind) noexcept {
            return
                compiled_project_runtime_physical_section(
                    compiled_image,
                    kind);
        };

    const auto copy_column =
        [&]<typename T>(
            compiled_project_section kind,
            const std::vector<T>& source) noexcept {

            auto target =
                runtime_type_mutable_records<T>(
                    section(kind));

            if (target.size() !=
                source.size()) {

                return false;
            }

            std::copy(
                source.begin(),
                source.end(),
                target.begin());

            return true;
        };

    return
        copy_column(
            compiled_project_section::
                runtime_type_apis,
            area.type_apis) &&
        copy_column(
            compiled_project_section::
                runtime_type_relative_references,
            area.relative_references) &&
        copy_column(
            compiled_project_section::
                runtime_type_absolute_references,
            area.absolute_references) &&
        copy_column(
            compiled_project_section::
                runtime_type_object_references,
            area.object_references) &&
        copy_column(
            compiled_project_section::
                runtime_type_stores,
            area.stores) &&
        copy_column(
            compiled_project_section::
                runtime_type_post_stores,
            area.post_stores) &&
        copy_column(
            compiled_project_section::
                runtime_type_children,
            area.children) &&
        copy_column(
            compiled_project_section::
                runtime_type_repeats,
            area.repeats) &&
        copy_column(
            compiled_project_section::
                runtime_type_constants,
            area.constants) &&
        copy_column(
            compiled_project_section::
                runtime_type_object_where,
            area.object_where) &&
        copy_column(
            compiled_project_section::
                runtime_type_objects,
            area.objects) &&
        copy_column(
            compiled_project_section::
                runtime_type_canonical_roots,
            area.canonical_roots) &&
        copy_column(
            compiled_project_section::
                runtime_type_object_groups,
            area.object_groups) &&
        copy_column(
            compiled_project_section::
                runtime_type_object_group_offsets,
            area.object_group_offsets) &&
        copy_column(
            compiled_project_section::
                runtime_type_canonical_groups,
            area.canonical_groups) &&
        copy_column(
            compiled_project_section::
                runtime_type_canonical_group_offsets,
            area.canonical_group_offsets) &&
        copy_column(
            compiled_project_section::
                runtime_type_object_patches,
            area.object_patches)
        ? shm_type_batch_result::success
        : shm_type_batch_result::invalid_input;
}

shm_type_batch_result
attach_shm_type_batch_physical_columns(
    const compiled_project_view& project,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry) noexcept {

    output.reset();

    const auto section =
        [&](compiled_project_section kind) noexcept {
            return
                project.runtime_physical_section(
                    kind);
        };

    output.persisted_type_apis =
        runtime_type_records<
            shm_type_batch::type_api>(
                section(
                    compiled_project_section::
                        runtime_type_apis));

    output.persisted_relative_references =
        runtime_type_records<
            shm_type_batch::relative_reference>(
                section(
                    compiled_project_section::
                        runtime_type_relative_references));

    output.persisted_absolute_references =
        runtime_type_records<
            shm_type_batch::absolute_reference>(
                section(
                    compiled_project_section::
                        runtime_type_absolute_references));

    output.persisted_object_references =
        runtime_type_records<
            shm_type_batch::object_reference>(
                section(
                    compiled_project_section::
                        runtime_type_object_references));

    output.persisted_stores =
        runtime_type_records<
            shm_type_batch::store_operation>(
                section(
                    compiled_project_section::
                        runtime_type_stores));

    output.persisted_post_stores =
        runtime_type_records<
            shm_type_batch::store_operation>(
                section(
                    compiled_project_section::
                        runtime_type_post_stores));

    output.persisted_children =
        runtime_type_records<
            shm_type_batch::child_operation>(
                section(
                    compiled_project_section::
                        runtime_type_children));

    output.persisted_repeats =
        runtime_type_records<
            shm_type_batch::repeat_operation>(
                section(
                    compiled_project_section::
                        runtime_type_repeats));

    output.persisted_constants =
        runtime_type_records<
            std::array<std::byte, 16>>(
                section(
                    compiled_project_section::
                        runtime_type_constants));

    output.persisted_object_where =
        runtime_type_records<
            shm_offset>(
                section(
                    compiled_project_section::
                        runtime_type_object_where));

    output.persisted_objects =
        runtime_type_records<
            shm_type_batch::object_runtime>(
                section(
                    compiled_project_section::
                        runtime_type_objects));

    output.persisted_canonical_roots =
        runtime_type_records<
            shm_type_batch::canonical_root>(
                section(
                    compiled_project_section::
                        runtime_type_canonical_roots));

    output.persisted_object_groups =
        runtime_type_records<
            shm_type_batch::root_group>(
                section(
                    compiled_project_section::
                        runtime_type_object_groups));

    output.persisted_object_group_offsets =
        runtime_type_records<
            shm_offset>(
                section(
                    compiled_project_section::
                        runtime_type_object_group_offsets));

    output.persisted_canonical_groups =
        runtime_type_records<
            shm_type_batch::root_group>(
                section(
                    compiled_project_section::
                        runtime_type_canonical_groups));

    output.persisted_canonical_group_offsets =
        runtime_type_records<
            shm_offset>(
                section(
                    compiled_project_section::
                        runtime_type_canonical_group_offsets));

    output.persisted_object_patches =
        runtime_type_records<
            shm_type_batch::store_operation>(
                section(
                    compiled_project_section::
                        runtime_type_object_patches));

    output.target_value =
        layout.target();

    output.layout_size =
        layout.size();

    output.persisted_view = true;
    output.prepared_value = true;

    if (telemetry != nullptr) {
        *telemetry = {};
        telemetry->type_apis =
            output.persisted_type_apis.size();
        telemetry->relative_references =
            output.persisted_relative_references.size();
        telemetry->objects =
            output.persisted_objects.size();
        telemetry->canonical_roots =
            output.persisted_canonical_roots.size();
        telemetry->inline_leaf_limit = 64;
        telemetry->resident_bytes = 0;
    }

    return shm_type_batch_result::
        success;
}

shm_type_batch_result prepare_shm_type_batch(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_type_batch_builder builder{
            project,
            abi,
            layout,
            output,
            telemetry,
        };

        return builder.build();
    }
    catch (...) {
        output.reset();
        return shm_type_batch_result::failed;
    }
}

shm_type_batch_result prepare_shm_type_batch_inline08(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_type_batch_builder builder{
            project,
            abi,
            layout,
            output,
            telemetry,
            type_batch_build_policy::
                inline_leaf_08,
        };

        return builder.build();
    }
    catch (...) {
        output.reset();
        return shm_type_batch_result::failed;
    }
}

shm_type_batch_result prepare_shm_type_batch_subtree08(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_type_batch_builder builder{
            project,
            abi,
            layout,
            output,
            telemetry,
            type_batch_build_policy::inline_subtree_08,
        };

        return builder.build();
    }
    catch (...) {
        output.reset();
        return shm_type_batch_result::failed;
    }
}

shm_type_batch_result prepare_shm_type_batch_inline16(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_type_batch_builder builder{
            project,
            abi,
            layout,
            output,
            telemetry,
            type_batch_build_policy::inline_leaf_16,
        };

        return builder.build();
    }
    catch (...) {
        output.reset();
        return shm_type_batch_result::failed;
    }
}

shm_type_batch_result prepare_shm_type_batch_inline32(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_type_batch_builder builder{
            project,
            abi,
            layout,
            output,
            telemetry,
            type_batch_build_policy::inline_leaf_32,
        };

        return builder.build();
    }
    catch (...) {
        output.reset();
        return shm_type_batch_result::failed;
    }
}

shm_type_batch_result prepare_shm_type_batch_inline64(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_batch& output,
    shm_type_batch_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_type_batch_builder builder{
            project,
            abi,
            layout,
            output,
            telemetry,
            type_batch_build_policy::inline_leaf_64,
        };

        return builder.build();
    }
    catch (...) {
        output.reset();
        return shm_type_batch_result::failed;
    }
}

shm_type_batch_result materialize_shm_type_batch_canonical(
    const shm_type_batch& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_batch_execute_telemetry* telemetry) noexcept {

    shm_type_batch_executor executor{
        area,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.canonical();
}

shm_type_batch_result materialize_shm_type_batch_objects(
    const shm_type_batch& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_batch_execute_telemetry* telemetry) noexcept {

    shm_type_batch_executor executor{
        area,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.objects();
}


shm_type_batch_result
materialize_shm_type_batch_objects_object_major(
    const shm_type_batch& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_batch_execute_telemetry* telemetry) noexcept {

    shm_type_batch_executor executor{
        area,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.objects_object_major();
}


shm_type_batch_result
materialize_shm_type_batch_objects_object_major_range(
    const shm_type_batch& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    std::size_t object_begin,
    std::size_t object_count,
    shm_type_batch_execute_telemetry* telemetry) noexcept {

    shm_type_batch_executor executor{
        area,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.objects_object_major_range(
        object_begin,
        object_count);
}


}
