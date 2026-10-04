#include "shm_type_area.hpp"

#include "../abi/abi_layout.hpp"
#include "../persistence/compiled_project.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
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

enum class type_area_mode : std::uint8_t {
    canonical = 0,
    defaults,
};

enum class cache_state : std::uint8_t {
    empty = 0,
    preparing,
    ready,
};

[[nodiscard]] constexpr shm_offset invalid_where() noexcept {
    return (std::numeric_limits<shm_offset>::max)();
}

}

class shm_type_area_builder final {
public:
    shm_type_area_builder(
        const compiled_project_view& project,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        shm_type_area& output,
        shm_type_area_prepare_telemetry* telemetry)
        : project(project),
          abi(abi),
          layout(layout),
          output(output),
          telemetry(telemetry) {
    }

    [[nodiscard]] shm_type_area_result build() {

        output.reset();

        if (!project.valid() ||
            layout.target() != abi.target ||
            !host_compatible(abi.target)) {
            return shm_type_area_result::incompatible_abi;
        }

        abi_properties properties;

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {
            return shm_type_area_result::incompatible_abi;
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
        if (result != shm_type_area_result::success) {
            output.reset();
            return result;
        }

        // Canonical unconnected<T> may require additional Runtime-only APIs.
        result = prepare_canonical_roots();
        if (result != shm_type_area_result::success) {
            output.reset();
            return result;
        }

        result = validate_object_references();
        if (result != shm_type_area_result::success) {
            output.reset();
            return result;
        }

        result = finalize_runtime_area();
        if (result != shm_type_area_result::success) {
            output.reset();
            return result;
        }

        output.target_value = abi.target;
        output.layout_size = layout.size();
        output.prepared_value = true;

        fill_telemetry();
        return shm_type_area_result::success;
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
        std::size_t repeats = 0;
    };

    [[nodiscard]] std::vector<cache_slot>&
    named_cache(type_area_mode mode) noexcept {
        return mode == type_area_mode::canonical
            ? canonical_named
            : default_named;
    }

    [[nodiscard]] std::vector<cache_slot>&
    derived_cache(type_area_mode mode) noexcept {
        return mode == type_area_mode::canonical
            ? canonical_derived
            : default_derived;
    }

    [[nodiscard]] area_begin begin_area() const noexcept {
        return {
            output.relative_references.size(),
            output.absolute_references.size(),
            output.object_references.size(),
            output.stores.size(),
            output.repeats.size(),
        };
    }

    [[nodiscard]] bool make_range(
        std::size_t begin,
        std::size_t end,
        shm_type_area::range& output_range) const noexcept {

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

    [[nodiscard]] shm_type_area_result finish_api(
        area_begin begin,
        std::uint32_t& output_api) {

        output_api = 0;

        shm_type_area::type_api api;

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
                begin.repeats,
                output.repeats.size(),
                api.repeats)) {
            return shm_type_area_result::overflow;
        }

        if (api.relative_references.count == 0 &&
            api.absolute_references.count == 0 &&
            api.object_references.count == 0 &&
            api.stores.count == 0 &&
            api.repeats.count == 0) {
            return shm_type_area_result::success;
        }

        if (output.type_apis.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_type_area_result::overflow;
        }

        output.type_apis.push_back(api);
        output_api =
            static_cast<std::uint32_t>(
                output.type_apis.size());

        return shm_type_area_result::success;
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

    [[nodiscard]] shm_type_area_result resolve_named(
        type_ref type,
        type_handle& output_handle) {

        output_handle = {};

        if (type.kind() != type_ref_kind::named ||
            type.payload() == 0 ||
            type.payload() > named_where.size()) {
            return shm_type_area_result::invalid_input;
        }

        auto& where =
            named_where[type.payload() - 1];

        if (where == 0) {
            const auto handle =
                project.type_location(type);

            if (!handle) {
                return shm_type_area_result::invalid_input;
            }

            where = handle.value();

            if (telemetry != nullptr) {
                ++telemetry->semantic_identity_resolutions;
            }
        }

        output_handle =
            project.type_at(where - 1);

        return output_handle
            ? shm_type_area_result::success
            : shm_type_area_result::invalid_input;
    }

    [[nodiscard]] shm_type_area_result resolve_named(
        identity_ref identity,
        type_handle& output_handle) {

        output_handle = {};

        if (!identity ||
            identity.slot() == 0 ||
            identity.slot() > named_where.size()) {
            return shm_type_area_result::invalid_input;
        }

        auto& where =
            named_where[identity.slot() - 1];

        if (where == 0) {
            const auto handle =
                project.type_location(identity);

            if (!handle) {
                return shm_type_area_result::invalid_input;
            }

            where = handle.value();

            if (telemetry != nullptr) {
                ++telemetry->semantic_identity_resolutions;
            }
        }

        output_handle =
            project.type_at(where - 1);

        return output_handle
            ? shm_type_area_result::success
            : shm_type_area_result::invalid_input;
    }

    [[nodiscard]] shm_type_area_result resolve_object(
        identity_ref identity,
        object_handle& output_object) {

        output_object = {};

        if (!identity ||
            identity.slot() == 0 ||
            identity.slot() >
                object_identity_to_slot.size()) {
            return shm_type_area_result::invalid_input;
        }

        auto& slot =
            object_identity_to_slot[
                identity.slot() - 1];

        if (slot == 0) {
            const auto object =
                project.object_location(identity);

            if (!object) {
                return shm_type_area_result::invalid_input;
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
            ? shm_type_area_result::success
            : shm_type_area_result::invalid_input;
    }

    [[nodiscard]] shm_type_area_result append_relative_reference(
        shm_record_offset target,
        shm_record_offset source) {

        output.relative_references.push_back({
            target,
            source,
        });

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result append_absolute_reference(
        shm_record_offset target,
        shm_offset source) {

        if (source >= layout.size()) {
            return shm_type_area_result::invalid_input;
        }

        output.absolute_references.push_back({
            target,
            0,
            source,
        });

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result append_object_reference(
        shm_record_offset target,
        std::uint32_t object_slot) {

        if (object_slot == 0 ||
            object_slot > project.object_slot_count()) {
            return shm_type_area_result::invalid_input;
        }

        output.object_references.push_back({
            target,
            object_slot,
        });

        return shm_type_area_result::success;
    }

    template <typename T>
    [[nodiscard]] shm_type_area_result append_store(
        shm_record_offset target,
        T value) {

        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 16);

        if (output.constants.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_type_area_result::overflow;
        }

        std::array<std::byte, 16> constant{};
        std::memcpy(
            constant.data(),
            &value,
            sizeof(T));

        output.constants.push_back(constant);

        output.stores.push_back({
            target,
            static_cast<std::uint32_t>(
                output.constants.size() - 1),
            static_cast<std::uint8_t>(sizeof(T)),
            {},
        });

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result append_repeat(
        shm_record_offset target,
        std::uint32_t type_api,
        shm_offset stride,
        std::uint64_t count) {

        if (type_api == 0) {
            if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }
            return shm_type_area_result::success;
        }

        if (type_api > output.type_apis.size() ||
            stride == 0 ||
            count == 0) {
            return shm_type_area_result::invalid_input;
        }

        output.repeats.push_back({
            target,
            type_api,
            stride,
            count,
        });

        return shm_type_area_result::success;
    }

    template <typename T>
    [[nodiscard]] shm_type_area_result append_integer(
        shm_record_offset target,
        construction_value construction) {

        T value{};

        if (!integer_value(construction, value)) {
            return shm_type_area_result::invalid_input;
        }

        return append_store(target, value);
    }

    template <typename T>
    [[nodiscard]] shm_type_area_result append_real(
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
            return shm_type_area_result::invalid_input;
        }

        return append_store(target, value);
    }

    [[nodiscard]] shm_type_area_result append_intrinsic(
        shm_record_offset target,
        intrinsic_type type,
        construction_value construction) {

        if (construction.kind == construction_kind::zero) {
            if (type == intrinsic_type::void_type) {
                return shm_type_area_result::unsupported_type;
            }

            if (type == intrinsic_type::none) {
                return shm_type_area_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }

            return shm_type_area_result::success;
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
                return shm_type_area_result::invalid_input;
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
                return shm_type_area_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }

            return shm_type_area_result::success;

        case intrinsic_type::void_type:
            return shm_type_area_result::unsupported_type;
        case intrinsic_type::none:
            return shm_type_area_result::invalid_input;
        }

        return shm_type_area_result::invalid_input;
    }

    [[nodiscard]] bool value_layout(
        type_ref type,
        shm_value_layout& value) {

        value = {};

        if (type.kind() == type_ref_kind::named) {
            type_handle handle;

            if (resolve_named(type, handle) !=
                shm_type_area_result::success) {
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

    [[nodiscard]] shm_type_area_result append_reference_zero(
        shm_record_offset target,
        type_ref referent) {

        shm_offset source = 0;

        if (!layout.unconnected_offset(
                referent,
                source)) {
            return shm_type_area_result::invalid_input;
        }

        return append_absolute_reference(
            target,
            source);
    }

    [[nodiscard]] shm_type_area_result append_member_reference(
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
                return shm_type_area_result::invalid_input;
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
                return shm_type_area_result::invalid_input;
            }

            type_ref referent;

            if (!reference_referent(
                    member.type,
                    referent)) {
                return shm_type_area_result::invalid_input;
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
                    return shm_type_area_result::invalid_input;
                }

                const auto source_local =
                    construction.operand - 1;

                if (source_local >=
                    record_type.members.count) {
                    return shm_type_area_result::invalid_input;
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
                    return shm_type_area_result::invalid_input;
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
                    shm_type_area_result::success) {
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
                return shm_type_area_result::invalid_input;
            }
        }

        return shm_type_area_result::invalid_input;
    }

    [[nodiscard]] shm_type_area_result copy_api(
        shm_record_offset target_base,
        std::uint32_t type_api) {

        if (type_api == 0) {
            if (telemetry != nullptr) {
                ++telemetry->zero_operations_elided;
            }
            return shm_type_area_result::success;
        }

        if (type_api > output.type_apis.size()) {
            return shm_type_area_result::invalid_input;
        }

        const auto api =
            output.type_apis[type_api - 1];

        if (telemetry != nullptr) {
            ++telemetry->flattened_api_copies;
        }

        for (std::uint32_t index = 0;
             index < api.relative_references.count;
             ++index) {

            const auto child =
                output.relative_references[
                    static_cast<std::size_t>(
                        api.relative_references.begin) +
                    index];

            shm_record_offset target = 0;
            shm_record_offset source = 0;

            if (!add_record_offset(
                    target_base,
                    child.target,
                    target) ||
                !add_record_offset(
                    target_base,
                    child.source,
                    source)) {
                return shm_type_area_result::overflow;
            }

            output.relative_references.push_back({
                target,
                source,
            });
        }

        for (std::uint32_t index = 0;
             index < api.absolute_references.count;
             ++index) {

            const auto child =
                output.absolute_references[
                    static_cast<std::size_t>(
                        api.absolute_references.begin) +
                    index];

            shm_record_offset target = 0;

            if (!add_record_offset(
                    target_base,
                    child.target,
                    target)) {
                return shm_type_area_result::overflow;
            }

            output.absolute_references.push_back({
                target,
                0,
                child.source,
            });
        }

        for (std::uint32_t index = 0;
             index < api.object_references.count;
             ++index) {

            const auto child =
                output.object_references[
                    static_cast<std::size_t>(
                        api.object_references.begin) +
                    index];

            shm_record_offset target = 0;

            if (!add_record_offset(
                    target_base,
                    child.target,
                    target)) {
                return shm_type_area_result::overflow;
            }

            output.object_references.push_back({
                target,
                child.object_slot,
            });
        }

        for (std::uint32_t index = 0;
             index < api.stores.count;
             ++index) {

            const auto child =
                output.stores[
                    static_cast<std::size_t>(
                        api.stores.begin) +
                    index];

            shm_record_offset target = 0;

            if (!add_record_offset(
                    target_base,
                    child.target,
                    target)) {
                return shm_type_area_result::overflow;
            }

            auto copy = child;
            copy.target = target;
            output.stores.push_back(copy);
        }

        for (std::uint32_t index = 0;
             index < api.repeats.count;
             ++index) {

            const auto child =
                output.repeats[
                    static_cast<std::size_t>(
                        api.repeats.begin) +
                    index];

            shm_record_offset target = 0;

            if (!add_record_offset(
                    target_base,
                    child.target,
                    target)) {
                return shm_type_area_result::overflow;
            }

            auto copy = child;
            copy.target = target;
            output.repeats.push_back(copy);
        }

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result prepare_value_dependency(
        type_ref type,
        construction_value construction,
        type_area_mode mode) {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return shm_type_area_result::success;

        case type_ref_kind::named: {
            type_handle handle;

            const auto resolved =
                resolve_named(type, handle);

            if (resolved !=
                shm_type_area_result::success) {
                return resolved;
            }

            type_entry entry;

            if (!project.type(handle, entry) ||
                !entry.valid_kind()) {
                return shm_type_area_result::invalid_input;
            }

            if (entry.kind ==
                graph_type_kind::intrinsic_alias) {
                return shm_type_area_result::success;
            }

            if (construction.kind !=
                construction_kind::zero) {
                return shm_type_area_result::invalid_input;
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
                return shm_type_area_result::invalid_input;
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
                    ? shm_type_area_result::success
                    : shm_type_area_result::invalid_input;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference:
                return construction.kind ==
                        construction_kind::zero
                    ? shm_type_area_result::success
                    : shm_type_area_result::invalid_input;

            case derived_type_kind::bounded_array: {
                if (construction.kind !=
                        construction_kind::zero ||
                    derived.payload == 0) {
                    return shm_type_area_result::invalid_input;
                }

                std::uint32_t ignored = 0;
                return ensure_zero_api(
                    derived.child,
                    mode,
                    ignored);
            }

            case derived_type_kind::unbounded_array:
                return shm_type_area_result::unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_type_area_result::invalid_input;
    }

    [[nodiscard]] shm_type_area_result ready_named(
        type_handle handle,
        type_area_mode mode,
        std::uint32_t& output_api) const noexcept {

        output_api = 0;

        if (!handle ||
            handle.value() > project.type_slot_count()) {
            return shm_type_area_result::invalid_input;
        }

        const auto& slot =
            mode == type_area_mode::canonical
            ? canonical_named[handle.value() - 1]
            : default_named[handle.value() - 1];

        if (slot.state != cache_state::ready) {
            return shm_type_area_result::invalid_input;
        }

        output_api = slot.type_api;
        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result ready_zero_api(
        type_ref type,
        type_area_mode mode,
        std::uint32_t& output_api) {

        output_api = 0;

        switch (type.kind()) {
        case type_ref_kind::intrinsic: {
            const auto intrinsic =
                static_cast<intrinsic_type>(
                    type.payload());

            if (intrinsic == intrinsic_type::void_type) {
                return shm_type_area_result::unsupported_type;
            }

            return intrinsic == intrinsic_type::none
                ? shm_type_area_result::invalid_input
                : shm_type_area_result::success;
        }

        case type_ref_kind::named: {
            type_handle handle;
            const auto resolved =
                resolve_named(type, handle);

            return resolved ==
                    shm_type_area_result::success
                ? ready_named(handle, mode, output_api)
                : resolved;
        }

        case type_ref_kind::derived: {
            if (type.payload() == 0 ||
                type.payload() > project.derived_type_count()) {
                return shm_type_area_result::invalid_input;
            }

            const auto& slot =
                mode == type_area_mode::canonical
                ? canonical_derived[type.payload() - 1]
                : default_derived[type.payload() - 1];

            if (slot.state != cache_state::ready) {
                return shm_type_area_result::invalid_input;
            }

            output_api = slot.type_api;
            return shm_type_area_result::success;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_type_area_result::invalid_input;
    }

    [[nodiscard]] shm_type_area_result append_value(
        shm_record_offset target,
        type_ref type,
        construction_value construction,
        type_area_mode mode) {

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
                shm_type_area_result::success) {
                return resolved;
            }

            type_entry entry;

            if (!project.type(handle, entry) ||
                !entry.valid_kind()) {
                return shm_type_area_result::invalid_input;
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
                return shm_type_area_result::invalid_input;
            }

            std::uint32_t child_api = 0;
            const auto ready =
                ready_named(
                    handle,
                    mode,
                    child_api);

            return ready ==
                    shm_type_area_result::success
                ? copy_api(target, child_api)
                : ready;
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(type, derived)) {
                return shm_type_area_result::invalid_input;
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
                    return shm_type_area_result::invalid_input;
                }

                if (telemetry != nullptr) {
                    ++telemetry->zero_operations_elided;
                }

                return shm_type_area_result::success;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference:
                if (construction.kind !=
                    construction_kind::zero) {
                    return shm_type_area_result::invalid_input;
                }

                return append_reference_zero(
                    target,
                    derived.child);

            case derived_type_kind::bounded_array: {
                if (construction.kind !=
                        construction_kind::zero ||
                    derived.payload == 0) {
                    return shm_type_area_result::invalid_input;
                }

                shm_value_layout child_layout;

                if (!value_layout(
                        derived.child,
                        child_layout) ||
                    child_layout.size == 0) {
                    return shm_type_area_result::invalid_input;
                }

                std::uint32_t child_api = 0;
                const auto ready =
                    ready_zero_api(
                        derived.child,
                        mode,
                        child_api);

                if (ready !=
                    shm_type_area_result::success) {
                    return ready;
                }

                return append_repeat(
                    target,
                    child_api,
                    child_layout.size,
                    derived.payload);
            }

            case derived_type_kind::unbounded_array:
                return shm_type_area_result::unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_type_area_result::invalid_input;
    }

    [[nodiscard]] shm_type_area_result ensure_zero_api(
        type_ref type,
        type_area_mode mode,
        std::uint32_t& output_api) {

        output_api = 0;

        switch (type.kind()) {
        case type_ref_kind::intrinsic: {
            const auto intrinsic =
                static_cast<intrinsic_type>(
                    type.payload());

            if (intrinsic == intrinsic_type::void_type) {
                return shm_type_area_result::unsupported_type;
            }

            return intrinsic == intrinsic_type::none
                ? shm_type_area_result::invalid_input
                : shm_type_area_result::success;
        }

        case type_ref_kind::named: {
            type_handle handle;
            const auto resolved =
                resolve_named(type, handle);

            return resolved ==
                    shm_type_area_result::success
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

        return shm_type_area_result::invalid_input;
    }

    [[nodiscard]] shm_type_area_result ensure_derived(
        type_ref type,
        type_area_mode mode,
        std::uint32_t& output_api) {

        output_api = 0;

        if (type.kind() != type_ref_kind::derived ||
            type.payload() == 0 ||
            type.payload() > project.derived_type_count()) {
            return shm_type_area_result::invalid_input;
        }

        auto& slot =
            derived_cache(mode)[type.payload() - 1];

        if (slot.state == cache_state::ready) {
            output_api = slot.type_api;
            return shm_type_area_result::success;
        }

        if (slot.state == cache_state::preparing) {
            return shm_type_area_result::invalid_input;
        }

        slot.state = cache_state::preparing;

        derived_type_record derived;

        if (!project.derived(type, derived)) {
            slot = {};
            return shm_type_area_result::invalid_input;
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
                shm_type_area_result::success) {
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
                shm_type_area_result::success) {
                slot = {};
                return appended;
            }

            const auto finished =
                finish_api(
                    begin,
                    result_api);

            if (finished !=
                shm_type_area_result::success) {
                slot = {};
                return finished;
            }
            break;
        }

        case derived_type_kind::bounded_array: {
            if (derived.payload == 0) {
                slot = {};
                return shm_type_area_result::invalid_input;
            }

            shm_value_layout child_layout;

            if (!value_layout(
                    derived.child,
                    child_layout) ||
                child_layout.size == 0) {
                slot = {};
                return shm_type_area_result::invalid_input;
            }

            std::uint32_t child_api = 0;
            const auto prepared =
                ensure_zero_api(
                    derived.child,
                    mode,
                    child_api);

            if (prepared !=
                shm_type_area_result::success) {
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
                    shm_type_area_result::success) {
                    slot = {};
                    return appended;
                }

                const auto finished =
                    finish_api(
                        begin,
                        result_api);

                if (finished !=
                    shm_type_area_result::success) {
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
            return shm_type_area_result::unsupported_type;
        }

        slot.type_api = result_api;
        slot.state = cache_state::ready;
        output_api = result_api;

        if (telemetry != nullptr) {
            ++telemetry->derived_types_prepared;
        }

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result ensure_named(
        type_handle handle,
        type_area_mode mode,
        std::uint32_t& output_api) {

        output_api = 0;

        if (!handle ||
            handle.value() > project.type_slot_count()) {
            return shm_type_area_result::invalid_input;
        }

        auto& slot =
            named_cache(mode)[handle.value() - 1];

        if (slot.state == cache_state::ready) {
            output_api = slot.type_api;
            return shm_type_area_result::success;
        }

        if (slot.state == cache_state::preparing) {
            return shm_type_area_result::invalid_input;
        }

        slot.state = cache_state::preparing;

        type_entry type;

        if (!project.type(handle, type) ||
            !type.defined() ||
            !type.valid_kind()) {
            slot = {};
            return shm_type_area_result::invalid_input;
        }

        if (type.kind ==
            graph_type_kind::intrinsic_alias) {
            slot.state = cache_state::ready;

            if (telemetry != nullptr) {
                ++telemetry->named_types_prepared;
            }

            return shm_type_area_result::success;
        }

        if (type.record_kind ==
            graph_record_kind::union_type) {
            slot = {};
            return shm_type_area_result::unsupported_type;
        }

        if (type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {
            slot = {};
            return shm_type_area_result::invalid_input;
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
                    ? shm_type_area_result::unsupported_type
                    : shm_type_area_result::invalid_input;
            }

            type_handle child;
            const auto resolved =
                resolve_named(
                    base.type,
                    child);

            if (resolved !=
                shm_type_area_result::success) {
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
                shm_type_area_result::success) {
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
                return shm_type_area_result::invalid_input;
            }

            type_ref referent;

            if (reference_referent(
                    member.type,
                    referent)) {
                continue;
            }

            construction_value construction{};

            if (mode == type_area_mode::defaults &&
                !project.construction_at(
                    global,
                    construction)) {
                slot = {};
                return shm_type_area_result::invalid_input;
            }

            const auto prepared =
                prepare_value_dependency(
                    member.type,
                    construction,
                    mode);

            if (prepared !=
                shm_type_area_result::success) {
                slot = {};
                return prepared;
            }
        }

        const auto begin = begin_area();

        // Emit flattened physical data. Nested record/base APIs are copied
        // into this Type API; no CALL survives into the hot executor.
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
                return shm_type_area_result::invalid_input;
            }

            type_handle child;
            const auto resolved =
                resolve_named(
                    base.type,
                    child);

            if (resolved !=
                shm_type_area_result::success) {
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
                shm_type_area_result::success) {
                slot = {};
                return ready;
            }

            const auto copied =
                copy_api(
                    offset,
                    child_api);

            if (copied !=
                shm_type_area_result::success) {
                slot = {};
                return copied;
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
                return shm_type_area_result::invalid_input;
            }

            type_ref referent;
            shm_type_area_result appended;

            if (reference_referent(
                    member.type,
                    referent)) {

                appended =
                    mode == type_area_mode::canonical
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

                if (mode == type_area_mode::defaults &&
                    !project.construction_at(
                        global,
                        construction)) {
                    slot = {};
                    return shm_type_area_result::invalid_input;
                }

                appended =
                    append_value(
                        offset,
                        member.type,
                        construction,
                        mode);
            }

            if (appended !=
                shm_type_area_result::success) {
                slot = {};
                return appended;
            }
        }

        std::uint32_t result_api = 0;
        const auto finished =
            finish_api(
                begin,
                result_api);

        if (finished !=
            shm_type_area_result::success) {
            slot = {};
            return finished;
        }

        slot.type_api = result_api;
        slot.state = cache_state::ready;
        output_api = result_api;

        if (telemetry != nullptr) {
            ++telemetry->named_types_prepared;
        }

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result prepare_object_patch(
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
                type_area_mode::defaults);

        if (appended !=
            shm_type_area_result::success) {
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
            return shm_type_area_result::invalid_input;
        }

        // A non-default null pointer/nullptr construction still writes no
        // physical bytes because the fresh SHM is already zeroed.
        if (output.stores.size() == stores_before) {
            return shm_type_area_result::success;
        }

        if (output.stores.size() != stores_before + 1) {
            return shm_type_area_result::invalid_input;
        }

        if (output.object_patches.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_type_area_result::overflow;
        }

        const auto patch = output.stores.back();
        output.stores.pop_back();
        output.object_patches.push_back(patch);

        output_patch =
            static_cast<std::uint32_t>(
                output.object_patches.size());

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result prepare_objects() {

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
                return shm_type_area_result::invalid_input;
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
                        type_area_mode::defaults,
                        type_api);

                if (prepared !=
                    shm_type_area_result::success) {
                    return prepared;
                }
            }
            else {
                const auto dependency =
                    prepare_value_dependency(
                        object.type,
                        construction,
                        type_area_mode::defaults);

                if (dependency !=
                    shm_type_area_result::success) {
                    return dependency;
                }

                const auto patched =
                    prepare_object_patch(
                        object.type,
                        construction,
                        patch);

                if (patched !=
                    shm_type_area_result::success) {
                    return patched;
                }
            }

            output.objects.push_back({
                offset,
                type_api,
                patch,
            });
        }

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result prepare_canonical_roots() {

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
                return shm_type_area_result::invalid_input;
            }

            std::uint32_t type_api = 0;
            const auto prepared =
                ensure_zero_api(
                    type,
                    type_area_mode::canonical,
                    type_api);

            if (prepared !=
                shm_type_area_result::success) {
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

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result
    validate_object_references() const noexcept {

        for (const auto& reference :
             output.object_references) {

            if (reference.object_slot == 0 ||
                reference.object_slot >
                    output.object_where.size() ||
                output.object_where[
                    reference.object_slot - 1] ==
                    invalid_where()) {
                return shm_type_area_result::invalid_input;
            }
        }

        return shm_type_area_result::success;
    }


    [[nodiscard]] bool valid_api_range(
        shm_type_area::range range,
        std::size_t size) const noexcept {

        return range.begin <= size &&
            range.count <= size - range.begin;
    }

    [[nodiscard]] shm_type_area_result
    finalize_runtime_area() {

        const auto old_type_apis =
            output.type_apis.size();
        const auto old_relative_references =
            output.relative_references.size();
        const auto old_absolute_references =
            output.absolute_references.size();
        const auto old_object_references =
            output.object_references.size();
        const auto old_stores =
            output.stores.size();
        const auto old_repeats =
            output.repeats.size();
        const auto old_constants =
            output.constants.size();
        const auto old_resident_bytes =
            output.resident_bytes();

        if (telemetry != nullptr) {
            telemetry->prefinal_type_apis =
                old_type_apis;
            telemetry->prefinal_relative_references =
                old_relative_references;
            telemetry->prefinal_absolute_references =
                old_absolute_references;
            telemetry->prefinal_object_references =
                old_object_references;
            telemetry->prefinal_stores =
                old_stores;
            telemetry->prefinal_repeats =
                old_repeats;
            telemetry->prefinal_constants =
                old_constants;
            telemetry->prefinal_resident_bytes =
                old_resident_bytes;
        }

        if (old_type_apis == 0) {
            if (!output.objects.empty() ||
                !output.canonical_roots.empty()) {

                for (const auto& object :
                     output.objects) {
                    if (object.type_api != 0) {
                        return shm_type_area_result::
                            invalid_input;
                    }
                }

                for (const auto& root :
                     output.canonical_roots) {
                    if (root.type_api != 0) {
                        return shm_type_area_result::
                            invalid_input;
                    }
                }
            }

            if (telemetry != nullptr) {
                telemetry->reachable_type_apis = 0;
                telemetry->reclaimed_bytes = 0;
            }

            return shm_type_area_result::success;
        }

        std::vector<std::uint8_t> reachable(
            old_type_apis,
            0);

        std::vector<std::uint32_t> stack;
        stack.reserve(old_type_apis);

        const auto mark =
            [&](std::uint32_t api) -> bool {

            if (api == 0) {
                return true;
            }

            if (api > old_type_apis) {
                return false;
            }

            auto& value =
                reachable[api - 1];

            if (value == 0) {
                value = 1;
                stack.push_back(api);
            }

            return true;
        };

        for (const auto& object :
             output.objects) {

            if (!mark(object.type_api)) {
                return shm_type_area_result::
                    invalid_input;
            }
        }

        for (const auto& root :
             output.canonical_roots) {

            if (!mark(root.type_api)) {
                return shm_type_area_result::
                    invalid_input;
            }
        }

        // Nested record/base composition is already flattened. Repeat is the
        // only final TypeApi -> TypeApi edge retained by this architecture.
        while (!stack.empty()) {
            const auto api_id =
                stack.back();
            stack.pop_back();

            const auto& api =
                output.type_apis[
                    api_id - 1];

            if (!valid_api_range(
                    api.relative_references,
                    old_relative_references) ||
                !valid_api_range(
                    api.absolute_references,
                    old_absolute_references) ||
                !valid_api_range(
                    api.object_references,
                    old_object_references) ||
                !valid_api_range(
                    api.stores,
                    old_stores) ||
                !valid_api_range(
                    api.repeats,
                    old_repeats)) {

                return shm_type_area_result::
                    invalid_input;
            }

            for (std::uint32_t index = 0;
                 index < api.repeats.count;
                 ++index) {

                const auto& repeat =
                    output.repeats[
                        static_cast<std::size_t>(
                            api.repeats.begin) +
                        index];

                if (repeat.type_api == 0 ||
                    !mark(repeat.type_api)) {

                    return shm_type_area_result::
                        invalid_input;
                }
            }
        }

        std::vector<std::uint32_t> remap(
            old_type_apis + 1,
            0);

        std::uint32_t reachable_count = 0;

        for (std::size_t index = 0;
             index < old_type_apis;
             ++index) {

            if (reachable[index] == 0) {
                continue;
            }

            if (reachable_count ==
                (std::numeric_limits<
                    std::uint32_t>::max)()) {

                return shm_type_area_result::
                    overflow;
            }

            ++reachable_count;
            remap[index + 1] =
                reachable_count;
        }

        std::size_t final_relative_count = 0;
        std::size_t final_absolute_count = 0;
        std::size_t final_object_count = 0;
        std::size_t final_store_count = 0;
        std::size_t final_repeat_count = 0;

        const auto add_count =
            [](
                std::size_t& target,
                std::uint32_t value) noexcept {

            if (value >
                (std::numeric_limits<
                    std::size_t>::max)() -
                    target) {

                return false;
            }

            target += value;
            return true;
        };

        for (std::size_t index = 0;
             index < old_type_apis;
             ++index) {

            if (reachable[index] == 0) {
                continue;
            }

            const auto& api =
                output.type_apis[index];

            if (!valid_api_range(
                    api.relative_references,
                    old_relative_references) ||
                !valid_api_range(
                    api.absolute_references,
                    old_absolute_references) ||
                !valid_api_range(
                    api.object_references,
                    old_object_references) ||
                !valid_api_range(
                    api.stores,
                    old_stores) ||
                !valid_api_range(
                    api.repeats,
                    old_repeats) ||
                !add_count(
                    final_relative_count,
                    api.relative_references.count) ||
                !add_count(
                    final_absolute_count,
                    api.absolute_references.count) ||
                !add_count(
                    final_object_count,
                    api.object_references.count) ||
                !add_count(
                    final_store_count,
                    api.stores.count) ||
                !add_count(
                    final_repeat_count,
                    api.repeats.count)) {

                return shm_type_area_result::
                    overflow;
            }
        }

        const auto max_u32 =
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)());

        if (final_relative_count > max_u32 ||
            final_absolute_count > max_u32 ||
            final_object_count > max_u32 ||
            final_store_count > max_u32 ||
            final_repeat_count > max_u32) {

            return shm_type_area_result::
                overflow;
        }

        std::vector<shm_type_area::type_api>
            final_type_apis;
        std::vector<shm_type_area::relative_reference>
            final_relative_references;
        std::vector<shm_type_area::absolute_reference>
            final_absolute_references;
        std::vector<shm_type_area::object_reference>
            final_object_references;
        std::vector<shm_type_area::store_operation>
            final_stores;
        std::vector<shm_type_area::repeat_operation>
            final_repeats;
        std::vector<std::array<std::byte, 16>>
            final_constants;

        final_type_apis.reserve(
            reachable_count);
        final_relative_references.reserve(
            final_relative_count);
        final_absolute_references.reserve(
            final_absolute_count);
        final_object_references.reserve(
            final_object_count);
        final_stores.reserve(
            final_store_count);
        final_repeats.reserve(
            final_repeat_count);
        final_constants.reserve(
            old_constants);

        const auto invalid_constant =
            (std::numeric_limits<
                std::uint32_t>::max)();

        std::vector<std::uint32_t>
            constant_remap(
                old_constants,
                invalid_constant);

        const auto remap_constant =
            [&](
                std::uint32_t old_constant,
                std::uint32_t& new_constant)
                -> bool {

            new_constant = 0;

            if (old_constant >=
                old_constants) {

                return false;
            }

            auto& mapped =
                constant_remap[
                    old_constant];

            if (mapped ==
                invalid_constant) {

                if (final_constants.size() >=
                    static_cast<std::size_t>(
                        invalid_constant)) {

                    return false;
                }

                mapped =
                    static_cast<std::uint32_t>(
                        final_constants.size());

                final_constants.push_back(
                    output.constants[
                        old_constant]);
            }

            new_constant = mapped;
            return true;
        };

        for (std::size_t old_index = 0;
             old_index < old_type_apis;
             ++old_index) {

            if (reachable[old_index] == 0) {
                continue;
            }

            const auto& old_api =
                output.type_apis[
                    old_index];

            shm_type_area::type_api new_api;

            const auto relative_begin =
                final_relative_references.size();

            for (std::uint32_t index = 0;
                 index <
                     old_api.relative_references.count;
                 ++index) {

                final_relative_references.push_back(
                    output.relative_references[
                        static_cast<std::size_t>(
                            old_api.relative_references.begin) +
                        index]);
            }

            if (!make_range(
                    relative_begin,
                    final_relative_references.size(),
                    new_api.relative_references)) {

                return shm_type_area_result::
                    overflow;
            }

            const auto absolute_begin =
                final_absolute_references.size();

            for (std::uint32_t index = 0;
                 index <
                     old_api.absolute_references.count;
                 ++index) {

                final_absolute_references.push_back(
                    output.absolute_references[
                        static_cast<std::size_t>(
                            old_api.absolute_references.begin) +
                        index]);
            }

            if (!make_range(
                    absolute_begin,
                    final_absolute_references.size(),
                    new_api.absolute_references)) {

                return shm_type_area_result::
                    overflow;
            }

            const auto object_begin =
                final_object_references.size();

            for (std::uint32_t index = 0;
                 index <
                     old_api.object_references.count;
                 ++index) {

                final_object_references.push_back(
                    output.object_references[
                        static_cast<std::size_t>(
                            old_api.object_references.begin) +
                        index]);
            }

            if (!make_range(
                    object_begin,
                    final_object_references.size(),
                    new_api.object_references)) {

                return shm_type_area_result::
                    overflow;
            }

            const auto store_begin =
                final_stores.size();

            for (std::uint32_t index = 0;
                 index <
                     old_api.stores.count;
                 ++index) {

                auto operation =
                    output.stores[
                        static_cast<std::size_t>(
                            old_api.stores.begin) +
                        index];

                std::uint32_t constant = 0;

                if (!remap_constant(
                        operation.constant,
                        constant)) {

                    return shm_type_area_result::
                        invalid_input;
                }

                operation.constant = constant;
                final_stores.push_back(
                    operation);
            }

            if (!make_range(
                    store_begin,
                    final_stores.size(),
                    new_api.stores)) {

                return shm_type_area_result::
                    overflow;
            }

            const auto repeat_begin =
                final_repeats.size();

            for (std::uint32_t index = 0;
                 index <
                     old_api.repeats.count;
                 ++index) {

                auto repeat =
                    output.repeats[
                        static_cast<std::size_t>(
                            old_api.repeats.begin) +
                        index];

                if (repeat.type_api == 0 ||
                    repeat.type_api >
                        old_type_apis) {

                    return shm_type_area_result::
                        invalid_input;
                }

                const auto mapped =
                    remap[
                        repeat.type_api];

                if (mapped == 0) {
                    return shm_type_area_result::
                        invalid_input;
                }

                repeat.type_api = mapped;
                final_repeats.push_back(
                    repeat);
            }

            if (!make_range(
                    repeat_begin,
                    final_repeats.size(),
                    new_api.repeats)) {

                return shm_type_area_result::
                    overflow;
            }

            if (remap[old_index + 1] !=
                final_type_apis.size() + 1) {

                return shm_type_area_result::
                    invalid_input;
            }

            final_type_apis.push_back(
                new_api);
        }

        auto final_objects =
            output.objects;

        for (auto& object :
             final_objects) {

            if (object.type_api == 0) {
                continue;
            }

            if (object.type_api >
                    old_type_apis ||
                remap[object.type_api] == 0) {

                return shm_type_area_result::
                    invalid_input;
            }

            object.type_api =
                remap[object.type_api];
        }

        auto final_canonical_roots =
            output.canonical_roots;

        for (auto& root :
             final_canonical_roots) {

            if (root.type_api == 0 ||
                root.type_api >
                    old_type_apis ||
                remap[root.type_api] == 0) {

                return shm_type_area_result::
                    invalid_input;
            }

            root.type_api =
                remap[root.type_api];
        }

        auto final_object_patches =
            output.object_patches;

        for (auto& patch :
             final_object_patches) {

            std::uint32_t constant = 0;

            if (!remap_constant(
                    patch.constant,
                    constant)) {

                return shm_type_area_result::
                    invalid_input;
            }

            patch.constant = constant;
        }

        output.type_apis =
            std::move(final_type_apis);
        output.relative_references =
            std::move(final_relative_references);
        output.absolute_references =
            std::move(final_absolute_references);
        output.object_references =
            std::move(final_object_references);
        output.stores =
            std::move(final_stores);
        output.repeats =
            std::move(final_repeats);
        output.constants =
            std::move(final_constants);
        output.objects =
            std::move(final_objects);
        output.canonical_roots =
            std::move(final_canonical_roots);
        output.object_patches =
            std::move(final_object_patches);

        if (telemetry != nullptr) {
            telemetry->reachable_type_apis =
                output.type_apis.size();
            telemetry->discarded_type_apis =
                old_type_apis -
                output.type_apis.size();
            telemetry->discarded_relative_references =
                old_relative_references -
                output.relative_references.size();
            telemetry->discarded_absolute_references =
                old_absolute_references -
                output.absolute_references.size();
            telemetry->discarded_object_references =
                old_object_references -
                output.object_references.size();
            telemetry->discarded_stores =
                old_stores -
                output.stores.size();
            telemetry->discarded_repeats =
                old_repeats -
                output.repeats.size();
            telemetry->discarded_constants =
                old_constants -
                output.constants.size();

            const auto final_resident_bytes =
                output.resident_bytes();

            telemetry->reclaimed_bytes =
                old_resident_bytes >=
                    final_resident_bytes
                ? old_resident_bytes -
                    final_resident_bytes
                : 0;
        }

        return shm_type_area_result::success;
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
        telemetry->repeats = output.repeats.size();

        telemetry->type_api_bytes =
            output.type_apis.size() *
            sizeof(shm_type_area::type_api);
        telemetry->relative_reference_bytes =
            output.relative_references.size() *
            sizeof(shm_type_area::relative_reference);
        telemetry->absolute_reference_bytes =
            output.absolute_references.size() *
            sizeof(shm_type_area::absolute_reference);
        telemetry->object_reference_bytes =
            output.object_references.size() *
            sizeof(shm_type_area::object_reference);
        telemetry->store_bytes =
            output.stores.size() *
            sizeof(shm_type_area::store_operation);
        telemetry->repeat_bytes =
            output.repeats.size() *
            sizeof(shm_type_area::repeat_operation);
        telemetry->constant_bytes =
            output.constants.size() *
            sizeof(output.constants.front());
        telemetry->object_where_bytes =
            output.object_where.size() *
            sizeof(shm_offset);
        telemetry->object_runtime_bytes =
            output.objects.size() *
            sizeof(shm_type_area::object_runtime);
        telemetry->canonical_root_bytes =
            output.canonical_roots.size() *
            sizeof(shm_type_area::canonical_root);
        telemetry->object_patch_bytes =
            output.object_patches.size() *
            sizeof(shm_type_area::store_operation);
        telemetry->resident_bytes =
            output.resident_bytes();
    }

    const compiled_project_view& project;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    shm_type_area& output;
    shm_type_area_prepare_telemetry* telemetry = nullptr;

    // Temporary semantic-WHO -> Graph-WHERE accelerators. They exist only
    // during prepare. The final Type Area retains no identity_ref values.
    std::vector<std::uint32_t> named_where;
    std::vector<std::uint32_t> object_identity_to_slot;

    std::vector<cache_slot> canonical_named;
    std::vector<cache_slot> default_named;
    std::vector<cache_slot> canonical_derived;
    std::vector<cache_slot> default_derived;
};

class shm_type_area_executor final {
public:
    shm_type_area_executor(
        const shm_type_area& area,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        std::span<std::byte> shm,
        shm_type_area_execute_telemetry* telemetry) noexcept
        : area(area),
          abi(abi),
          layout(layout),
          shm(shm),
          telemetry(telemetry) {
    }

    [[nodiscard]] shm_type_area_result canonical() noexcept {
        const auto valid = validate();
        if (valid != shm_type_area_result::success) {
            return valid;
        }

        for (const auto& root : area.canonical_roots) {
            const auto result =
                apply_api(
                    root.type_api,
                    root.offset);

            if (result != shm_type_area_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->canonical_roots;
            }
        }

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result objects() noexcept {
        const auto valid = validate();
        if (valid != shm_type_area_result::success) {
            return valid;
        }

        for (const auto& object : area.objects) {
            if (object.type_api != 0) {
                const auto result =
                    apply_api(
                        object.type_api,
                        object.offset);

                if (result != shm_type_area_result::success) {
                    return result;
                }
            }

            if (object.patch != 0) {
                if (object.patch >
                    area.object_patches.size()) {
                    return shm_type_area_result::invalid_input;
                }

                const auto result =
                    execute_store(
                        area.object_patches[
                            object.patch - 1],
                        object.offset);

                if (result != shm_type_area_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->object_patch_writes;
                }
            }

            if (telemetry != nullptr) {
                ++telemetry->objects;
            }
        }

        return shm_type_area_result::success;
    }

private:
    [[nodiscard]] shm_type_area_result validate() noexcept {

        if (!area.prepared_value ||
            area.target_value != abi.target ||
            layout.target() != abi.target ||
            area.layout_size != layout.size() ||
            area.layout_size > shm.size() ||
            (area.layout_size != 0 &&
             shm.data() == nullptr) ||
            !host_compatible(abi.target)) {
            return shm_type_area_result::incompatible_abi;
        }

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {
            return shm_type_area_result::incompatible_abi;
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
            return shm_type_area_result::overflow;
        }

        return shm_type_area_result::success;
    }

    [[nodiscard]] bool valid_range(
        shm_type_area::range range,
        std::size_t size) const noexcept {

        return range.begin <= size &&
            range.count <= size - range.begin;
    }

    [[nodiscard]] shm_type_area_result write_reference(
        std::byte* target,
        const std::byte* source) noexcept {

        if (target == nullptr ||
            source == nullptr) {
            return shm_type_area_result::invalid_input;
        }

        const auto value =
            reinterpret_cast<std::uintptr_t>(
                source);

        if (properties.reference_size == 4) {
            if (value >
                (std::numeric_limits<
                    std::uint32_t>::max)()) {
                return shm_type_area_result::overflow;
            }

            const auto narrowed =
                static_cast<std::uint32_t>(value);

            std::memcpy(
                target,
                &narrowed,
                sizeof(narrowed));

            return shm_type_area_result::success;
        }

        if (properties.reference_size == 8) {
            const auto widened =
                static_cast<std::uint64_t>(value);

            std::memcpy(
                target,
                &widened,
                sizeof(widened));

            return shm_type_area_result::success;
        }

        return shm_type_area_result::incompatible_abi;
    }

    [[nodiscard]] shm_type_area_result execute_store(
        const shm_type_area::store_operation& operation,
        shm_offset base_offset) noexcept {

        if (operation.size == 0 ||
            operation.size > 16 ||
            operation.constant >= area.constants.size() ||
            base_offset >= area.layout_size ||
            operation.target >
                area.layout_size - base_offset ||
            static_cast<shm_offset>(operation.size) >
                area.layout_size - base_offset -
                    operation.target) {
            return shm_type_area_result::invalid_input;
        }

        std::memcpy(
            shm.data() +
                static_cast<std::size_t>(
                    base_offset + operation.target),
            area.constants[operation.constant].data(),
            operation.size);

        if (telemetry != nullptr) {
            ++telemetry->store_writes;
        }

        return shm_type_area_result::success;
    }

    [[nodiscard]] shm_type_area_result apply_api(
        std::uint32_t type_api,
        shm_offset base_offset) noexcept {

        if (type_api == 0 ||
            type_api > area.type_apis.size() ||
            base_offset >= area.layout_size) {
            return shm_type_area_result::invalid_input;
        }

        const auto api =
            area.type_apis[type_api - 1];

        if (!valid_range(
                api.relative_references,
                area.relative_references.size()) ||
            !valid_range(
                api.absolute_references,
                area.absolute_references.size()) ||
            !valid_range(
                api.object_references,
                area.object_references.size()) ||
            !valid_range(
                api.stores,
                area.stores.size()) ||
            !valid_range(
                api.repeats,
                area.repeats.size())) {
            return shm_type_area_result::invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->api_applications;
        }

        auto* const base =
            shm.data() +
            static_cast<std::size_t>(base_offset);

        for (std::uint32_t index = 0;
             index < api.relative_references.count;
             ++index) {

            const auto& reference =
                area.relative_references[
                    static_cast<std::size_t>(
                        api.relative_references.begin) +
                    index];

            if (reference.target >=
                    area.layout_size - base_offset ||
                reference.source >=
                    area.layout_size - base_offset) {
                return shm_type_area_result::invalid_input;
            }

            const auto result =
                write_reference(
                    base + reference.target,
                    base + reference.source);

            if (result != shm_type_area_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->relative_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.absolute_references.count;
             ++index) {

            const auto& reference =
                area.absolute_references[
                    static_cast<std::size_t>(
                        api.absolute_references.begin) +
                    index];

            if (reference.target >=
                    area.layout_size - base_offset ||
                reference.source >=
                    area.layout_size) {
                return shm_type_area_result::invalid_input;
            }

            const auto result =
                write_reference(
                    base + reference.target,
                    shm.data() +
                        static_cast<std::size_t>(
                            reference.source));

            if (result != shm_type_area_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->absolute_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.object_references.count;
             ++index) {

            const auto& reference =
                area.object_references[
                    static_cast<std::size_t>(
                        api.object_references.begin) +
                    index];

            if (reference.target >=
                    area.layout_size - base_offset ||
                reference.object_slot == 0 ||
                reference.object_slot >
                    area.object_where.size()) {
                return shm_type_area_result::invalid_input;
            }

            const auto source_offset =
                area.object_where[
                    reference.object_slot - 1];

            if (source_offset == invalid_where() ||
                source_offset >= area.layout_size) {
                return shm_type_area_result::invalid_input;
            }

            const auto result =
                write_reference(
                    base + reference.target,
                    shm.data() +
                        static_cast<std::size_t>(
                            source_offset));

            if (result != shm_type_area_result::success) {
                return result;
            }

            if (telemetry != nullptr) {
                ++telemetry->reference_writes;
                ++telemetry->object_reference_writes;
            }
        }

        for (std::uint32_t index = 0;
             index < api.stores.count;
             ++index) {

            const auto result =
                execute_store(
                    area.stores[
                        static_cast<std::size_t>(
                            api.stores.begin) +
                        index],
                    base_offset);

            if (result != shm_type_area_result::success) {
                return result;
            }
        }

        for (std::uint32_t index = 0;
             index < api.repeats.count;
             ++index) {

            const auto& repeat =
                area.repeats[
                    static_cast<std::size_t>(
                        api.repeats.begin) +
                    index];

            if (repeat.type_api == 0 ||
                repeat.type_api > area.type_apis.size() ||
                repeat.stride == 0 ||
                repeat.count == 0 ||
                repeat.target >=
                    area.layout_size - base_offset) {
                return shm_type_area_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->repeat_visits;
                telemetry->repeat_iterations +=
                    repeat.count;
            }

            shm_offset current =
                base_offset + repeat.target;

            for (std::uint64_t iteration = 0;
                 iteration < repeat.count;
                 ++iteration) {

                if (current >= area.layout_size) {
                    return shm_type_area_result::invalid_input;
                }

                const auto result =
                    apply_api(
                        repeat.type_api,
                        current);

                if (result != shm_type_area_result::success) {
                    return result;
                }

                if (iteration + 1 != repeat.count) {
                    if (repeat.stride >
                        area.layout_size - current) {
                        return shm_type_area_result::overflow;
                    }

                    current += repeat.stride;
                }
            }
        }

        return shm_type_area_result::success;
    }

    const shm_type_area& area;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    std::span<std::byte> shm;
    shm_type_area_execute_telemetry* telemetry = nullptr;
    abi_properties properties{};
};

std::size_t shm_type_area::resident_bytes() const noexcept {

    return
        type_apis.size() * sizeof(type_api) +
        relative_references.size() * sizeof(relative_reference) +
        absolute_references.size() * sizeof(absolute_reference) +
        object_references.size() * sizeof(object_reference) +
        stores.size() * sizeof(store_operation) +
        repeats.size() * sizeof(repeat_operation) +
        constants.size() * sizeof(constants.front()) +
        object_where.size() * sizeof(shm_offset) +
        objects.size() * sizeof(object_runtime) +
        canonical_roots.size() * sizeof(canonical_root) +
        object_patches.size() * sizeof(store_operation);
}

void shm_type_area::reset() noexcept {

    type_apis.clear();
    relative_references.clear();
    absolute_references.clear();
    object_references.clear();
    stores.clear();
    repeats.clear();
    constants.clear();
    object_where.clear();
    objects.clear();
    canonical_roots.clear();
    object_patches.clear();

    target_value = abi_target::windows_x64;
    layout_size = 0;
    prepared_value = false;
}

shm_type_area_result prepare_shm_type_area(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_type_area& output,
    shm_type_area_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_type_area_builder builder{
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
        return shm_type_area_result::failed;
    }
}

shm_type_area_result materialize_shm_type_area_canonical(
    const shm_type_area& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_area_execute_telemetry* telemetry) noexcept {

    shm_type_area_executor executor{
        area,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.canonical();
}

shm_type_area_result materialize_shm_type_area_objects(
    const shm_type_area& area,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_type_area_execute_telemetry* telemetry) noexcept {

    shm_type_area_executor executor{
        area,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.objects();
}

}
