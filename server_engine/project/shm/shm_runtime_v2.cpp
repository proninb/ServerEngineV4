#include "../graph/construction_semantics.hpp"
#include "../persistence/byte_patch.hpp"
#include "shm_runtime_v2.hpp"

#include "../abi/abi_layout.hpp"
#include "../construction/execution_lanes.hpp"
#include "../persistence/compiled_project.hpp"
#include "../persistence/runtime_project.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace cw::server {

class shm_runtime_v2_link_builder final {
public:
    shm_runtime_v2_link_builder(
        const compiled_project_view& project,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        shm_runtime_v2& output,
        shm_runtime_v2_physical_plan_telemetry*
            telemetry = nullptr) noexcept
        : project(project),
          abi(abi),
          layout(layout),
          output(output),
          telemetry(telemetry) {
    }

    [[nodiscard]] shm_runtime_v2_result build() {

        if (telemetry != nullptr) {
            *telemetry = {};
        }

        reset_execution();

        const auto compatible =
            validate_target();

        if (compatible !=
            shm_runtime_v2_result::success) {

            return compatible;
        }

        const auto links =
            build_links_phase();

        if (links !=
            shm_runtime_v2_result::success) {

            return links;
        }

        const auto initializations =
            build_initializations_phase();

        if (initializations !=
            shm_runtime_v2_result::success) {

            return initializations;
        }

        if (telemetry != nullptr) {
            telemetry->total_dereferences =
                static_cast<std::uint64_t>(
                    output.endpoint_dereferences.size());

            telemetry->
                initialization_dereferences =
                telemetry->total_dereferences >=
                        telemetry->link_dereferences
                    ? telemetry->total_dereferences -
                        telemetry->link_dereferences
                    : 0;

            telemetry->parallel_lanes = 1;
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    build_links_only() {

        reset_execution();

        const auto compatible =
            validate_target();

        if (compatible !=
            shm_runtime_v2_result::success) {

            return compatible;
        }

        return build_links_phase();
    }

    [[nodiscard]] shm_runtime_v2_result
    build_initializations_only() {

        reset_execution();

        const auto compatible =
            validate_target();

        if (compatible !=
            shm_runtime_v2_result::success) {

            return compatible;
        }

        const auto result =
            build_initializations_phase();

        if (result ==
                shm_runtime_v2_result::success &&
            telemetry != nullptr) {

            telemetry->
                initialization_dereferences =
                static_cast<std::uint64_t>(
                    output.endpoint_dereferences.size());
        }

        return result;
    }

    [[nodiscard]] static
    shm_runtime_v2_result merge_parallel(
        shm_runtime_v2&& links,
        shm_runtime_v2&& initializations,
        shm_runtime_v2& output,
        shm_runtime_v2_physical_plan_telemetry*
            telemetry) {

        output.compact_link_offsets = links.compact_link_offsets && initializations.compact_link_offsets;
        const auto link_dereferences =
            links.endpoint_dereferences.size();

        const auto initialization_dereferences =
            initializations.
                endpoint_dereferences.size();

        if (link_dereferences >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) ||
            initialization_dereferences >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)()) -
                    link_dereferences) {

            return shm_runtime_v2_result::
                overflow;
        }

        const auto rebase =
            static_cast<std::uint32_t>(
                link_dereferences);

        if (rebase != 0) {
            for (auto& initialization :
                 initializations.initializations) {

                if (initialization.target.
                        dereference_count == 0) {

                    continue;
                }

                if (initialization.target.
                        dereference_begin >
                    (std::numeric_limits<
                        std::uint32_t>::max)() -
                        rebase) {

                    return shm_runtime_v2_result::
                        overflow;
                }

                initialization.target.
                    dereference_begin +=
                        rebase;
            }
        }

        output.links =
            std::move(
                links.links);

        output.initializations =
            std::move(
                initializations.
                    initializations);
        output.initialization_bytes = std::move(initializations.initialization_bytes);

        output.live_link_count_value =
            links.live_link_count_value;

        output.link_dereference_count_value =
            link_dereferences;

        if (link_dereferences == 0) {
            output.endpoint_dereferences =
                std::move(
                    initializations.
                        endpoint_dereferences);
        }
        else if (initialization_dereferences == 0) {
            output.endpoint_dereferences =
                std::move(
                    links.endpoint_dereferences);
        }
        else {
            output.endpoint_dereferences =
                std::move(
                    links.endpoint_dereferences);

            output.endpoint_dereferences.reserve(
                link_dereferences +
                initialization_dereferences);

            output.endpoint_dereferences.insert(
                output.endpoint_dereferences.end(),
                initializations.
                    endpoint_dereferences.begin(),
                initializations.
                    endpoint_dereferences.end());
        }

        if (telemetry != nullptr) {
            telemetry->link_dereferences =
                static_cast<std::uint64_t>(
                    link_dereferences);

            telemetry->
                initialization_dereferences =
                static_cast<std::uint64_t>(
                    initialization_dereferences);

            telemetry->total_dereferences =
                static_cast<std::uint64_t>(
                    output.endpoint_dereferences.size());

            telemetry->parallel_lanes = 2;
        }

        return shm_runtime_v2_result::
            success;
    }

private:
    // Bounded per-preparation caches contain only immutable Project/ABI results.
    // Recipes retain WHERE-independent tails; each caller gets its own root and
    // dereference span so persistence sizing and link order stay unchanged.
    struct root_cache_entry { identity_ref id{}; object_entry value{}; shm_offset offset = 0; };
    struct recipe_cache_entry {
        type_ref root_type{};
        endpoint_ref member{};
        type_ref value_type{};
        bool pointer_target = false;
        shm_runtime_v2::endpoint_program program{};
    };
    std::array<root_cache_entry, 16> root_cache{};
    std::array<recipe_cache_entry, 1024> recipe_cache{};

    void reset_execution() noexcept {
        root_cache.fill({});
        recipe_cache.fill({});
        output.links.clear();
        output.initializations.clear();
        output.initialization_bytes.clear();
        output.endpoint_dereferences.clear();
        output.live_link_count_value = 0;
        output.link_dereference_count_value = 0;
    }

    [[nodiscard]] shm_runtime_v2_result
    validate_target() const noexcept {
        output.compact_link_offsets = layout.size() < shm_runtime_v2_compact_limit;

        if (!host_compatible(
                abi.target) ||
            layout.target() !=
                abi.target) {

            return shm_runtime_v2_result::
                incompatible_abi;
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    build_links_phase() {

        output.links.resize(
            project.link_count());

        using profile_clock =
            std::chrono::steady_clock;

        if (telemetry != nullptr) {
            telemetry->link_slots =
                static_cast<std::uint64_t>(
                    project.link_count());
        }

        const auto links_started =
            telemetry != nullptr
            ? profile_clock::now()
            : profile_clock::time_point{};

        std::size_t virtual_links = 0;
        const bool dense = project.live_link_count() == project.link_count();
        for (std::size_t index = 0; index < project.link_count(); ++index) {
            link_record link;
            if (dense) {
                const auto handle = project.link_from_raw(static_cast<std::uint32_t>(index + 1));
                if (!project.link_raw(handle, link)) return shm_runtime_v2_result::invalid_input;
            } else {
                const auto handle = project.link_at(index);
                if (!handle) continue;
                if (handle.value() != index + 1 || !project.link(handle, link))
                    return shm_runtime_v2_result::invalid_input;
            }

            std::uint8_t bit = 0;
            if (project.bit_endpoint(link.source.member, bit)) {
                ++virtual_links;
                continue;
            }

            auto& plan =
                output.links[index];

            const auto source =
                compile_endpoint(
                    link.source,
                    plan.source);

            if (source !=
                shm_runtime_v2_result::
                    success) {

                return source;
            }

            const auto target =
                compile_endpoint(
                    link.target,
                    plan.target, nullptr, true);

            if (target !=
                    shm_runtime_v2_result::
                        success ||
                !plan.target.final_reference) {

                return target ==
                        shm_runtime_v2_result::
                            success
                    ? shm_runtime_v2_result::
                        invalid_input
                    : target;
            }

            plan.live = true;
            plan.state =
                shm_runtime_v2::
                    link_state::prepared;

            ++output.live_link_count_value;
        }

        if (telemetry != nullptr) {
            telemetry->links_ns =
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                        profile_clock::now() -
                        links_started)
                        .count());

            telemetry->live_links =
                static_cast<std::uint64_t>(
                    output.live_link_count_value);
        }

        if (output.live_link_count_value + virtual_links !=
            project.live_link_count()) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        output.link_dereference_count_value =
            output.endpoint_dereferences.size();

        if (telemetry != nullptr) {
            telemetry->link_dereferences =
                static_cast<std::uint64_t>(
                    output.
                        link_dereference_count_value);
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    build_initializations_phase() {

        output.initializations.reserve(
            project.initialization_count());

        using profile_clock =
            std::chrono::steady_clock;

        const auto initializations_started =
            telemetry != nullptr
            ? profile_clock::now()
            : profile_clock::time_point{};

        for (std::size_t index = 0;
             index <
                 project.initialization_count();
             ++index) {

            object_initialization_record
                initialization;

            if (!project.initialization_at(
                    index,
                    initialization)) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            shm_runtime_v2::
                initialization_plan plan;

            type_ref target_type;

            const auto target =
                compile_endpoint(
                    initialization.target,
                    plan.target,
                    &target_type);

            if (target !=
                    shm_runtime_v2_result::
                        success ||
                plan.target.final_reference) {

                return target ==
                        shm_runtime_v2_result::
                            success
                    ? shm_runtime_v2_result::
                        invalid_input
                    : target;
            }

            if (!valid_byte_patch_target(project, initialization.target, initialization.value))
                return shm_runtime_v2_result::invalid_input;
            plan.reserved[1] = plan.target.reserved[0];
            const auto encoded =
                encode_initialization(
                    target_type,
                    initialization.value,
                    plan);

            if (encoded !=
                shm_runtime_v2_result::
                    success) {

                return encoded;
            }

            output.initializations.push_back(
                plan);
        }

        if (telemetry != nullptr) {
            telemetry->initializations_ns =
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                        profile_clock::now() -
                        initializations_started)
                        .count());

            telemetry->initializations =
                static_cast<std::uint64_t>(
                    output.initializations.size());
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] static bool host_compatible(
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
    [[nodiscard]] bool add_offset(
        shm_offset& target,
        shm_offset value) const noexcept {

        if (value >
            (std::numeric_limits<
                shm_offset>::max)() -
                target) {

            return false;
        }

        target += value;
        return true;
    }

    void strip_cv(
        type_ref& type) const noexcept {

        derived_type_record derived;

        while (project.derived(
                   type,
                   derived) &&
               (derived.kind ==
                    derived_type_kind::
                        const_qualified ||
                derived.kind ==
                    derived_type_kind::
                        volatile_qualified)) {

            type =
                derived.child;
        }
    }

    [[nodiscard]] bool reference_referent(
        type_ref type,
        type_ref& output_type, bool pointer = false) const noexcept {

        output_type = {};

        derived_type_record derived;
        if (pointer) strip_cv(type);

        if (!project.derived(
                type,
                derived) ||
            (derived.kind !=
                 derived_type_kind::
                     lvalue_reference &&
             derived.kind !=
                 derived_type_kind::
                     rvalue_reference &&
             !(pointer && derived.kind == derived_type_kind::pointer))) {

            return false;
        }

        output_type =
            derived.child;

        return static_cast<bool>(
            output_type);
    }

    [[nodiscard]] bool record_type(
        type_ref type,
        type_handle& output_type) const noexcept {

        output_type = {};

        strip_cv(type);

        return
            type.kind() ==
                type_ref_kind::named &&
            project.named(
                type,
                output_type);
    }

    [[nodiscard]] bool value_layout(
        type_ref type,
        shm_value_layout& value) const noexcept {

        value = {};

        if (type.kind() ==
            type_ref_kind::named) {

            type_handle handle;

            return
                project.named(
                    type,
                    handle) &&
                layout.type(
                    handle,
                    value);
        }

        return layout.value(
            type,
            value);
    }

    [[nodiscard]] shm_runtime_v2_result
    append_dereference(
        shm_runtime_v2::endpoint_program& program,
        shm_offset& pending_static) {

        if (output.endpoint_dereferences.size() >=
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {

            return shm_runtime_v2_result::
                overflow;
        }

        output.endpoint_dereferences.push_back(
            pending_static);

        pending_static = 0;
        ++program.dereference_count;

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    append_member(
        type_ref& current_type,
        std::uint64_t local_value,
        shm_offset& pending_static) {

        type_handle record;

        if (!record_type(
                current_type,
                record)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        type_entry type;

        if (!project.type(
                record,
                type) ||
            local_value >=
                type.members.count) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        const auto global =
            static_cast<std::size_t>(
                type.members.begin) +
            static_cast<std::size_t>(
                local_value);

        member_record member;
        shm_record_offset offset = 0;

        if (!project.member_at(
                global,
                member) ||
            !layout.member_offset(
                global,
                offset)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        if (!add_offset(
                pending_static,
                static_cast<shm_offset>(
                    offset))) {

            return shm_runtime_v2_result::
                overflow;
        }

        current_type =
            member.type;

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    append_base(
        type_ref& current_type,
        std::uint64_t local_value,
        shm_offset& pending_static) {

        type_handle record;

        if (!record_type(
                current_type,
                record)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        type_entry type;

        if (!project.type(
                record,
                type) ||
            local_value >=
                type.bases.count) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        const auto global =
            static_cast<std::size_t>(
                type.bases.begin) +
            static_cast<std::size_t>(
                local_value);

        base_record base;
        shm_record_offset offset = 0;

        if (!project.base_at(
                global,
                base) ||
            base.virtual_base() ||
            !layout.base_offset(
                global,
                offset)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        if (!add_offset(
                pending_static,
                static_cast<shm_offset>(
                    offset))) {

            return shm_runtime_v2_result::
                overflow;
        }

        const auto base_type =
            project.find_type(
                base.type);

        if (!base_type) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        current_type =
            project.named(
                base_type);

        return current_type
            ? shm_runtime_v2_result::success
            : shm_runtime_v2_result::
                invalid_input;
    }

    [[nodiscard]] shm_runtime_v2_result
    append_array(
        type_ref& current_type,
        std::uint64_t index,
        shm_offset& pending_static) {

        strip_cv(
            current_type);

        derived_type_record array;
        shm_value_layout child;

        if (!project.derived(
                current_type,
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

            return shm_runtime_v2_result::
                invalid_input;
        }

        const auto delta =
            index *
            child.size;

        if (!add_offset(
                pending_static,
                delta)) {

            return shm_runtime_v2_result::
                overflow;
        }

        current_type =
            array.child;

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    compile_endpoint(
        object_endpoint endpoint,
        shm_runtime_v2::endpoint_program& program,
        type_ref* resolved_type = nullptr, bool pointer_target = false) {

        program = {};

        if (!endpoint.object ||
            endpoint.object.kind() !=
                identity_kind::object ||
            !endpoint.member) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        auto& root = root_cache[endpoint.object.value() & (root_cache.size() - 1)];
        if (root.id != endpoint.object) {
            const auto object = project.find_object(endpoint.object);
            object_entry value;
            shm_offset offset = 0;
            if (!object || !project.object(object, value) || !layout.object_offset(object, offset))
                return shm_runtime_v2_result::invalid_input;
            root = {endpoint.object, value, offset};
        }
        const auto object_value = root.value;
        const auto object_offset = root.offset;
        const auto key = (std::uint64_t{object_value.type.value()} * 0x9e3779b185ebca87ull) ^
            (std::uint64_t{endpoint.member.value()} * 0xc2b2ae3d27d4eb4full) ^ std::uint64_t{pointer_target};
        auto& recipe = recipe_cache[(key ^ (key >> 32)) & (recipe_cache.size() - 1)];
        if (recipe.root_type == object_value.type && recipe.member == endpoint.member &&
            recipe.pointer_target == pointer_target) {
            program = recipe.program;
            program.root = object_offset;
            if (program.dereference_count) {
                const auto original = program.dereference_begin;
                const auto count = program.dereference_count;
                if (output.endpoint_dereferences.size() > UINT32_MAX - count)
                    return shm_runtime_v2_result::overflow;
                program.dereference_begin = static_cast<std::uint32_t>(output.endpoint_dereferences.size());
                for (std::uint32_t i = 0; i < count; ++i) {
                    const auto delta = output.endpoint_dereferences[original + i];
                    output.endpoint_dereferences.push_back(delta);
                }
            }
            if (program.reserved[0] && !resolved_type)
                return shm_runtime_v2_result::invalid_input;
            if (resolved_type) *resolved_type = recipe.value_type;
            return shm_runtime_v2_result::success;
        }

        program.root =
            object_offset;

        if (output.endpoint_dereferences.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())) {

            return shm_runtime_v2_result::
                overflow;
        }

        program.dereference_begin =
            static_cast<std::uint32_t>(
                output.endpoint_dereferences.size());

        type_ref current_type =
            object_value.type;

        shm_offset pending_static = 0;

        if (!endpoint.member.is_path()) {
            const auto local =
                endpoint.member.
                    direct_member();

            if (!local) {
                return shm_runtime_v2_result::
                    invalid_input;
            }

            const auto appended =
                append_member(
                    current_type,
                    local.value(),
                    pending_static);

            if (appended !=
                shm_runtime_v2_result::
                    success) {

                return appended;
            }
        }
        else {
            endpoint_path_record path;

            if (!project.endpoint_path(
                    endpoint.member.path(),
                    path) ||
                path.root_type !=
                    object_value.type) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            for (std::uint32_t index = 0;
                 index < path.steps.count;
                 ++index) {

                endpoint_path_step step;

                if (!project.endpoint_path_step_at(
                        static_cast<std::size_t>(
                            path.steps.begin) +
                            index,
                        step)) {

                    return shm_runtime_v2_result::
                        invalid_input;
                }

                shm_runtime_v2_result result =
                    shm_runtime_v2_result::
                        invalid_input;

                switch (step.kind) {
                case endpoint_path_step_kind::
                    member:
                    result =
                        append_member(
                            current_type,
                            step.value,
                            pending_static);
                    break;

                case endpoint_path_step_kind::
                    base:
                    result =
                        append_base(
                            current_type,
                            step.value,
                            pending_static);
                    break;

                case endpoint_path_step_kind::
                    array_index:
                    result =
                        append_array(
                            current_type,
                            step.value,
                            pending_static);
                    break;

                case endpoint_path_step_kind::bit_index:
                    if (!resolved_type || index + 1 != path.steps.count ||
                        !valid_bit_index(project, current_type, step.value) ||
                        pending_static > (std::numeric_limits<shm_offset>::max)() - step.value / 8)
                        return shm_runtime_v2_result::invalid_input;
                    pending_static += step.value / 8;
                    program.reserved[0] = static_cast<std::uint8_t>(1 + step.value % 8);
                    current_type = path.value_type;
                    result = shm_runtime_v2_result::success;
                    break;
                case endpoint_path_step_kind::
                    dereference: {
                    type_ref referent;

                    if (step.value != 0 ||
                        !reference_referent(
                            current_type,
                            referent, true)) {

                        return shm_runtime_v2_result::
                            invalid_input;
                    }

                    result =
                        append_dereference(
                            program,
                            pending_static);

                    if (result ==
                        shm_runtime_v2_result::
                            success) {

                        current_type =
                            referent;
                    }

                    break;
                }
                }

                if (result !=
                    shm_runtime_v2_result::
                        success) {

                    return result;
                }
            }

            if (current_type !=
                path.value_type) {

                return shm_runtime_v2_result::
                    invalid_input;
            }
        }

        program.tail =
            pending_static;

        type_ref ignored;

        program.final_reference =
            reference_referent(
                current_type,
                ignored, pointer_target);

        if (resolved_type != nullptr) {
            *resolved_type =
                current_type;
        }

        recipe = {object_value.type, endpoint.member, current_type, pointer_target, program};
        return shm_runtime_v2_result::
            success;
    }

    template <typename T>
    [[nodiscard]] static bool integer_value(
        construction_value construction,
        T& value) noexcept {

        static_assert(
            std::is_integral_v<T>);

        value = {};

        if (construction.kind ==
            construction_kind::zero) {

            return true;
        }

        if (construction.kind ==
            construction_kind::
                signed_integer) {

            const auto source =
                std::bit_cast<std::int64_t>(
                    construction.bits());

            if constexpr (
                std::is_signed_v<T>) {

                if (source <
                        static_cast<std::int64_t>(
                            (std::numeric_limits<
                                T>::min)()) ||
                    source >
                        static_cast<std::int64_t>(
                            (std::numeric_limits<
                                T>::max)())) {

                    return false;
                }
            }
            else {
                if (source < 0 ||
                    static_cast<std::uint64_t>(
                        source) >
                        static_cast<std::uint64_t>(
                            (std::numeric_limits<
                                T>::max)())) {

                    return false;
                }
            }

            value =
                static_cast<T>(
                    source);

            return true;
        }

        if (construction.kind ==
            construction_kind::
                unsigned_integer) {

            const auto source =
                construction.bits();

            if (source >
                static_cast<std::uint64_t>(
                    (std::numeric_limits<
                        T>::max)())) {

                return false;
            }

            value =
                static_cast<T>(
                    source);

            return true;
        }

        return false;
    }

    template <typename T>
    [[nodiscard]] static
    shm_runtime_v2_result encode_native(
        const T& value,
        shm_runtime_v2::
            initialization_plan& plan) noexcept {

        static_assert(
            std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 16);

        plan.value = {};
        std::memcpy(
            plan.value.data(),
            &value,
            sizeof(T));

        plan.size =
            static_cast<std::uint8_t>(
                sizeof(T));

        return shm_runtime_v2_result::
            success;
    }

    template <typename T>
    [[nodiscard]] static
    shm_runtime_v2_result encode_integer(
        construction_value construction,
        shm_runtime_v2::
            initialization_plan& plan) noexcept {

        T value{};

        return integer_value(
                   construction,
                   value)
            ? encode_native(
                value,
                plan)
            : shm_runtime_v2_result::
                invalid_input;
    }

    template <typename T>
    [[nodiscard]] static
    shm_runtime_v2_result encode_real(
        construction_value construction,
        shm_runtime_v2::
            initialization_plan& plan) noexcept {

        T value{};

        switch (construction.kind) {
        case construction_kind::zero:
            break;

        case construction_kind::
            signed_integer:
            value =
                static_cast<T>(
                    std::bit_cast<std::int64_t>(
                        construction.bits()));
            break;

        case construction_kind::
            unsigned_integer:
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

        case construction_kind::
            member_binding:
        case construction_kind::
            object_binding:
        case construction_kind::
            unsupported:
            return shm_runtime_v2_result::
                invalid_input;
        }

        return encode_native(
            value,
            plan);
    }

    [[nodiscard]] static bool
    zero_pointer_construction(
        construction_value construction) noexcept {

        if (construction.kind ==
            construction_kind::zero) {

            return true;
        }

        if (construction.kind ==
                construction_kind::
                    signed_integer ||
            construction.kind ==
                construction_kind::
                    unsigned_integer) {

            return construction.bits() == 0;
        }

        return false;
    }

    [[nodiscard]] shm_runtime_v2_result
    encode_pointer_zero(
        construction_value construction,
        shm_runtime_v2::
            initialization_plan& plan) const noexcept {

        if (!zero_pointer_construction(
                construction)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        abi_properties properties;

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.pointer_size != 4 &&
             properties.pointer_size != 8)) {

            return shm_runtime_v2_result::
                incompatible_abi;
        }

        plan.value = {};
        plan.size = static_cast<std::uint8_t>(properties.pointer_size);

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    encode_initialization(
        type_ref type,
        construction_value construction,
        shm_runtime_v2::
            initialization_plan& plan) noexcept {

        if (construction.kind == construction_kind::byte_array) {
            if (!construction_compatible(project, type, construction) || !valid_byte_array_value(project, construction))
                return shm_runtime_v2_result::invalid_input;
            const auto data = project.string(construction.low);
            const auto mask = project.string(construction.high);
            const auto count = output.initialization_bytes.size();
            if (count + data.size() + mask.size() > (std::numeric_limits<std::uint32_t>::max)())
                return shm_runtime_v2_result::overflow;
            const char_array_store store{static_cast<std::uint32_t>(count), static_cast<std::uint32_t>(data.size()),
                construction.operand, static_cast<std::uint32_t>(mask.size())};
            try {
                for (const auto bytes : {data, mask}) {
                    if (!bytes.empty()) output.initialization_bytes.insert(output.initialization_bytes.end(),
                        reinterpret_cast<const std::byte*>(bytes.data()), reinterpret_cast<const std::byte*>(bytes.data() + bytes.size()));
                }
            } catch (...) { return shm_runtime_v2_result::failed; }
            std::memcpy(plan.value.data(), &store, sizeof store);
            plan.size = 0;
            plan.reserved[0] = 255;
            return shm_runtime_v2_result::success;
        }

        if (construction.kind == construction_kind::byte_patch) {
            if (!valid_construction(construction) || type.kind() != type_ref_kind::intrinsic ||
                type.payload() != static_cast<std::uint32_t>(intrinsic_type::char_type))
                return shm_runtime_v2_result::invalid_input;
            plan.value = {};
            const auto bits = construction.bits();
            std::memcpy(plan.value.data(), &bits, sizeof bits);
            plan.size = static_cast<std::uint8_t>(std::bit_width(construction.operand));
            plan.reserved[0] = static_cast<std::uint8_t>(construction.operand);
            return shm_runtime_v2_result::success;
        }

        derived_type_record derived;

        for (;;) {
            if (!project.derived(
                    type,
                    derived)) {

                break;
            }

            if (derived.kind ==
                derived_type_kind::
                    const_qualified) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            if (derived.kind ==
                derived_type_kind::
                    volatile_qualified) {

                type =
                    derived.child;

                continue;
            }

            break;
        }

        auto storage_type = type.kind() == type_ref_kind::intrinsic
            ? static_cast<intrinsic_type>(type.payload()) : intrinsic_type::none;
        if (type.kind() == type_ref_kind::named) {
            type_entry entry;
            if (project.type(project.type_location(type), entry) &&
                entry.scalar_named()) storage_type = entry.storage_intrinsic();
        }
        if (storage_type != intrinsic_type::none) {
            switch (storage_type) {

            case intrinsic_type::bool_type: {
                bool value = false;

                switch (construction.kind) {
                case construction_kind::zero:
                    break;

                case construction_kind::
                    signed_integer:
                    value =
                        std::bit_cast<
                            std::int64_t>(
                                construction.bits()) !=
                        0;
                    break;

                case construction_kind::
                    unsigned_integer:
                    value =
                        construction.bits() !=
                        0;
                    break;

                case construction_kind::real:
                    value =
                        std::bit_cast<double>(
                            construction.bits()) !=
                        0.0;
                    break;

                case construction_kind::
                    member_binding:
                case construction_kind::
                    object_binding:
                case construction_kind::
                    unsupported:
                    return shm_runtime_v2_result::
                        invalid_input;
                }

                return encode_native(
                    value,
                    plan);
            }

            case intrinsic_type::char_type:
                return encode_integer<char>(
                    construction,
                    plan);
            case intrinsic_type::signed_char:
                return encode_integer<
                    signed char>(
                        construction,
                        plan);
            case intrinsic_type::unsigned_char:
                return encode_integer<
                    unsigned char>(
                        construction,
                        plan);
            case intrinsic_type::wchar_type:
                return encode_integer<wchar_t>(
                    construction,
                    plan);
            case intrinsic_type::char8_type:
                return encode_integer<char8_t>(
                    construction,
                    plan);
            case intrinsic_type::char16_type:
                return encode_integer<char16_t>(
                    construction,
                    plan);
            case intrinsic_type::char32_type:
                return encode_integer<char32_t>(
                    construction,
                    plan);
            case intrinsic_type::signed_short:
                return encode_integer<short>(
                    construction,
                    plan);
            case intrinsic_type::unsigned_short:
                return encode_integer<
                    unsigned short>(
                        construction,
                        plan);
            case intrinsic_type::signed_int:
                return encode_integer<int>(
                    construction,
                    plan);
            case intrinsic_type::unsigned_int:
                return encode_integer<
                    unsigned int>(
                        construction,
                        plan);
            case intrinsic_type::signed_long:
                return encode_integer<long>(
                    construction,
                    plan);
            case intrinsic_type::unsigned_long:
                return encode_integer<
                    unsigned long>(
                        construction,
                        plan);
            case intrinsic_type::signed_long_long:
                return encode_integer<
                    long long>(
                        construction,
                        plan);
            case intrinsic_type::unsigned_long_long:
                return encode_integer<
                    unsigned long long>(
                        construction,
                        plan);
            case intrinsic_type::float_type:
                return encode_real<float>(
                    construction,
                    plan);
            case intrinsic_type::double_type:
                return encode_real<double>(
                    construction,
                    plan);
            case intrinsic_type::long_double_type:
                return encode_real<long double>(
                    construction,
                    plan);
            case intrinsic_type::nullptr_type:
                return encode_pointer_zero(
                    construction,
                    plan);
            case intrinsic_type::void_type:
                return shm_runtime_v2_result::
                    unsupported_type;
            case intrinsic_type::none:
                return shm_runtime_v2_result::
                    invalid_input;
            }
        }

        if (type.kind() ==
            type_ref_kind::derived &&
            project.derived(
                type,
                derived) &&
            derived.kind ==
                derived_type_kind::pointer) {

            return encode_pointer_zero(
                construction,
                plan);
        }

        return shm_runtime_v2_result::
            invalid_input;
    }

    const compiled_project_view& project;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    shm_runtime_v2& output;
    shm_runtime_v2_physical_plan_telemetry*
        telemetry = nullptr;
};


namespace {

struct runtime_plan_parallel_job final {
    const compiled_project_view& project;
    const server_abi_configuration& abi;
    const shm_layout& layout;

    shm_runtime_v2 links;
    shm_runtime_v2 initializations;

    shm_runtime_v2_physical_plan_telemetry*
        telemetry = nullptr;

    std::array<
        shm_runtime_v2_result,
        2> results{
            shm_runtime_v2_result::failed,
            shm_runtime_v2_result::failed,
        };

    static void run(
        void* context,
        std::size_t lane) noexcept {

        auto& job =
            *static_cast<
                runtime_plan_parallel_job*>(
                    context);

        if (lane >= 2) {
            return;
        }

        try {
            if (lane == 0) {
                shm_runtime_v2_link_builder builder{
                    job.project,
                    job.abi,
                    job.layout,
                    job.links,
                    job.telemetry,
                };

                job.results[0] =
                    builder.build_links_only();
            }
            else {
                shm_runtime_v2_link_builder builder{
                    job.project,
                    job.abi,
                    job.layout,
                    job.initializations,
                    job.telemetry,
                };

                job.results[1] =
                    builder.
                        build_initializations_only();
            }
        }
        catch (...) {
            job.results[lane] =
                shm_runtime_v2_result::failed;
        }
    }
};

constexpr std::array<std::byte, 8>
    shm_runtime_v2_execution_magic{
        std::byte{'S'}, std::byte{'E'}, std::byte{'R'}, std::byte{'V'},
        std::byte{'2'}, std::byte{'E'}, std::byte{'X'}, std::byte{'1'},
    };

constexpr std::uint32_t
    shm_runtime_v2_execution_version = 1;

struct shm_runtime_v2_execution_header final {
    std::array<std::byte, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t reserved0 = 0;

    std::uint64_t live_links = 0;
    std::uint64_t link_dereferences = 0;
    std::uint64_t initializations = 0;
    std::uint64_t endpoint_programs = 0;
    std::uint64_t endpoint_dereferences = 0;
    std::uint64_t reserved1 = 0;
};

static_assert(
    sizeof(shm_runtime_v2_execution_header) == 64);

template <typename T>
[[nodiscard]] bool runtime_const_records(
    std::span<const std::byte> bytes,
    std::span<const T>& output) noexcept {

    output = {};

    if (bytes.size() % sizeof(T) != 0 ||
        (bytes.data() != nullptr &&
         reinterpret_cast<std::uintptr_t>(
             bytes.data()) %
             alignof(T) != 0)) {
        return false;
    }

    output = {
        reinterpret_cast<const T*>(
            bytes.data()),
        bytes.size() / sizeof(T),
    };

    return true;
}

template <typename T>
[[nodiscard]] bool runtime_mutable_records(
    std::span<std::byte> bytes,
    std::span<T>& output) noexcept {

    output = {};

    if (bytes.size() % sizeof(T) != 0 ||
        (bytes.data() != nullptr &&
         reinterpret_cast<std::uintptr_t>(
             bytes.data()) %
             alignof(T) != 0)) {
        return false;
    }

    output = {
        reinterpret_cast<T*>(
            bytes.data()),
        bytes.size() / sizeof(T),
    };

    return true;
}

}

std::size_t
shm_runtime_v2::link_slot_count() const noexcept {

    return persisted_physical_value
        ? persisted_links.size()
        : links.size();
}

bool shm_runtime_v2::link_program_at(
    std::size_t index,
    endpoint_program& source,
    endpoint_program& target,
    bool& live) const noexcept {

    source = {};
    target = {};
    live = false;

    if (!persisted_physical_value) {
        if (index >= links.size()) {
            return false;
        }

        const auto& plan =
            links[index];

        source = plan.source;
        target = plan.target;
        live = plan.live;
        return true;
    }

    if (index >=
        persisted_links.size()) {
        return false;
    }

    const auto& physical =
        persisted_links[index];

    if ((physical.flags &
            ~(shm_runtime_v2_physical_link_live |
              shm_runtime_v2_physical_link_source_reference)) !=
            0 ||
        physical.reserved != 0) {
        return false;
    }

    live =
        (physical.flags &
            shm_runtime_v2_physical_link_live) != 0;

    if (!live) {
        return
            physical.source == 0 &&
            physical.target == 0 &&
            physical.source_program == 0 &&
            physical.target_program == 0 &&
            physical.flags == 0;
    }

    const auto decode =
        [&](shm_offset direct,
            std::uint32_t program_slot,
            bool final_reference,
            endpoint_program& output_program) noexcept {

            output_program = {};
            output_program.final_reference =
                final_reference;

            if (program_slot == 0) {
                output_program.root =
                    direct;
                return true;
            }

            if (program_slot >
                persisted_endpoint_programs.size()) {
                return false;
            }

            const auto& program =
                persisted_endpoint_programs[
                    program_slot - 1];

            output_program.root =
                program.root;
            output_program.tail =
                program.tail;
            output_program.dereference_begin =
                program.dereference_begin;
            output_program.dereference_count =
                program.dereference_count;

            return true;
        };

    return
        decode(
            physical.source,
            physical.source_program,
            (physical.flags &
                shm_runtime_v2_physical_link_source_reference) != 0,
            source) &&
        decode(
            physical.target,
            physical.target_program,
            true,
            target);
}

shm_runtime_v2::link_state*
shm_runtime_v2::link_state_at(
    std::size_t index) noexcept {

    if (persisted_physical_value) {
        return index <
                persisted_link_states.size()
            ? &persisted_link_states[index]
            : nullptr;
    }

    return index < links.size()
        ? &links[index].state
        : nullptr;
}

bool shm_runtime_v2::set_link_target_slot(
    std::size_t index,
    shm_offset value) noexcept {

    if (persisted_physical_value) {
        if (index >=
            persisted_link_target_slots.size()) {
            return false;
        }

        persisted_link_target_slots[index] =
            value;

        return true;
    }

    if (index >= links.size()) {
        return false;
    }

    links[index].target_slot =
        value;

    return true;
}

bool shm_runtime_v2::link_target_slot_at(
    std::size_t index,
    shm_offset& value) const noexcept {

    value = 0;

    if (persisted_physical_value) {
        if (index >=
            persisted_link_target_slots.size()) {
            return false;
        }

        value =
            persisted_link_target_slots[index];

        return true;
    }

    if (index >= links.size()) {
        return false;
    }

    value =
        links[index].target_slot;

    return true;
}

bool shm_runtime_v2::initialization_program_at(
    std::size_t index,
    initialization_plan& value) const noexcept {

    value = {};

    if (!persisted_physical_value) {
        if (index >= initializations.size()) {
            return false;
        }

        value =
            initializations[index];

        return true;
    }

    if (index >=
        persisted_initializations.size()) {
        return false;
    }

    const auto& physical =
        persisted_initializations[index];

    if ((physical.size == 0 ? physical.reserved[0] != 255 :
        (physical.size > physical.value.size() ||
         (physical.reserved[0] != 0 && (physical.size > 8 || (physical.reserved[0] >> physical.size) != 0)))) ||
        (physical.reserved[1] && (physical.reserved[1] > 8 || physical.size != 1 || physical.reserved[0])) ||
        physical.reserved[2] != 0) {
        return false;
    }

    value.value =
        physical.value;
    value.size =
        physical.size;
    value.reserved[0] = physical.reserved[0];
    value.reserved[1] = physical.reserved[1];
    value.target.final_reference =
        false;

    if (physical.target_program == 0) {
        value.target.root =
            physical.target;
        return true;
    }

    if (physical.target_program >
        persisted_endpoint_programs.size()) {
        return false;
    }

    const auto& program =
        persisted_endpoint_programs[
            physical.target_program - 1];

    value.target.root =
        program.root;
    value.target.tail =
        program.tail;
    value.target.dereference_begin =
        program.dereference_begin;
    value.target.dereference_count =
        program.dereference_count;

    return true;
}

std::size_t
shm_runtime_v2::endpoint_dereference_count_total() const noexcept {

    return persisted_physical_value
        ? persisted_endpoint_dereferences.size()
        : endpoint_dereferences.size();
}

bool shm_runtime_v2::endpoint_dereference_at(
    std::size_t index,
    shm_offset& value) const noexcept {

    value = 0;

    if (persisted_physical_value) {
        if (index >=
            persisted_endpoint_dereferences.size()) {
            return false;
        }

        value =
            persisted_endpoint_dereferences[index];

        return true;
    }

    if (index >=
        endpoint_dereferences.size()) {
        return false;
    }

    value =
        endpoint_dereferences[index];

    return true;
}

bool shm_runtime_v2::physical_counts(shm_runtime_v2_physical_counts& output) const noexcept {
    output = {};
    if (persisted_physical_value) return false;
    output.link_slots = links.size();
    output.initializations = initializations.size();
    output.dereferences = endpoint_dereferences.size();
    output.initialization_bytes = initialization_bytes.size();
    for (const auto& link : links) {
        if (!link.live) continue;
        output.endpoint_programs += link.source.dereference_count != 0;
        output.endpoint_programs += link.target.dereference_count != 0;
    }
    for (const auto& value : initializations)
        output.endpoint_programs += value.target.dereference_count != 0;
    output.compact_links = compact_link_offsets && output.endpoint_programs < shm_runtime_v2_compact_limit;
    return true;
}

shm_runtime_v2_result prepare_shm_runtime_v2_physical_plan(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& runtime,
    shm_runtime_v2_physical_plan_telemetry*
        telemetry) noexcept {

    runtime = shm_runtime_v2{};

    if (telemetry != nullptr) {
        *telemetry = {};
    }

    try {
        if (execution_lane_capacity() >= 2) {
            runtime_plan_parallel_job job{
                project,
                abi,
                layout,
                {},
                {},
                telemetry,
            };

            execution_lanes lanes;

            const auto started =
                lanes.start(2);

            if (succeeded(started)) {
                const auto ran =
                    lanes.run(
                        2,
                        runtime_plan_parallel_job::run,
                        &job);

                if (succeeded(ran)) {
                    if (job.results[0] !=
                        shm_runtime_v2_result::
                            success) {

                        runtime = shm_runtime_v2{};
                        return job.results[0];
                    }

                    if (job.results[1] !=
                        shm_runtime_v2_result::
                            success) {

                        runtime = shm_runtime_v2{};
                        return job.results[1];
                    }

                    const auto merged =
                        shm_runtime_v2_link_builder::
                            merge_parallel(
                                std::move(
                                    job.links),
                                std::move(
                                    job.initializations),
                                runtime,
                                telemetry);

                    if (merged !=
                        shm_runtime_v2_result::
                            success) {

                        runtime = shm_runtime_v2{};
                    }

                    return merged;
                }
            }
        }

        // Execution-lane creation/dispatch is an optimization boundary.
        // Preserve the exact sequential producer as the correctness fallback.
        shm_runtime_v2_link_builder builder{
            project,
            abi,
            layout,
            runtime,
            telemetry,
        };

        const auto built =
            builder.build();

        if (built !=
            shm_runtime_v2_result::success) {

            runtime = shm_runtime_v2{};
            return built;
        }
    }
    catch (...) {
        runtime = shm_runtime_v2{};

        if (telemetry != nullptr) {
            *telemetry = {};
        }

        return shm_runtime_v2_result::failed;
    }

    return shm_runtime_v2_result::success;
}

shm_runtime_v2_result encode_shm_runtime_v2_physical_columns(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> compiled_image) noexcept {
    shm_runtime_v2 runtime;
    const auto prepared = prepare_shm_runtime_v2_physical_plan(project, abi, layout, runtime);
    return prepared == shm_runtime_v2_result::success
        ? encode_shm_runtime_v2_physical_columns(runtime, layout, compiled_image) : prepared;
}

shm_runtime_v2_result encode_shm_runtime_v2_physical_columns(
    const shm_runtime_v2& runtime,
    const shm_layout& layout,
    std::span<std::byte> compiled_image) noexcept {

    const auto section =
        [&](compiled_project_section kind) noexcept {
            return
                runtime_project_mutable_section(
                    compiled_image,
                    kind);
        };

    const auto byte_destination = section(compiled_project_section::runtime_initialization_bytes);
    if (byte_destination.size() != runtime.initialization_bytes.size()) return shm_runtime_v2_result::invalid_input;
    if (!byte_destination.empty()) std::memcpy(byte_destination.data(), runtime.initialization_bytes.data(), byte_destination.size());

    std::span<shm_runtime_v2_execution_header>
        header;
    const auto links = section(compiled_project_section::link_runtime);
    const bool compact_links = links.size() == runtime.links.size() * sizeof(shm_runtime_v2_compact_link);
    if (!compact_links && links.size() != runtime.links.size() * sizeof(shm_runtime_v2_physical_link))
        return shm_runtime_v2_result::invalid_input;
    const auto write_link = [&](std::size_t index, const shm_runtime_v2_physical_link& link) {
        if (compact_links) {
            shm_runtime_v2_compact_link packed;
            if (!pack_shm_runtime_v2_link(link, packed)) return false;
            std::memcpy(links.data() + index * sizeof packed, &packed, sizeof packed);
        } else std::memcpy(links.data() + index * sizeof link, &link, sizeof link);
        return true;
    };
    std::span<shm_runtime_v2_physical_initialization>
        initializations;
    std::span<shm_runtime_v2_physical_endpoint_program>
        programs;
    std::span<shm_offset>
        dereferences;

    if (!runtime_mutable_records(
            section(
                compiled_project_section::
                    runtime_execution_header),
            header) ||
        header.size() != 1 ||
        !runtime_mutable_records(
            section(
                compiled_project_section::
                    initialization_runtime),
            initializations) ||
        initializations.size() !=
            runtime.initializations.size() ||
        !runtime_mutable_records(
            section(
                compiled_project_section::
                    runtime_endpoint_programs),
            programs) ||
        !runtime_mutable_records(
            section(
                compiled_project_section::
                    runtime_endpoint_dereferences),
            dereferences) ||
        dereferences.size() !=
            runtime.endpoint_dereferences.size()) {

        return shm_runtime_v2_result::
            invalid_input;
    }


    std::fill(
        initializations.begin(),
        initializations.end(),
        shm_runtime_v2_physical_initialization{});

    std::fill(
        programs.begin(),
        programs.end(),
        shm_runtime_v2_physical_endpoint_program{});

    std::copy(
        runtime.endpoint_dereferences.begin(),
        runtime.endpoint_dereferences.end(),
        dereferences.begin());

    std::size_t program_cursor = 0;

    const auto encode_endpoint =
        [&](const shm_runtime_v2::endpoint_program& input,
            shm_offset& direct,
            std::uint32_t& program_slot)
            -> bool {

            direct = 0;
            program_slot = 0;

            if (input.dereference_count == 0) {
                if (input.root >=
                        layout.size() ||
                    input.tail >
                        layout.size() -
                            input.root) {
                    return false;
                }

                const auto position =
                    input.root +
                    input.tail;

                if (position >=
                    layout.size()) {
                    return false;
                }

                direct = position;
                return true;
            }

            if (program_cursor >=
                    programs.size() ||
                input.dereference_begin >
                    dereferences.size() ||
                input.dereference_count >
                    dereferences.size() -
                        input.dereference_begin ||
                program_cursor >=
                    static_cast<std::size_t>(
                        (std::numeric_limits<
                            std::uint32_t>::max)())) {
                return false;
            }

            programs[program_cursor] = {
                input.root,
                input.tail,
                input.dereference_begin,
                input.dereference_count,
            };

            program_slot =
                static_cast<std::uint32_t>(
                    program_cursor + 1);

            ++program_cursor;

            return true;
        };

    for (std::size_t index = 0;
         index < runtime.links.size();
         ++index) {

        const auto& source =
            runtime.links[index];

        shm_runtime_v2_physical_link target{};
        if (!source.live) {
            if (!write_link(index, target)) return shm_runtime_v2_result::invalid_input;
            continue;
        }

        if (!encode_endpoint(
                source.source,
                target.source,
                target.source_program) ||
            !encode_endpoint(
                source.target,
                target.target,
                target.target_program) ||
            !source.target.final_reference) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        target.flags =
            shm_runtime_v2_physical_link_live;

        if (source.source.final_reference) {
            target.flags |=
                shm_runtime_v2_physical_link_source_reference;
        }
        if (!write_link(index, target)) return shm_runtime_v2_result::invalid_input;
    }

    for (std::size_t index = 0;
         index < runtime.initializations.size();
         ++index) {

        const auto& source =
            runtime.initializations[index];

        auto& target =
            initializations[index];

        if (source.target.final_reference ||
            (source.size == 0 ? source.reserved[0] != 255 : source.size > source.value.size()) ||
            !encode_endpoint(
                source.target,
                target.target,
                target.target_program)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        target.value =
            source.value;
        target.size =
            source.size;
        target.reserved[0] = source.reserved[0];
        target.reserved[1] = source.reserved[1];
    }

    if (program_cursor !=
        programs.size()) {
        return shm_runtime_v2_result::
            invalid_input;
    }

    header.front() = {};
    header.front().magic =
        shm_runtime_v2_execution_magic;
    header.front().version =
        shm_runtime_v2_execution_version;
    header.front().live_links =
        runtime.live_link_count_value;
    header.front().link_dereferences =
        runtime.link_dereference_count_value;
    header.front().initializations =
        runtime.initializations.size();
    header.front().endpoint_programs =
        programs.size();
    header.front().endpoint_dereferences =
        dereferences.size();

    return shm_runtime_v2_result::success;
}

shm_runtime_v2_result
attach_shm_runtime_v2_physical_columns(
    const runtime_project_view& project,
    shm_runtime_v2& output) noexcept {

    const auto records =
        []<typename T>(
            std::span<const std::byte> bytes) noexcept {

            return std::span<const T>{
                reinterpret_cast<const T*>(
                    bytes.data()),
                bytes.size() /
                    sizeof(T),
            };
        };

    const auto header =
        records.template operator()<
            shm_runtime_v2_execution_header>(
                project.section(
                    compiled_project_section::
                        runtime_execution_header));

    if (header.size() != 1) return shm_runtime_v2_result::invalid_input;
    const auto& value = header.front();

    output.persisted_initialization_bytes = project.section(compiled_project_section::runtime_initialization_bytes);

    output.persisted_links.assign(project.section(compiled_project_section::link_runtime),
        project.record_size(compiled_project_section::link_runtime));

    output.persisted_initializations =
        records.template operator()<
            shm_runtime_v2_physical_initialization>(
                project.section(
                    compiled_project_section::
                        initialization_runtime));

    output.persisted_endpoint_programs =
        records.template operator()<
            shm_runtime_v2_physical_endpoint_program>(
                project.section(
                    compiled_project_section::
                        runtime_endpoint_programs));

    output.persisted_endpoint_dereferences =
        records.template operator()<
            shm_offset>(
                project.section(
                    compiled_project_section::
                        runtime_endpoint_dereferences));

    if (value.magic != shm_runtime_v2_execution_magic ||
        value.version != shm_runtime_v2_execution_version || value.reserved0 || value.reserved1 ||
        value.live_links > output.persisted_links.size() ||
        value.initializations != output.persisted_initializations.size() ||
        value.endpoint_programs != output.persisted_endpoint_programs.size() ||
        value.endpoint_dereferences != output.persisted_endpoint_dereferences.size() ||
        value.link_dereferences > value.endpoint_dereferences)
        return shm_runtime_v2_result::invalid_input;

    try {
        output.persisted_link_states.assign(
            output.persisted_links.size(),
            shm_runtime_v2::
                link_state::prepared);

        output.persisted_link_target_slots.assign(
            output.persisted_links.size(),
            0);
    }
    catch (...) {
        return shm_runtime_v2_result::
            failed;
    }

    output.live_link_count_value =
        static_cast<std::size_t>(
            value.live_links);

    output.link_dereference_count_value =
        static_cast<std::size_t>(
            value.link_dereferences);

    output.persisted_physical_value = true;

    return shm_runtime_v2_result::
        success;
}


class shm_runtime_v2_link_executor final {
public:
    shm_runtime_v2_link_executor(
        shm_runtime_v2& runtime,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        std::span<std::byte> shm,
        shm_runtime_v2_link_telemetry* telemetry,
        shm_runtime_v2_initialization_telemetry*
            initialization_telemetry = nullptr) noexcept
        : runtime(runtime),
          abi(abi),
          layout(layout),
          shm(shm),
          telemetry(telemetry),
          initialization_telemetry(
              initialization_telemetry) {
    }

    [[nodiscard]] shm_runtime_v2_result mark(bool deferred = false) noexcept {

        const auto valid =
            validate();

        if (valid !=
            shm_runtime_v2_result::success) {

            return valid;
        }

        publish_shape();

        if (runtime.persisted_physical_value) {
            if (deferred) {
                return mark_persisted_complex_targets();
            }
            for (std::size_t index = 0;
                 index < runtime.persisted_links.size();
                 ++index) {

                const auto& link =
                    runtime.persisted_links[index];

                if ((link.flags &
                        shm_runtime_v2_physical_link_live) ==
                    0 || link.target_program != 0) {
                    continue;
                }

                auto& state =
                    runtime.persisted_link_states[index];

                if (state !=
                    shm_runtime_v2::
                        link_state::prepared) {

                    return shm_runtime_v2_result::
                        invalid_input;
                }

                shm_offset target_slot = 0;

                if (link.target_program == 0) {
                    target_slot =
                        link.target;
                }
                else {
                    const auto& physical_program =
                        runtime.persisted_endpoint_programs[
                            link.target_program - 1];

                    shm_runtime_v2::
                        endpoint_program target;

                    target.root =
                        physical_program.root;
                    target.tail =
                        physical_program.tail;
                    target.dereference_begin =
                        physical_program.dereference_begin;
                    target.dereference_count =
                        physical_program.dereference_count;
                    target.final_reference =
                        true;

                    const auto located =
                        endpoint_position(
                            target,
                            false,
                            target_slot);

                    if (located !=
                        shm_runtime_v2_result::
                            success) {
                        return located;
                    }
                }

                std::uint64_t stored = 0;

                if (!read_reference(
                        target_slot,
                        stored) ||
                    stored != 0) {

                    return shm_runtime_v2_result::
                        invalid_input;
                }

                const auto raw =
                    index + 1;

                if (raw == 0 ||
                    raw >
                        link_handle::
                            maximum_slot) {

                    return shm_runtime_v2_result::
                        invalid_input;
                }

                if (!write_word(
                        target_slot,
                        pending_value(
                            static_cast<
                                std::uint32_t>(
                                    raw)))) {

                    return shm_runtime_v2_result::
                        incompatible_abi;
                }

                runtime.persisted_link_target_slots[
                    index] =
                        target_slot;

                state =
                    shm_runtime_v2::
                        link_state::marked;

                if (telemetry != nullptr) {
                    ++telemetry->
                        targets_marked;
                }
            }

            return shm_runtime_v2_result::
                success;
        }

        for (std::size_t index = 0;
             index < runtime.links.size();
             ++index) {

            auto& plan =
                runtime.links[index];

            if (!plan.live ||
                (plan.target.dereference_count != 0) != deferred) {
                continue;
            }

            if (plan.state !=
                    shm_runtime_v2::
                        link_state::prepared ||
                !plan.target.final_reference) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            shm_offset target_slot = 0;

            const auto located =
                endpoint_position(
                    plan.target,
                    deferred,
                    target_slot);

            if (located !=
                shm_runtime_v2_result::
                    success) {

                return located;
            }

            std::uint64_t stored = 0;

            if (!read_reference(
                    target_slot,
                    stored) ||
                (stored != 0 && (!deferred || !runtime_address(stored)))) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            const auto raw =
                index + 1;

            if (raw == 0 ||
                raw >
                    link_handle::
                        maximum_slot) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            if (!write_word(
                    target_slot,
                    pending_value(
                        static_cast<
                            std::uint32_t>(
                                raw)))) {

                return shm_runtime_v2_result::
                    incompatible_abi;
            }

            plan.target_slot =
                target_slot;

            plan.resolved_source =
                invalid_offset();

            plan.state =
                shm_runtime_v2::
                    link_state::marked;

            if (telemetry != nullptr) {
                ++telemetry->
                    targets_marked;
            }
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    mark_persisted_direct_target_range(
        shm_offset target_begin,
        shm_offset target_end) noexcept {

        const auto valid =
            validate();

        if (valid !=
                shm_runtime_v2_result::success ||
            !runtime.persisted_physical_value ||
            target_begin > target_end ||
            target_end > layout.size()) {

            return valid !=
                    shm_runtime_v2_result::success
                ? valid
                : shm_runtime_v2_result::
                    invalid_input;
        }

        for (std::size_t index = 0;
             index < runtime.persisted_links.size();
             ++index) {

            const auto& link =
                runtime.persisted_links[index];

            if ((link.flags &
                    shm_runtime_v2_physical_link_live) ==
                    0 ||
                link.target_program != 0 ||
                link.target < target_begin ||
                link.target >= target_end) {

                continue;
            }

            auto& state =
                runtime.persisted_link_states[
                    index];

            if (state !=
                shm_runtime_v2::
                    link_state::prepared) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            std::uint64_t stored = 0;

            if (!read_reference(
                    link.target,
                    stored) ||
                stored != 0) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            const auto raw =
                index + 1;

            if (raw == 0 ||
                raw >
                    link_handle::
                        maximum_slot) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            if (!write_word(
                    link.target,
                    pending_value(
                        static_cast<
                            std::uint32_t>(
                                raw)))) {

                return shm_runtime_v2_result::
                    incompatible_abi;
            }

            runtime.persisted_link_target_slots[
                index] =
                    link.target;

            state =
                shm_runtime_v2::
                    link_state::marked;

            if (telemetry != nullptr) {
                ++telemetry->
                    targets_marked;
            }
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    mark_persisted_complex_targets() noexcept {

        const auto valid =
            validate();

        if (valid !=
                shm_runtime_v2_result::success ||
            !runtime.persisted_physical_value) {

            return valid !=
                    shm_runtime_v2_result::success
                ? valid
                : shm_runtime_v2_result::
                    invalid_input;
        }

        for (std::size_t index = 0;
             index < runtime.persisted_links.size();
             ++index) {

            const auto& link =
                runtime.persisted_links[index];

            if ((link.flags &
                    shm_runtime_v2_physical_link_live) ==
                    0 ||
                link.target_program == 0) {

                continue;
            }

            auto& state =
                runtime.persisted_link_states[
                    index];

            if (state !=
                shm_runtime_v2::
                    link_state::prepared) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            const auto& physical_program =
                runtime.persisted_endpoint_programs[
                    link.target_program - 1];

            shm_runtime_v2::
                endpoint_program target;

            target.root =
                physical_program.root;
            target.tail =
                physical_program.tail;
            target.dereference_begin =
                physical_program.dereference_begin;
            target.dereference_count =
                physical_program.dereference_count;
            target.final_reference =
                true;

            shm_offset target_slot = 0;

            const auto located =
                endpoint_position(
                    target,
                    true,
                    target_slot);

            if (located !=
                shm_runtime_v2_result::
                    success) {

                return located;
            }

            std::uint64_t stored = 0;

            if (!read_reference(target_slot, stored)) {
                return shm_runtime_v2_result::invalid_input;
            }
            if (stored != 0 && !runtime_address(stored)) {
                std::uint32_t pending = 0;
                return decode_pending(stored, pending)
                    ? shm_runtime_v2_result::conflicting_link_target
                    : shm_runtime_v2_result::invalid_input;
            }

            const auto raw =
                index + 1;

            if (raw == 0 ||
                raw >
                    link_handle::
                        maximum_slot) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            if (!write_word(
                    target_slot,
                    pending_value(
                        static_cast<
                            std::uint32_t>(
                                raw)))) {

                return shm_runtime_v2_result::
                    incompatible_abi;
            }

            runtime.persisted_link_target_slots[
                index] =
                    target_slot;

            state =
                shm_runtime_v2::
                    link_state::marked;

            if (telemetry != nullptr) {
                ++telemetry->
                    targets_marked;
            }
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    materialize() noexcept {

        const auto valid =
            validate();

        if (valid !=
            shm_runtime_v2_result::success) {

            return valid;
        }

        publish_shape();

        // Constructor reference defaults must exist before locating targets
        // through aliases. Direct targets were protected before construction.
        const auto deferred = mark(true);
        if (deferred != shm_runtime_v2_result::success) {
            return deferred;
        }

        if (runtime.persisted_physical_value) {
            for (std::size_t index = 0;
                 index < runtime.persisted_links.size();
                 ++index) {

                const auto& link =
                    runtime.persisted_links[index];

                if ((link.flags &
                        shm_runtime_v2_physical_link_live) ==
                    0) {
                    continue;
                }

                shm_offset ignored = 0;

                const auto resolved =
                    resolve_link(
                        index,
                        ignored);

                if (resolved !=
                    shm_runtime_v2_result::
                        success) {

                    return resolved;
                }
            }

            return shm_runtime_v2_result::
                success;
        }

        for (std::size_t index = 0;
             index < runtime.links.size();
             ++index) {

            if (!runtime.links[index].live) {
                continue;
            }

            shm_offset ignored = 0;

            const auto resolved =
                resolve_link(
                    index,
                    ignored);

            if (resolved !=
                shm_runtime_v2_result::
                    success) {

                return resolved;
            }
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result write_initialization(shm_offset target,
        const std::array<std::byte, 16>& value, unsigned size, unsigned mask, unsigned bit) noexcept {
        if (bit) {
            if (bit > 8 || size != 1 || mask || target >= shm.size() || value[0] > std::byte{1})
                return shm_runtime_v2_result::invalid_input;
            auto& byte = shm[static_cast<std::size_t>(target)];
            const auto bit_mask = static_cast<std::byte>(1u << (bit - 1));
            byte = value[0] == std::byte{0} ? byte & ~bit_mask : byte | bit_mask;
            return shm_runtime_v2_result::success;
        }
        if (size == 0 && mask == 255) {
            char_array_store store;
            std::memcpy(&store, value.data(), sizeof store);
            const std::span<const std::byte> bytes = runtime.persisted_physical_value
                ? runtime.persisted_initialization_bytes : std::span<const std::byte>{runtime.initialization_bytes};
            const auto count = std::uint64_t{store.length} + store.mask_length;
            if (!store.extent || store.length > store.extent || target > shm.size() || store.extent > shm.size() - target ||
                store.offset > bytes.size() || count > bytes.size() - store.offset ||
                store.mask_length > (std::uint64_t{store.extent} + 7) / 8)
                return shm_runtime_v2_result::invalid_input;
            const auto* data = bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data() + store.offset);
            if (store.mask_length && store.extent % 8 && store.mask_length == (std::uint64_t{store.extent} + 7) / 8 &&
                (static_cast<unsigned char>(data[store.length + store.mask_length - 1]) >> (store.extent % 8)))
                return shm_runtime_v2_result::invalid_input;
            return apply_char_array(shm.subspan(static_cast<std::size_t>(target), store.extent),
                {data, store.length}, {data + store.length, store.mask_length})
                ? shm_runtime_v2_result::success : shm_runtime_v2_result::invalid_input;
        }
        if (!size || size > value.size() || target > shm.size() || size > shm.size() - target ||
            (mask && (size > 8 || (mask >> size) != 0)))
            return shm_runtime_v2_result::invalid_input;
        auto* destination = shm.data() + static_cast<std::size_t>(target);
        if (!mask || mask == ((1u << size) - 1)) {
            std::memcpy(destination, value.data(), size);
        } else {
            for (unsigned i = 0; i < size; ++i)
                if (mask & (1u << i)) destination[i] = value[i];
        }
        return shm_runtime_v2_result::success;
    }

    [[nodiscard]] shm_runtime_v2_result
    materialize_initializations() noexcept {

        const auto valid =
            validate();

        if (valid !=
            shm_runtime_v2_result::success) {

            return valid;
        }

        if (initialization_telemetry != nullptr) {
            initialization_telemetry->
                initializations_prepared =
                    runtime.initialization_count();

            initialization_telemetry->
                endpoint_programs =
                    runtime.initialization_count();

            initialization_telemetry->
                dereference_steps =
                    runtime.
                        initialization_dereference_count();
        }

        if (runtime.persisted_physical_value) {
            for (std::size_t index = 0;
                 index <
                    runtime.persisted_initializations.size();
                 ++index) {

                const auto& initialization =
                    runtime.persisted_initializations[
                        index];

                if (initialization.target_program == 0) {
                    const auto written = write_initialization(initialization.target,
                        initialization.value, initialization.size, initialization.reserved[0], initialization.reserved[1]);
                    if (written != shm_runtime_v2_result::success) return written;

                    if (initialization_telemetry !=
                        nullptr) {

                        ++initialization_telemetry->
                            writes;
                    }

                    continue;
                }

                shm_runtime_v2::
                    initialization_plan decoded;

                if (!runtime.initialization_program_at(
                        index,
                        decoded)) {

                    return shm_runtime_v2_result::
                        invalid_input;
                }

                shm_offset target = 0;

                const auto located =
                    endpoint_position(
                        decoded.target,
                        false,
                        target);

                if (located !=
                    shm_runtime_v2_result::
                        success) {

                    return located;
                }

                const auto written = write_initialization(target,
                    decoded.value, decoded.size, decoded.reserved[0], decoded.reserved[1]);
                if (written != shm_runtime_v2_result::success) return written;

                if (initialization_telemetry !=
                    nullptr) {

                    ++initialization_telemetry->
                        writes;
                }
            }

            return shm_runtime_v2_result::
                success;
        }

        for (const auto& initialization :
             runtime.initializations) {

            if ((initialization.size == 0 ? initialization.reserved[0] != 255 :
                initialization.size > initialization.value.size()) ||
                initialization.target.
                    final_reference) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            shm_offset target = 0;

            const auto located =
                endpoint_position(
                    initialization.target,
                    false,
                    target);

            if (located !=
                    shm_runtime_v2_result::
                        success ||
                target >
                    layout.size() ||
                static_cast<shm_offset>(
                    initialization.size) >
                    layout.size() -
                        target) {

                return located ==
                        shm_runtime_v2_result::
                            success
                    ? shm_runtime_v2_result::
                        invalid_input
                    : located;
            }

            const auto written = write_initialization(target,
                initialization.value, initialization.size, initialization.reserved[0], initialization.reserved[1]);
            if (written != shm_runtime_v2_result::success) return written;

            if (initialization_telemetry !=
                nullptr) {

                ++initialization_telemetry->
                    writes;
            }
        }

        return shm_runtime_v2_result::
            success;
    }


private:
    [[nodiscard]] static constexpr
    shm_offset invalid_offset() noexcept {

        return
            (std::numeric_limits<
                shm_offset>::max)();
    }

    void publish_shape() noexcept {

        if (telemetry == nullptr) {
            return;
        }

        telemetry->links_prepared =
            runtime.live_link_count_value;

        telemetry->endpoint_programs =
            runtime.live_link_count_value * 2;

        telemetry->dereference_steps =
            runtime.endpoint_dereference_count_total();
    }

    [[nodiscard]] shm_runtime_v2_result
    validate() noexcept {

        if (!runtime.prepared() ||
            layout.target() !=
                abi.target ||
            layout.size() >
                shm.size() ||
            (layout.size() != 0 &&
             shm.data() == nullptr)) {

            return shm_runtime_v2_result::
                incompatible_abi;
        }

        if (!abi_layout_properties(
                abi.target,
                properties) ||
            (properties.reference_size != 4 &&
             properties.reference_size != 8)) {

            return shm_runtime_v2_result::
                incompatible_abi;
        }

        base_address =
            static_cast<std::uint64_t>(
                reinterpret_cast<
                    std::uintptr_t>(
                        shm.data()));

        mask =
            properties.reference_size == 4
            ? static_cast<std::uint64_t>(
                (std::numeric_limits<
                    std::uint32_t>::max)())
            : (std::numeric_limits<
                std::uint64_t>::max)();

        if (base_address == 0 ||
            base_address > mask) {

            return shm_runtime_v2_result::
                overflow;
        }

        if (layout.size() != 0) {
            const auto tail =
                layout.size() - 1;

            if (tail >
                mask -
                    base_address) {

                return shm_runtime_v2_result::
                    overflow;
            }

            const auto last =
                base_address +
                tail;

            const auto reserved_begin =
                mask -
                static_cast<std::uint64_t>(
                    link_handle::
                        maximum_slot);

            if (last >=
                reserved_begin) {

                return shm_runtime_v2_result::
                    overflow;
            }
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] bool add_offset(
        shm_offset base,
        shm_offset delta,
        shm_offset& output_value) const noexcept {

        output_value = 0;

        if (delta >
            (std::numeric_limits<
                shm_offset>::max)() -
                base) {

            return false;
        }

        const auto value =
            base +
            delta;

        if (value >=
            layout.size()) {

            return false;
        }

        output_value =
            value;

        return true;
    }

    [[nodiscard]] bool reference_fits(
        shm_offset offset) const noexcept {

        const auto size =
            static_cast<shm_offset>(
                properties.reference_size);

        return
            offset <=
                layout.size() &&
            size <=
                layout.size() -
                    offset;
    }

    [[nodiscard]] bool read_reference(
        shm_offset offset,
        std::uint64_t& value) const noexcept {

        value = 0;

        if (!reference_fits(
                offset)) {

            return false;
        }

        const auto* slot =
            shm.data() +
            static_cast<std::size_t>(
                offset);

        if (properties.reference_size == 4) {
            std::uint32_t word = 0;

            std::memcpy(
                &word,
                slot,
                sizeof(word));

            value = word;
            return true;
        }

        if (properties.reference_size == 8) {
            std::memcpy(
                &value,
                slot,
                sizeof(value));

            return true;
        }

        return false;
    }

    [[nodiscard]] bool write_word(
        shm_offset offset,
        std::uint64_t value) noexcept {

        if (!reference_fits(
                offset) ||
            value > mask) {

            return false;
        }

        auto* slot =
            shm.data() +
            static_cast<std::size_t>(
                offset);

        if (properties.reference_size == 4) {
            const auto word =
                static_cast<std::uint32_t>(
                    value);

            std::memcpy(
                slot,
                &word,
                sizeof(word));

            return true;
        }

        if (properties.reference_size == 8) {
            std::memcpy(
                slot,
                &value,
                sizeof(value));

            return true;
        }

        return false;
    }

    [[nodiscard]] std::uint64_t
    pending_value(
        std::uint32_t raw_link) const noexcept {

        return
            (~static_cast<std::uint64_t>(
                raw_link)) &
            mask;
    }

    [[nodiscard]] bool decode_pending(
        std::uint64_t value,
        std::uint32_t& raw_link) const noexcept {

        raw_link = 0;

        if (value > mask ||
            value == mask) {

            return false;
        }

        const auto decoded =
            (~value) &
            mask;

        if (decoded == 0 ||
            decoded >
                link_handle::
                    maximum_slot) {

            return false;
        }

        raw_link =
            static_cast<std::uint32_t>(
                decoded);

        return true;
    }

    [[nodiscard]] bool runtime_address(
        std::uint64_t value) const noexcept {

        return
            value >=
                base_address &&
            value -
                base_address <
                    layout.size();
    }

    [[nodiscard]] shm_runtime_v2_result
    reference_source(
        std::uint64_t value,
        shm_offset& output_value) noexcept {

        output_value = 0;

        if (runtime_address(
                value)) {

            output_value =
                value -
                base_address;

            return shm_runtime_v2_result::
                success;
        }

        std::uint32_t raw_link = 0;

        if (!decode_pending(
                value,
                raw_link) ||
            raw_link == 0 ||
            raw_link >
                runtime.link_slot_count()) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->
                recursive_resolutions;
        }

        return resolve_link(
            raw_link - 1,
            output_value);
    }

    [[nodiscard]] shm_runtime_v2_result
    endpoint_position(
        const shm_runtime_v2::
            endpoint_program& program,
        bool allow_pending,
        shm_offset& output_value) noexcept {

        output_value = 0;

        const auto dereference_count =
            runtime.
                endpoint_dereference_count_total();

        if (program.root >=
                layout.size() ||
            program.dereference_begin >
                dereference_count ||
            program.dereference_count >
                dereference_count -
                    program.
                        dereference_begin) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        auto current =
            program.root;

        for (std::uint32_t index = 0;
             index <
                 program.
                     dereference_count;
             ++index) {

            shm_offset delta = 0;

            if (!runtime.endpoint_dereference_at(
                    static_cast<std::size_t>(
                        program.
                            dereference_begin) +
                        index,
                    delta)) {
                return shm_runtime_v2_result::
                    invalid_input;
            }

            shm_offset slot = 0;

            if (!add_offset(
                    current,
                    delta,
                    slot)) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            std::uint64_t word = 0;

            if (!read_reference(
                    slot,
                    word)) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->
                    dereference_reads;
            }

            if (initialization_telemetry !=
                nullptr) {

                ++initialization_telemetry->
                    dereference_reads;
            }

            if (runtime_address(
                    word)) {

                current =
                    word -
                    base_address;

                continue;
            }

            if (!allow_pending) {
                return shm_runtime_v2_result::
                    invalid_input;
            }

            const auto resolved =
                reference_source(
                    word,
                    current);

            if (resolved !=
                shm_runtime_v2_result::
                    success) {

                return resolved;
            }
        }

        if (!add_offset(
                current,
                program.tail,
                output_value)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    source_position(
        const shm_runtime_v2::
            endpoint_program& program,
        shm_offset& output_value) noexcept {

        shm_offset position = 0;

        const auto located =
            endpoint_position(
                program,
                true,
                position);

        if (located !=
            shm_runtime_v2_result::
                success) {

            return located;
        }

        if (!program.final_reference) {
            output_value =
                position;

            return shm_runtime_v2_result::
                success;
        }

        std::uint64_t word = 0;

        if (!read_reference(
                position,
                word)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        if (telemetry != nullptr) {
            ++telemetry->
                dereference_reads;
        }

        return reference_source(
            word,
            output_value);
    }

    [[nodiscard]] shm_runtime_v2_result
    resolve_persisted_direct_link(
        std::size_t index,
        const shm_runtime_v2_physical_link& link,
        shm_offset& output_value) noexcept {

        output_value = 0;

        auto& state =
            runtime.persisted_link_states[
                index];

        const auto target_slot =
            runtime.persisted_link_target_slots[
                index];

        if (state ==
            shm_runtime_v2::
                link_state::resolved) {

            std::uint64_t stored = 0;

            if (!read_reference(
                    target_slot,
                    stored) ||
                !runtime_address(
                    stored)) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            output_value =
                stored -
                base_address;

            return shm_runtime_v2_result::
                success;
        }

        if (state ==
            shm_runtime_v2::
                link_state::resolving) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        if (state !=
            shm_runtime_v2::
                link_state::marked) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        std::uint64_t stored = 0;

        if (!read_reference(
                target_slot,
                stored)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        if (runtime_address(
                stored)) {

            state =
                shm_runtime_v2::
                    link_state::resolved;

            output_value =
                stored -
                base_address;

            return shm_runtime_v2_result::
                success;
        }

        std::uint32_t raw_pending = 0;

        if (!decode_pending(
                stored,
                raw_pending) ||
            raw_pending !=
                index + 1) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        state =
            shm_runtime_v2::
                link_state::resolving;

        shm_offset source_slot =
            link.source;

        if ((link.flags &
                shm_runtime_v2_physical_link_source_reference) !=
            0) {

            std::uint64_t source_word = 0;

            if (!read_reference(
                    source_slot,
                    source_word)) {

                state =
                    shm_runtime_v2::
                        link_state::marked;

                return shm_runtime_v2_result::
                    invalid_input;
            }

            if (telemetry != nullptr) {
                ++telemetry->
                    dereference_reads;
            }

            const auto resolved =
                reference_source(
                    source_word,
                    source_slot);

            if (resolved !=
                shm_runtime_v2_result::
                    success) {

                state =
                    shm_runtime_v2::
                        link_state::marked;

                return resolved;
            }
        }

        if (!write_word(
                target_slot,
                base_address +
                    source_slot)) {

            state =
                shm_runtime_v2::
                    link_state::marked;

            return shm_runtime_v2_result::
                invalid_input;
        }

        state =
            shm_runtime_v2::
                link_state::resolved;

        output_value =
            source_slot;

        if (telemetry != nullptr) {
            ++telemetry->
                links_resolved;
        }

        return shm_runtime_v2_result::
            success;
    }

    [[nodiscard]] shm_runtime_v2_result
    resolve_link(
        std::size_t index,
        shm_offset& output_value) noexcept {

        output_value = 0;


        if (runtime.persisted_physical_value) {
            const auto& physical =
                runtime.persisted_links[
                    index];

            if (physical.source_program == 0 &&
                physical.target_program == 0) {

                return
                    resolve_persisted_direct_link(
                        index,
                        physical,
                        output_value);
            }
        }

        shm_runtime_v2::endpoint_program source;
        shm_runtime_v2::endpoint_program target;
        bool live = false;

        if (!runtime.link_program_at(
                index,
                source,
                target,
                live) ||
            !live) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        auto* state =
            runtime.link_state_at(
                index);

        if (state == nullptr) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        shm_offset target_slot = 0;

        if (!runtime.link_target_slot_at(
                index,
                target_slot) ||
            target_slot >=
                layout.size()) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        if (*state ==
            shm_runtime_v2::
                link_state::resolved) {

            std::uint64_t stored = 0;

            if (!read_reference(
                    target_slot,
                    stored) ||
                !runtime_address(
                    stored)) {
                return shm_runtime_v2_result::
                    invalid_input;
            }

            output_value =
                stored -
                base_address;

            return shm_runtime_v2_result::
                success;
        }

        if (*state ==
            shm_runtime_v2::
                link_state::resolving) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        if (*state !=
            shm_runtime_v2::
                link_state::marked) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        std::uint64_t stored = 0;

        if (!read_reference(
                target_slot,
                stored)) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        if (runtime_address(
                stored)) {

            *state =
                shm_runtime_v2::
                    link_state::resolved;

            output_value =
                stored -
                base_address;

            return shm_runtime_v2_result::
                success;
        }

        std::uint32_t raw_pending = 0;

        if (!decode_pending(
                stored,
                raw_pending) ||
            raw_pending !=
                index + 1) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        *state =
            shm_runtime_v2::
                link_state::resolving;

        shm_offset source_slot = 0;

        const auto resolved =
            source_position(
                source,
                source_slot);

        if (resolved !=
            shm_runtime_v2_result::
                success) {

            *state =
                shm_runtime_v2::
                    link_state::marked;

            return resolved;
        }

        if (source_slot >=
                layout.size() ||
            source_slot >
                mask -
                    base_address ||
            !write_word(
                target_slot,
                base_address +
                    source_slot)) {

            *state =
                shm_runtime_v2::
                    link_state::marked;

            return shm_runtime_v2_result::
                invalid_input;
        }

        *state =
            shm_runtime_v2::
                link_state::resolved;

        output_value =
            source_slot;

        if (telemetry != nullptr) {
            ++telemetry->
                links_resolved;
        }

        return shm_runtime_v2_result::
            success;
    }

    shm_runtime_v2& runtime;
    const server_abi_configuration& abi;
    const shm_layout& layout;
    std::span<std::byte> shm;
    shm_runtime_v2_link_telemetry* telemetry = nullptr;
    shm_runtime_v2_initialization_telemetry*
        initialization_telemetry = nullptr;

    abi_properties properties{};
    std::uint64_t base_address = 0;
    std::uint64_t mask = 0;
};



shm_runtime_v2_result
prepare_shm_runtime_v2_persisted(
    const runtime_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& output,
    shm_runtime_v2_prepare_telemetry* telemetry) noexcept {

    output = shm_runtime_v2{};

    if (layout.target() != abi.target) {
        return shm_runtime_v2_result::incompatible_abi;
    }

    const auto result =
        attach_shm_type_batch_physical_columns(
            project,
            layout,
            output.area,
            telemetry);

    if (result !=
        shm_type_batch_result::success) {

        output = shm_runtime_v2{};
        return result;
    }

    const auto attached =
        attach_shm_runtime_v2_physical_columns(
            project,
            output);

    if (attached !=
        shm_runtime_v2_result::
            success) {

        output =
            shm_runtime_v2{};

        if (telemetry != nullptr) {
            *telemetry = {};
        }

        return attached;
    }

    return shm_runtime_v2_result::
        success;
}

shm_runtime_v2_result prepare_shm_runtime_v2(
    const compiled_project_view& project,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    shm_runtime_v2& output,
    shm_runtime_v2_prepare_telemetry* telemetry) noexcept {

    output = shm_runtime_v2{};

    const auto result =
        prepare_shm_type_batch_inline64(
            project,
            abi,
            layout,
            output.area,
            telemetry);

    if (result ==
            shm_type_batch_result::success &&
        telemetry != nullptr &&
        telemetry->inline_leaf_limit !=
            shm_runtime_v2_inline_leaf_limit) {

        output = shm_runtime_v2{};
        *telemetry = {};
        return shm_type_batch_result::
            invalid_input;
    }

    if (result !=
        shm_type_batch_result::success) {

        output = shm_runtime_v2{};
        return result;
    }

    try {
        shm_runtime_v2_link_builder builder{
            project,
            abi,
            layout,
            output,
        };

        const auto links =
            builder.build();

        if (links !=
            shm_runtime_v2_result::
                success) {

            output =
                shm_runtime_v2{};

            if (telemetry != nullptr) {
                *telemetry = {};
            }

            return links;
        }
    }
    catch (...) {
        output =
            shm_runtime_v2{};

        if (telemetry != nullptr) {
            *telemetry = {};
        }

        return shm_runtime_v2_result::
            failed;
    }

    return shm_runtime_v2_result::
        success;
}

shm_runtime_v2_result
materialize_shm_runtime_v2_canonical(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry) noexcept {

    return materialize_shm_type_batch_canonical(
        runtime.area,
        abi,
        layout,
        shm,
        telemetry);
}

shm_runtime_v2_result
mark_shm_runtime_v2_links(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_link_telemetry* telemetry) noexcept {

    shm_runtime_v2_link_executor executor{
        runtime,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.mark();
}

shm_runtime_v2_result
mark_shm_runtime_v2_persisted_direct_target_range(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_offset target_begin,
    shm_offset target_end,
    shm_runtime_v2_link_telemetry* telemetry) noexcept {

    shm_runtime_v2_link_executor executor{
        runtime,
        abi,
        layout,
        shm,
        telemetry,
    };

    return
        executor.
            mark_persisted_direct_target_range(
                target_begin,
                target_end);
}

shm_runtime_v2_result
mark_shm_runtime_v2_persisted_complex_targets(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_link_telemetry* telemetry) noexcept {

    shm_runtime_v2_link_executor executor{
        runtime,
        abi,
        layout,
        shm,
        telemetry,
    };

    return
        executor.
            mark_persisted_complex_targets();
}

shm_runtime_v2_result
materialize_shm_runtime_v2_objects(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_execute_telemetry* telemetry) noexcept {

    return
        materialize_shm_type_batch_objects_object_major(
            runtime.area,
            abi,
            layout,
            shm,
            telemetry);
}

shm_runtime_v2_result
materialize_shm_runtime_v2_objects_range(
    const shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    std::size_t object_begin,
    std::size_t object_count,
    shm_runtime_v2_execute_telemetry* telemetry) noexcept {

    return
        materialize_shm_type_batch_objects_object_major_range(
            runtime.area,
            abi,
            layout,
            shm,
            object_begin,
            object_count,
            telemetry);
}

shm_runtime_v2_result
materialize_shm_runtime_v2_links(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_link_telemetry* telemetry) noexcept {

    shm_runtime_v2_link_executor executor{
        runtime,
        abi,
        layout,
        shm,
        telemetry,
    };

    return executor.materialize();
}

shm_runtime_v2_result
materialize_shm_runtime_v2_initializations(
    shm_runtime_v2& runtime,
    const server_abi_configuration& abi,
    const shm_layout& layout,
    std::span<std::byte> shm,
    shm_runtime_v2_initialization_telemetry*
        telemetry) noexcept {

    shm_runtime_v2_link_executor executor{
        runtime,
        abi,
        layout,
        shm,
        nullptr,
        telemetry,
    };

    return
        executor.
            materialize_initializations();
}

}
