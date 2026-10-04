#include "shm_sparse.hpp"

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

    if (construction.kind ==
        construction_kind::zero) {
        return true;
    }

    return
        (construction.kind ==
             construction_kind::signed_integer ||
         construction.kind ==
             construction_kind::unsigned_integer) &&
        construction.bits() == 0;
}

enum class sparse_mode : std::uint8_t {
    canonical = 0,
    defaults,
};

enum class cache_state : std::uint8_t {
    empty = 0,
    preparing,
    ready,
};

}

class shm_sparse_builder final {
public:
    shm_sparse_builder(
        const compiled_project_view& project,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        shm_sparse_types& output,
        shm_sparse_prepare_telemetry* telemetry)
        : project(project),
          abi(abi),
          layout(layout),
          output(output),
          telemetry(telemetry) {
    }

    [[nodiscard]] shm_sparse_result build() {

        output.reset();

        if (!project.valid() ||
            layout.target() != abi.target ||
            !host_compatible(abi.target)) {
            return shm_sparse_result::incompatible_abi;
        }

        abi_properties properties;

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {
            return shm_sparse_result::incompatible_abi;
        }

        canonical_named.resize(project.type_slot_count());
        default_named.resize(project.type_slot_count());
        canonical_derived.resize(project.derived_type_count());
        default_derived.resize(project.derived_type_count());

        auto result = prepare_canonical_roots();
        if (result != shm_sparse_result::success) {
            output.reset();
            return result;
        }

        result = prepare_object_roots();
        if (result != shm_sparse_result::success) {
            output.reset();
            return result;
        }

        output.target_value = abi.target;
        output.layout_size = layout.size();
        output.prepared_value = true;

        if (telemetry != nullptr) {
            telemetry->actions = output.actions.size();
            telemetry->canonical_roots =
                output.canonical_roots.size();
            telemetry->object_roots =
                output.object_roots.size();

            telemetry->action_size =
                sizeof(shm_sparse_types::action);
            telemetry->hot_action_bytes =
                output.actions.size() *
                sizeof(shm_sparse_types::action);
            telemetry->absolute_offsets =
                output.absolute_offsets.size();
            telemetry->absolute_offset_bytes =
                output.absolute_offsets.size() *
                sizeof(shm_offset);
            telemetry->constants =
                output.constants.size();
            telemetry->constant_bytes =
                output.constants.size() *
                sizeof(output.constants.front());
            telemetry->repeat_descriptors =
                output.repeat_descriptors.size();
            telemetry->repeat_descriptor_bytes =
                output.repeat_descriptors.size() *
                sizeof(shm_sparse_types::repeat_descriptor);
            telemetry->root_bytes =
                (output.canonical_roots.size() +
                 output.object_roots.size()) *
                sizeof(shm_sparse_types::root);
            telemetry->resident_bytes =
                output.resident_bytes();
        }

        return shm_sparse_result::success;
    }

private:
    struct cache_slot final {
        shm_sparse_types::program_range program{};
        cache_state state = cache_state::empty;
    };

    [[nodiscard]] std::vector<cache_slot>&
    named_cache(sparse_mode mode) noexcept {
        return mode == sparse_mode::canonical
            ? canonical_named
            : default_named;
    }

    [[nodiscard]] std::vector<cache_slot>&
    derived_cache(sparse_mode mode) noexcept {
        return mode == sparse_mode::canonical
            ? canonical_derived
            : default_derived;
    }

    void mark_program(
        shm_sparse_types::program_range program,
        bool named) noexcept {

        if (telemetry == nullptr) {
            return;
        }

        if (program.empty()) {
            ++telemetry->empty_programs;
        }
        else if (named) {
            ++telemetry->named_programs;
        }
        else {
            ++telemetry->derived_programs;
        }
    }

    [[nodiscard]] shm_sparse_result append_action(
        shm_sparse_types::action action) {

        if (output.actions.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_sparse_result::overflow;
        }

        output.actions.push_back(
            std::move(action));

        if (telemetry != nullptr) {
            switch (output.actions.back().kind) {
            case shm_sparse_types::action_kind::
                reference_relative:
                ++telemetry->reference_relative_actions;
                break;
            case shm_sparse_types::action_kind::
                reference_absolute:
                ++telemetry->reference_absolute_actions;
                break;
            case shm_sparse_types::action_kind::store:
                ++telemetry->store_actions;
                break;
            case shm_sparse_types::action_kind::call:
                ++telemetry->call_actions;
                break;
            case shm_sparse_types::action_kind::repeat:
                ++telemetry->repeat_actions;
                break;
            }
        }

        return shm_sparse_result::success;
    }

    [[nodiscard]] shm_sparse_result append_call(
        shm_record_offset target,
        shm_sparse_types::program_range child) {

        if (child.empty()) {
            if (telemetry != nullptr) {
                ++telemetry->zero_actions_elided;
            }
            return shm_sparse_result::success;
        }

        shm_sparse_types::action action;
        action.target = target;
        action.arg0 = child.begin;
        action.arg1 = child.count;
        action.kind = shm_sparse_types::action_kind::call;

        return append_action(std::move(action));
    }

    [[nodiscard]] shm_sparse_result append_repeat(
        shm_record_offset target,
        shm_sparse_types::program_range child,
        std::uint64_t count,
        shm_offset stride) {

        if (child.empty()) {
            if (telemetry != nullptr) {
                ++telemetry->zero_actions_elided;
            }
            return shm_sparse_result::success;
        }

        if (count == 0 || stride == 0) {
            return shm_sparse_result::invalid_input;
        }

        if (output.repeat_descriptors.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_sparse_result::overflow;
        }

        const auto payload =
            static_cast<std::uint32_t>(
                output.repeat_descriptors.size());

        output.repeat_descriptors.push_back({
            child,
            stride,
            count,
        });

        shm_sparse_types::action action;
        action.target = target;
        action.arg0 = payload;
        action.kind = shm_sparse_types::action_kind::repeat;

        return append_action(std::move(action));
    }

    [[nodiscard]] shm_sparse_result
    append_reference_relative(
        shm_record_offset target,
        shm_record_offset source) {

        shm_sparse_types::action action;
        action.target = target;
        action.kind =
            shm_sparse_types::action_kind::
                reference_relative;
        action.arg0 = source;

        return append_action(std::move(action));
    }

    [[nodiscard]] shm_sparse_result
    append_reference_absolute(
        shm_record_offset target,
        shm_offset source) {

        if (source >= layout.size()) {
            return shm_sparse_result::invalid_input;
        }

        if (output.absolute_offsets.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_sparse_result::overflow;
        }

        const auto payload =
            static_cast<std::uint32_t>(
                output.absolute_offsets.size());

        output.absolute_offsets.push_back(source);

        shm_sparse_types::action action;
        action.target = target;
        action.arg0 = payload;
        action.kind =
            shm_sparse_types::action_kind::
                reference_absolute;

        return append_action(std::move(action));
    }

    template <typename T>
    [[nodiscard]] shm_sparse_result append_store(
        shm_record_offset target,
        T value) {

        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 16);

        if (output.constants.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            return shm_sparse_result::overflow;
        }

        const auto payload =
            static_cast<std::uint32_t>(
                output.constants.size());

        std::array<std::byte, 16> constant{};

        std::memcpy(
            constant.data(),
            &value,
            sizeof(T));

        output.constants.push_back(constant);

        shm_sparse_types::action action;
        action.target = target;
        action.arg0 = payload;
        action.kind = shm_sparse_types::action_kind::store;
        action.aux =
            static_cast<std::uint8_t>(sizeof(T));

        return append_action(std::move(action));
    }

    template <typename T>
    [[nodiscard]] shm_sparse_result append_integer(
        shm_record_offset target,
        construction_value construction) {

        T value{};

        if (!integer_value(construction, value)) {
            return shm_sparse_result::invalid_input;
        }

        return append_store(target, value);
    }

    template <typename T>
    [[nodiscard]] shm_sparse_result append_real(
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
            return shm_sparse_result::invalid_input;
        }

        return append_store(target, value);
    }

    [[nodiscard]] shm_sparse_result append_intrinsic(
        shm_record_offset target,
        intrinsic_type type,
        construction_value construction) {

        if (construction.kind ==
            construction_kind::zero) {

            if (type == intrinsic_type::void_type) {
                return shm_sparse_result::unsupported_type;
            }

            if (type == intrinsic_type::none) {
                return shm_sparse_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->zero_actions_elided;
            }

            return shm_sparse_result::success;
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
                return shm_sparse_result::invalid_input;
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
            return zero_pointer_construction(construction)
                ? shm_sparse_result::success
                : shm_sparse_result::invalid_input;

        case intrinsic_type::void_type:
            return shm_sparse_result::unsupported_type;
        case intrinsic_type::none:
            return shm_sparse_result::invalid_input;
        }

        return shm_sparse_result::invalid_input;
    }

    [[nodiscard]] bool value_layout(
        type_ref type,
        shm_value_layout& value) const noexcept {

        value = {};

        if (type.kind() == type_ref_kind::named) {
            const auto handle =
                project.type_location(type);

            return handle &&
                layout.type(handle, value);
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

    [[nodiscard]] shm_sparse_result
    prepare_value_dependency(
        type_ref type,
        construction_value construction,
        sparse_mode mode) {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return shm_sparse_result::success;

        case type_ref_kind::named: {
            const auto handle =
                project.type_location(type);

            type_entry entry;

            if (!handle ||
                !project.type(handle, entry) ||
                !entry.valid_kind()) {
                return shm_sparse_result::invalid_input;
            }

            if (entry.kind ==
                graph_type_kind::intrinsic_alias) {
                return shm_sparse_result::success;
            }

            if (construction.kind !=
                construction_kind::zero) {
                return shm_sparse_result::invalid_input;
            }

            shm_sparse_types::program_range ignored;
            return prepare_named(handle, mode, ignored);
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(type, derived)) {
                return shm_sparse_result::invalid_input;
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
                    ? shm_sparse_result::success
                    : shm_sparse_result::invalid_input;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference:
                return construction.kind ==
                        construction_kind::zero
                    ? shm_sparse_result::success
                    : shm_sparse_result::invalid_input;

            case derived_type_kind::bounded_array: {
                if (construction.kind !=
                        construction_kind::zero ||
                    derived.payload == 0) {
                    return shm_sparse_result::invalid_input;
                }

                shm_sparse_types::program_range ignored;
                return prepare_zero_program(
                    derived.child,
                    mode,
                    ignored);
            }

            case derived_type_kind::unbounded_array:
                return shm_sparse_result::unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_sparse_result::invalid_input;
    }

    [[nodiscard]] shm_sparse_result append_value(
        shm_record_offset target,
        type_ref type,
        construction_value construction,
        sparse_mode mode) {

        switch (type.kind()) {
        case type_ref_kind::intrinsic:
            return append_intrinsic(
                target,
                static_cast<intrinsic_type>(
                    type.payload()),
                construction);

        case type_ref_kind::named: {
            const auto handle =
                project.type_location(type);

            type_entry entry;

            if (!handle ||
                !project.type(handle, entry) ||
                !entry.valid_kind()) {
                return shm_sparse_result::invalid_input;
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
                return shm_sparse_result::invalid_input;
            }

            shm_sparse_types::program_range child;
            const auto prepared =
                prepare_named(handle, mode, child);

            if (prepared != shm_sparse_result::success) {
                return prepared;
            }

            return append_call(target, child);
        }

        case type_ref_kind::derived: {
            derived_type_record derived;

            if (!project.derived(type, derived)) {
                return shm_sparse_result::invalid_input;
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
                    return shm_sparse_result::invalid_input;
                }

                if (telemetry != nullptr) {
                    ++telemetry->zero_actions_elided;
                }

                return shm_sparse_result::success;

            case derived_type_kind::lvalue_reference:
            case derived_type_kind::rvalue_reference: {
                if (construction.kind !=
                    construction_kind::zero) {
                    return shm_sparse_result::invalid_input;
                }

                shm_offset source = 0;

                if (!layout.unconnected_offset(
                        derived.child,
                        source)) {
                    return shm_sparse_result::invalid_input;
                }

                return append_reference_absolute(
                    target,
                    source);
            }

            case derived_type_kind::bounded_array: {
                if (construction.kind !=
                        construction_kind::zero ||
                    derived.payload == 0) {
                    return shm_sparse_result::invalid_input;
                }

                shm_value_layout child_layout;

                if (!value_layout(
                        derived.child,
                        child_layout) ||
                    child_layout.size == 0) {
                    return shm_sparse_result::invalid_input;
                }

                shm_sparse_types::program_range child;
                const auto prepared =
                    prepare_zero_program(
                        derived.child,
                        mode,
                        child);

                if (prepared != shm_sparse_result::success) {
                    return prepared;
                }

                return append_repeat(
                    target,
                    child,
                    derived.payload,
                    child_layout.size);
            }

            case derived_type_kind::unbounded_array:
                return shm_sparse_result::unsupported_type;
            }

            break;
        }

        case type_ref_kind::invalid:
            break;
        }

        return shm_sparse_result::invalid_input;
    }

    [[nodiscard]] shm_sparse_result
    prepare_zero_program(
        type_ref type,
        sparse_mode mode,
        shm_sparse_types::program_range& output_program) {

        output_program = {};

        switch (type.kind()) {
        case type_ref_kind::intrinsic: {
            const auto intrinsic =
                static_cast<intrinsic_type>(
                    type.payload());

            if (intrinsic == intrinsic_type::void_type) {
                return shm_sparse_result::unsupported_type;
            }

            return intrinsic == intrinsic_type::none
                ? shm_sparse_result::invalid_input
                : shm_sparse_result::success;
        }

        case type_ref_kind::named: {
            const auto handle =
                project.type_location(type);

            return handle
                ? prepare_named(handle, mode, output_program)
                : shm_sparse_result::invalid_input;
        }

        case type_ref_kind::derived:
            return prepare_derived(
                type,
                mode,
                output_program);

        case type_ref_kind::invalid:
            break;
        }

        return shm_sparse_result::invalid_input;
    }

    [[nodiscard]] shm_sparse_result prepare_derived(
        type_ref type,
        sparse_mode mode,
        shm_sparse_types::program_range& output_program) {

        output_program = {};

        if (type.kind() != type_ref_kind::derived ||
            type.payload() == 0 ||
            type.payload() > project.derived_type_count()) {
            return shm_sparse_result::invalid_input;
        }

        auto& slot =
            derived_cache(mode)[type.payload() - 1];

        if (slot.state == cache_state::ready) {
            output_program = slot.program;
            return shm_sparse_result::success;
        }

        if (slot.state == cache_state::preparing) {
            return shm_sparse_result::invalid_input;
        }

        slot.state = cache_state::preparing;

        derived_type_record derived;

        if (!project.derived(type, derived)) {
            slot = {};
            return shm_sparse_result::invalid_input;
        }

        shm_sparse_types::program_range result;

        switch (derived.kind) {
        case derived_type_kind::const_qualified:
        case derived_type_kind::volatile_qualified: {
            const auto prepared =
                prepare_zero_program(
                    derived.child,
                    mode,
                    result);

            if (prepared != shm_sparse_result::success) {
                slot = {};
                return prepared;
            }
            break;
        }

        case derived_type_kind::pointer:
            break;

        case derived_type_kind::lvalue_reference:
        case derived_type_kind::rvalue_reference: {
            shm_offset source = 0;

            if (!layout.unconnected_offset(
                    derived.child,
                    source)) {
                slot = {};
                return shm_sparse_result::invalid_input;
            }

            const auto begin = output.actions.size();
            const auto appended =
                append_reference_absolute(0, source);

            if (appended != shm_sparse_result::success) {
                slot = {};
                return appended;
            }

            result.begin =
                static_cast<std::uint32_t>(begin);
            result.count = 1;
            break;
        }

        case derived_type_kind::bounded_array: {
            if (derived.payload == 0) {
                slot = {};
                return shm_sparse_result::invalid_input;
            }

            shm_value_layout child_layout;

            if (!value_layout(
                    derived.child,
                    child_layout) ||
                child_layout.size == 0) {
                slot = {};
                return shm_sparse_result::invalid_input;
            }

            shm_sparse_types::program_range child;
            const auto prepared =
                prepare_zero_program(
                    derived.child,
                    mode,
                    child);

            if (prepared != shm_sparse_result::success) {
                slot = {};
                return prepared;
            }

            if (!child.empty()) {
                const auto begin = output.actions.size();
                const auto appended =
                    append_repeat(
                        0,
                        child,
                        derived.payload,
                        child_layout.size);

                if (appended != shm_sparse_result::success) {
                    slot = {};
                    return appended;
                }

                result.begin =
                    static_cast<std::uint32_t>(begin);
                result.count = 1;
            }
            else if (telemetry != nullptr) {
                ++telemetry->zero_actions_elided;
            }

            break;
        }

        case derived_type_kind::unbounded_array:
            slot = {};
            return shm_sparse_result::unsupported_type;
        }

        slot.program = result;
        slot.state = cache_state::ready;
        mark_program(result, false);
        output_program = result;

        return shm_sparse_result::success;
    }

    [[nodiscard]] shm_sparse_result prepare_named(
        type_handle handle,
        sparse_mode mode,
        shm_sparse_types::program_range& output_program) {

        output_program = {};

        if (!handle ||
            handle.value() > project.type_slot_count()) {
            return shm_sparse_result::invalid_input;
        }

        auto& slot =
            named_cache(mode)[handle.value() - 1];

        if (slot.state == cache_state::ready) {
            output_program = slot.program;
            return shm_sparse_result::success;
        }

        if (slot.state == cache_state::preparing) {
            return shm_sparse_result::invalid_input;
        }

        slot.state = cache_state::preparing;

        type_entry type;

        if (!project.type(handle, type) ||
            !type.defined() ||
            !type.valid_kind()) {
            slot = {};
            return shm_sparse_result::invalid_input;
        }

        if (type.kind ==
            graph_type_kind::intrinsic_alias) {
            slot.state = cache_state::ready;
            mark_program({}, true);
            return shm_sparse_result::success;
        }

        if (type.record_kind ==
            graph_record_kind::union_type) {
            slot = {};
            return shm_sparse_result::unsupported_type;
        }

        if (type.record_kind !=
                graph_record_kind::struct_type &&
            type.record_kind !=
                graph_record_kind::class_type) {
            slot = {};
            return shm_sparse_result::invalid_input;
        }

        // Pass 1: prepare dependencies. Parent actions are emitted only after
        // all child ranges are finalized, keeping every program contiguous.
        for (std::uint32_t local = 0;
             local < type.bases.count;
             ++local) {

            const auto global =
                static_cast<std::size_t>(
                    type.bases.begin) +
                local;

            base_record base;

            if (!project.base_at(global, base)) {
                slot = {};
                return shm_sparse_result::invalid_input;
            }

            if (base.virtual_base()) {
                slot = {};
                return shm_sparse_result::unsupported_type;
            }

            const auto child =
                project.type_location(base.type);

            shm_sparse_types::program_range ignored;

            if (!child) {
                slot = {};
                return shm_sparse_result::invalid_input;
            }

            const auto prepared =
                prepare_named(child, mode, ignored);

            if (prepared != shm_sparse_result::success) {
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
                return shm_sparse_result::invalid_input;
            }

            type_ref referent;

            if (reference_referent(
                    member.type,
                    referent)) {
                continue;
            }

            construction_value construction{};

            if (mode == sparse_mode::defaults &&
                !project.construction_at(
                    global,
                    construction)) {
                slot = {};
                return shm_sparse_result::invalid_input;
            }

            const auto prepared =
                prepare_value_dependency(
                    member.type,
                    construction,
                    mode);

            if (prepared != shm_sparse_result::success) {
                slot = {};
                return prepared;
            }
        }

        const auto begin = output.actions.size();

        // Pass 2: emit only non-zero physical work.
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
                return shm_sparse_result::invalid_input;
            }

            const auto child =
                project.type_location(base.type);

            shm_sparse_types::program_range child_program;

            if (!child) {
                slot = {};
                return shm_sparse_result::invalid_input;
            }

            const auto prepared =
                prepare_named(
                    child,
                    mode,
                    child_program);

            if (prepared != shm_sparse_result::success) {
                slot = {};
                return prepared;
            }

            const auto appended =
                append_call(offset, child_program);

            if (appended != shm_sparse_result::success) {
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
                return shm_sparse_result::invalid_input;
            }

            construction_value construction{};

            if (mode == sparse_mode::defaults &&
                !project.construction_at(
                    global,
                    construction)) {
                slot = {};
                return shm_sparse_result::invalid_input;
            }

            type_ref referent;
            shm_sparse_result appended;

            if (reference_referent(
                    member.type,
                    referent)) {

                appended =
                    mode == sparse_mode::canonical
                    ? append_reference_zero(
                        offset,
                        referent)
                    : append_member_reference(
                        type,
                        local,
                        offset);
            }
            else {
                appended =
                    append_value(
                        offset,
                        member.type,
                        construction,
                        mode);
            }

            if (appended != shm_sparse_result::success) {
                slot = {};
                return appended;
            }
        }

        const auto end = output.actions.size();

        if (end - begin >
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {
            slot = {};
            return shm_sparse_result::overflow;
        }

        shm_sparse_types::program_range result{
            static_cast<std::uint32_t>(begin),
            static_cast<std::uint32_t>(end - begin),
        };

        slot.program = result;
        slot.state = cache_state::ready;
        mark_program(result, true);
        output_program = result;

        return shm_sparse_result::success;
    }

    [[nodiscard]] shm_sparse_result append_reference_zero(
        shm_record_offset target,
        type_ref referent) {

        shm_offset source = 0;

        if (!layout.unconnected_offset(
                referent,
                source)) {
            return shm_sparse_result::invalid_input;
        }

        return append_reference_absolute(
            target,
            source);
    }

    [[nodiscard]] shm_sparse_result append_member_reference(
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
                return shm_sparse_result::invalid_input;
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
                return shm_sparse_result::invalid_input;
            }

            type_ref referent;

            if (!reference_referent(
                    member.type,
                    referent)) {
                return shm_sparse_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->
                    reference_chain_steps_resolved;
            }

            switch (construction.kind) {
            case construction_kind::zero:
                return append_reference_zero(
                    target_offset,
                    referent);

            case construction_kind::member_binding: {
                if (construction.operand == 0) {
                    return shm_sparse_result::invalid_input;
                }

                const auto source_local =
                    construction.operand - 1;

                if (source_local >=
                    record_type.members.count) {
                    return shm_sparse_result::invalid_input;
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
                    return shm_sparse_result::invalid_input;
                }

                type_ref source_referent;

                if (!reference_referent(
                        source_member.type,
                        source_referent)) {
                    return append_reference_relative(
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

                const auto object =
                    project.object_location(identity);

                shm_offset source = 0;

                if (!object ||
                    !layout.object_offset(
                        object,
                        source)) {
                    return shm_sparse_result::invalid_input;
                }

                return append_reference_absolute(
                    target_offset,
                    source);
            }

            case construction_kind::signed_integer:
            case construction_kind::unsigned_integer:
            case construction_kind::real:
            case construction_kind::unsupported:
                return shm_sparse_result::invalid_input;
            }
        }

        return shm_sparse_result::invalid_input;
    }

    [[nodiscard]] shm_sparse_result prepare_canonical_roots() {

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
                return shm_sparse_result::invalid_input;
            }

            shm_sparse_types::program_range program;

            const auto prepared =
                prepare_zero_program(
                    type,
                    sparse_mode::canonical,
                    program);

            if (prepared != shm_sparse_result::success) {
                return prepared;
            }

            if (!program.empty()) {
                output.canonical_roots.push_back({
                    offset,
                    program,
                });
            }
        }

        return shm_sparse_result::success;
    }

    [[nodiscard]] shm_sparse_result prepare_object_roots() {

        for (std::size_t index = 0;
             index < project.object_slot_count();
             ++index) {

            const auto handle =
                project.object_at(index);

            // Stable WHERE may contain dead slots after sparse BUILD.
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
                    offset)) {
                return shm_sparse_result::invalid_input;
            }

            shm_sparse_types::program_range program;

            if (construction.kind ==
                construction_kind::zero) {

                const auto prepared =
                    prepare_zero_program(
                        object.type,
                        sparse_mode::defaults,
                        program);

                if (prepared != shm_sparse_result::success) {
                    return prepared;
                }
            }
            else {
                const auto dependency =
                    prepare_value_dependency(
                        object.type,
                        construction,
                        sparse_mode::defaults);

                if (dependency != shm_sparse_result::success) {
                    return dependency;
                }

                const auto begin = output.actions.size();

                const auto appended =
                    append_value(
                        0,
                        object.type,
                        construction,
                        sparse_mode::defaults);

                if (appended != shm_sparse_result::success) {
                    return appended;
                }

                const auto count =
                    output.actions.size() -
                    begin;

                if (count >
                    static_cast<std::size_t>(
                        (std::numeric_limits<
                            std::uint32_t>::max)())) {
                    return shm_sparse_result::overflow;
                }

                program = {
                    static_cast<std::uint32_t>(begin),
                    static_cast<std::uint32_t>(count),
                };
            }

            if (!program.empty()) {
                output.object_roots.push_back({
                    offset,
                    program,
                });
            }
        }

        return shm_sparse_result::success;
    }

    const compiled_project_view& project;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    shm_sparse_types& output;
    shm_sparse_prepare_telemetry* telemetry = nullptr;

    std::vector<cache_slot> canonical_named;
    std::vector<cache_slot> default_named;
    std::vector<cache_slot> canonical_derived;
    std::vector<cache_slot> default_derived;
};

class shm_sparse_executor final {
public:
    shm_sparse_executor(
        const shm_sparse_types& sparse,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        std::span<std::byte> shm,
        shm_sparse_execute_telemetry* telemetry) noexcept
        : sparse(sparse),
          abi(abi),
          layout(layout),
          shm(shm),
          telemetry(telemetry) {
    }

    [[nodiscard]] shm_sparse_result canonical() noexcept {
        return execute_roots(sparse.canonical_roots);
    }

    [[nodiscard]] shm_sparse_result objects() noexcept {
        return execute_roots(sparse.object_roots);
    }

private:
    [[nodiscard]] shm_sparse_result validate() noexcept {

        if (!sparse.prepared_value ||
            sparse.target_value != abi.target ||
            sparse.layout_size != layout.size() ||
            layout.target() != abi.target ||
            !host_compatible(abi.target) ||
            layout.size() > shm.size() ||
            (layout.size() != 0 &&
             shm.data() == nullptr)) {
            return shm_sparse_result::incompatible_abi;
        }

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {
            return shm_sparse_result::incompatible_abi;
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
            return shm_sparse_result::overflow;
        }

        return shm_sparse_result::success;
    }

    [[nodiscard]] shm_sparse_result execute_roots(
        const std::vector<
            shm_sparse_types::root>& roots) noexcept {

        const auto valid = validate();

        if (valid != shm_sparse_result::success) {
            return valid;
        }

        for (const auto& root : roots) {
            if (root.offset >= sparse.layout_size) {
                return shm_sparse_result::invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->root_executions;
            }

            const auto result =
                execute_program(
                    root.program,
                    shm.data() +
                        static_cast<std::size_t>(
                            root.offset));

            if (result != shm_sparse_result::success) {
                return result;
            }
        }

        return shm_sparse_result::success;
    }

    [[nodiscard]] shm_sparse_result execute_program(
        shm_sparse_types::program_range program,
        std::byte* base) noexcept {

        const auto begin =
            static_cast<std::size_t>(
                program.begin);

        const auto count =
            static_cast<std::size_t>(
                program.count);

        if (begin > sparse.actions.size() ||
            count > sparse.actions.size() - begin) {
            return shm_sparse_result::invalid_input;
        }

        for (std::size_t index = 0;
             index < count;
             ++index) {

            const auto& action =
                sparse.actions[begin + index];

            if (telemetry != nullptr) {
                ++telemetry->action_visits;
            }

            auto* target =
                base +
                static_cast<std::size_t>(
                    action.target);

            switch (action.kind) {
            case shm_sparse_types::action_kind::
                reference_relative: {

                const auto native =
                    reinterpret_cast<std::uintptr_t>(
                        base +
                        static_cast<std::size_t>(
                            action.arg0));

                const auto result =
                    write_reference(
                        target,
                        static_cast<std::uint64_t>(
                            native));

                if (result != shm_sparse_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->reference_writes;
                }

                break;
            }

            case shm_sparse_types::action_kind::
                reference_absolute: {

                if (action.arg0 >=
                    sparse.absolute_offsets.size()) {
                    return shm_sparse_result::invalid_input;
                }

                const auto source =
                    sparse.absolute_offsets[
                        action.arg0];

                if (source >= sparse.layout_size) {
                    return shm_sparse_result::invalid_input;
                }

                const auto native =
                    reinterpret_cast<std::uintptr_t>(
                        shm.data() +
                        static_cast<std::size_t>(
                            source));

                const auto result =
                    write_reference(
                        target,
                        static_cast<std::uint64_t>(
                            native));

                if (result != shm_sparse_result::success) {
                    return result;
                }

                if (telemetry != nullptr) {
                    ++telemetry->reference_writes;
                }

                break;
            }

            case shm_sparse_types::action_kind::store:
                if (action.arg0 >=
                        sparse.constants.size() ||
                    action.aux == 0 ||
                    action.aux >
                        sparse.constants[
                            action.arg0].size()) {
                    return shm_sparse_result::invalid_input;
                }

                std::memcpy(
                    target,
                    sparse.constants[
                        action.arg0].data(),
                    action.aux);

                if (telemetry != nullptr) {
                    ++telemetry->store_writes;
                }

                break;

            case shm_sparse_types::action_kind::call: {
                const shm_sparse_types::program_range child{
                    action.arg0,
                    action.arg1,
                };

                if (child.empty()) {
                    return shm_sparse_result::invalid_input;
                }

                if (telemetry != nullptr) {
                    ++telemetry->call_visits;
                }

                const auto result =
                    execute_program(child, target);

                if (result != shm_sparse_result::success) {
                    return result;
                }

                break;
            }

            case shm_sparse_types::action_kind::repeat: {
                if (action.arg0 >=
                    sparse.repeat_descriptors.size()) {
                    return shm_sparse_result::invalid_input;
                }

                const auto& repeat =
                    sparse.repeat_descriptors[
                        action.arg0];

                if (repeat.child.empty() ||
                    repeat.stride == 0 ||
                    repeat.count == 0) {
                    return shm_sparse_result::invalid_input;
                }

                if (telemetry != nullptr) {
                    ++telemetry->repeat_visits;
                    telemetry->repeat_iterations +=
                        repeat.count;
                }

                auto* current = target;

                for (std::uint64_t iteration = 0;
                     iteration < repeat.count;
                     ++iteration) {

                    const auto result =
                        execute_program(
                            repeat.child,
                            current);

                    if (result != shm_sparse_result::success) {
                        return result;
                    }

                    current +=
                        static_cast<std::size_t>(
                            repeat.stride);
                }

                break;
            }
            }
        }

        return shm_sparse_result::success;
    }

    [[nodiscard]] shm_sparse_result write_reference(
        std::byte* target,
        std::uint64_t value) noexcept {

        if (properties.reference_size == 4) {
            if (value >
                (std::numeric_limits<
                    std::uint32_t>::max)()) {
                return shm_sparse_result::overflow;
            }

            const auto narrowed =
                static_cast<std::uint32_t>(value);

            std::memcpy(
                target,
                &narrowed,
                sizeof(narrowed));

            return shm_sparse_result::success;
        }

        if (properties.reference_size == 8) {
            std::memcpy(
                target,
                &value,
                sizeof(value));

            return shm_sparse_result::success;
        }

        return shm_sparse_result::incompatible_abi;
    }

    const shm_sparse_types& sparse;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    std::span<std::byte> shm;
    shm_sparse_execute_telemetry* telemetry = nullptr;
    abi_properties properties{};
};

std::size_t shm_sparse_types::resident_bytes() const noexcept {

    return
        actions.size() * sizeof(action) +
        absolute_offsets.size() * sizeof(shm_offset) +
        constants.size() * sizeof(constants.front()) +
        repeat_descriptors.size() *
            sizeof(repeat_descriptor) +
        canonical_roots.size() * sizeof(root) +
        object_roots.size() * sizeof(root);
}

void shm_sparse_types::reset() noexcept {

    actions.clear();
    absolute_offsets.clear();
    constants.clear();
    repeat_descriptors.clear();
    canonical_roots.clear();
    object_roots.clear();

    target_value = abi_target::windows_x64;
    layout_size = 0;
    prepared_value = false;
}

shm_sparse_result prepare_shm_sparse_types(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_sparse_types& output,
    shm_sparse_prepare_telemetry* telemetry) noexcept {

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        shm_sparse_builder builder{
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
        return shm_sparse_result::failed;
    }
}

shm_sparse_result materialize_shm_sparse_canonical(
    const shm_sparse_types& sparse,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_sparse_execute_telemetry* telemetry) noexcept {

    shm_sparse_executor executor{
        sparse,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.canonical();
}

shm_sparse_result materialize_shm_sparse_objects(
    const shm_sparse_types& sparse,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_sparse_execute_telemetry* telemetry) noexcept {

    shm_sparse_executor executor{
        sparse,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.objects();
}

}
