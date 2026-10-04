#include "shm_runtime_v2.hpp"

#include "../abi/abi_layout.hpp"
#include "../persistence/compiled_project.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace cw::server {

class shm_runtime_v2_link_builder final {
public:
    shm_runtime_v2_link_builder(
        const compiled_project_view& project,
        const shm_layout& layout,
        shm_runtime_v2& output) noexcept
        : project(project),
          layout(layout),
          output(output) {
    }

    [[nodiscard]] shm_runtime_v2_result build() {

        output.links.clear();
        output.endpoint_dereferences.clear();
        output.live_link_count_value = 0;

        output.links.resize(
            project.link_count());

        for (std::size_t index = 0;
             index < project.link_count();
             ++index) {

            const auto handle =
                project.link_at(
                    index);

            if (!handle) {
                continue;
            }

            if (handle.value() !=
                index + 1) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            link_record link;

            if (!project.link(
                    handle,
                    link)) {

                return shm_runtime_v2_result::
                    invalid_input;
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
                    plan.target);

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

        return
            output.live_link_count_value ==
                project.live_link_count()
            ? shm_runtime_v2_result::success
            : shm_runtime_v2_result::
                invalid_input;
    }

private:
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
        type_ref& output_type) const noexcept {

        output_type = {};

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
            !layout.value(
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
        shm_runtime_v2::endpoint_program& program) {

        program = {};

        if (!endpoint.object ||
            endpoint.object.kind() !=
                identity_kind::object ||
            !endpoint.member) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        const auto object =
            project.find_object(
                endpoint.object);

        object_entry object_value;
        shm_offset object_offset = 0;

        if (!object ||
            !project.object(
                object,
                object_value) ||
            !layout.object_offset(
                object,
                object_offset)) {

            return shm_runtime_v2_result::
                invalid_input;
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

                case endpoint_path_step_kind::
                    dereference: {
                    type_ref referent;

                    if (step.value != 0 ||
                        !reference_referent(
                            current_type,
                            referent)) {

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
                ignored);

        return shm_runtime_v2_result::
            success;
    }

    const compiled_project_view& project;
    const shm_layout& layout;
    shm_runtime_v2& output;
};


class shm_runtime_v2_link_executor final {
public:
    shm_runtime_v2_link_executor(
        shm_runtime_v2& runtime,
        const server_abi_configuration& abi,
        const shm_layout& layout,
        std::span<std::byte> shm,
        shm_runtime_v2_link_telemetry* telemetry) noexcept
        : runtime(runtime),
          abi(abi),
          layout(layout),
          shm(shm),
          telemetry(telemetry) {
    }

    [[nodiscard]] shm_runtime_v2_result mark() noexcept {

        const auto valid =
            validate();

        if (valid !=
            shm_runtime_v2_result::success) {

            return valid;
        }

        publish_shape();

        for (std::size_t index = 0;
             index < runtime.links.size();
             ++index) {

            auto& plan =
                runtime.links[index];

            if (!plan.live) {
                continue;
            }

            if (plan.state !=
                shm_runtime_v2::
                    link_state::prepared ||
                !plan.target.final_reference) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            shm_offset target = 0;

            const auto located =
                endpoint_position(
                    plan.target,
                    false,
                    target);

            if (located !=
                shm_runtime_v2_result::
                    success) {

                return located;
            }

            std::uint64_t stored = 0;

            if (!read_reference(
                    target,
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
                    target,
                    pending_value(
                        static_cast<
                            std::uint32_t>(
                                raw)))) {

                return shm_runtime_v2_result::
                    incompatible_abi;
            }

            plan.target_slot =
                target;

            plan.resolved_source =
                invalid_offset();

            plan.state =
                shm_runtime_v2::
                    link_state::marked;

            if (telemetry != nullptr) {
                ++telemetry->targets_marked;
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
            runtime.endpoint_dereferences.size();
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
                runtime.links.size()) {

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

        if (program.root >=
                layout.size() ||
            program.dereference_begin >
                runtime.
                    endpoint_dereferences.
                    size() ||
            program.dereference_count >
                runtime.
                    endpoint_dereferences.
                    size() -
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

            const auto delta =
                runtime.
                    endpoint_dereferences[
                        static_cast<std::size_t>(
                            program.
                                dereference_begin) +
                        index];

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
    resolve_link(
        std::size_t index,
        shm_offset& output_value) noexcept {

        output_value = 0;

        if (index >=
            runtime.links.size()) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        auto& plan =
            runtime.links[index];

        if (!plan.live) {
            return shm_runtime_v2_result::
                invalid_input;
        }

        if (plan.state ==
            shm_runtime_v2::
                link_state::resolved) {

            if (plan.resolved_source ==
                invalid_offset()) {

                return shm_runtime_v2_result::
                    invalid_input;
            }

            output_value =
                plan.resolved_source;

            return shm_runtime_v2_result::
                success;
        }

        if (plan.state ==
            shm_runtime_v2::
                link_state::resolving) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        if (plan.state !=
            shm_runtime_v2::
                link_state::marked) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        std::uint64_t stored = 0;

        if (!read_reference(
                plan.target_slot,
                stored)) {

            return shm_runtime_v2_result::
                invalid_input;
        }

        if (runtime_address(
                stored)) {

            plan.resolved_source =
                stored -
                base_address;

            plan.state =
                shm_runtime_v2::
                    link_state::resolved;

            output_value =
                plan.resolved_source;

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

        plan.state =
            shm_runtime_v2::
                link_state::resolving;

        shm_offset source = 0;

        const auto resolved =
            source_position(
                plan.source,
                source);

        if (resolved !=
            shm_runtime_v2_result::
                success) {

            plan.state =
                shm_runtime_v2::
                    link_state::marked;

            return resolved;
        }

        if (source >=
                layout.size() ||
            source >
                mask -
                    base_address ||
            !write_word(
                plan.target_slot,
                base_address +
                    source)) {

            plan.state =
                shm_runtime_v2::
                    link_state::marked;

            return shm_runtime_v2_result::
                invalid_input;
        }

        plan.resolved_source =
            source;

        plan.state =
            shm_runtime_v2::
                link_state::resolved;

        output_value =
            source;

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

    abi_properties properties{};
    std::uint64_t base_address = 0;
    std::uint64_t mask = 0;
};


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

}
