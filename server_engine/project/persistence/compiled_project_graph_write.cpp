#include "compiled_project_build.hpp"

#include <limits>

namespace cw::server {

compiled_project_image_result
prepare_compiled_project_graph_write_plan(
    const graph_delta& G,
    compiled_project_graph_write_plan& output) noexcept {

    output = {};

    const auto make_slot_append =
        [](
            std::size_t total,
            std::size_t appended,
            std::uint32_t maximum,
            compiled_project_slot_append& range) noexcept {

            range = {};

            if (appended > total ||
                total > maximum ||
                appended >
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {

                return false;
            }

            if (appended == 0) {
                return true;
            }

            const auto baseline =
                total - appended;

            if (baseline >= maximum) {
                return false;
            }

            range.first_slot =
                static_cast<std::uint32_t>(
                    baseline + 1);

            range.count =
                static_cast<std::uint32_t>(
                    appended);

            return true;
        };

    const auto make_index_append =
        [](
            std::size_t total,
            std::size_t appended,
            compiled_project_index_append& range) noexcept {

            range = {};

            if (appended > total ||
                total >
                    (std::numeric_limits<
                        std::uint32_t>::max)() ||
                appended >
                    (std::numeric_limits<
                        std::uint32_t>::max)()) {

                return false;
            }

            range.begin =
                static_cast<std::uint32_t>(
                    total - appended);

            range.count =
                static_cast<std::uint32_t>(
                    appended);

            return true;
        };

    const auto type_append =
        G.type_entries();

    const auto type_identity_append =
        G.type_identity_entries();

    const auto member_append =
        G.member_entries();

    const auto member_construction_append =
        G.member_construction_entries();

    const auto base_append =
        G.base_entries();

    const auto object_append =
        G.object_entries();

    const auto object_identity_append =
        G.object_identity_entries();

    const auto object_construction_append =
        G.object_construction_entries();

    const auto link_append =
        G.link_entries();

    const auto derived_append =
        G.derived_type_entries();

    const auto endpoint_path_append =
        G.endpoint_path_entries();

    const auto endpoint_path_step_append =
        G.endpoint_path_step_entries();

    if (type_append.size() !=
            type_identity_append.size() ||
        member_append.size() !=
            member_construction_append.size() ||
        object_append.size() !=
            object_identity_append.size() ||
        G.type_patch_count() >
            (std::numeric_limits<
                std::uint32_t>::max)() ||
        G.object_patch_count() >
            (std::numeric_limits<
                std::uint32_t>::max)() ||
        G.link_patch_count() >
            (std::numeric_limits<
                std::uint32_t>::max)() ||
        G.initialization_change_count() >
            (std::numeric_limits<
                std::uint32_t>::max)()) {

        return compiled_project_image_result::
            invalid_state;
    }

    if (!make_slot_append(
            G.type_count(),
            type_append.size(),
            type_handle::maximum_slot,
            output.appended_types) ||
        !make_index_append(
            G.member_count(),
            member_append.size(),
            output.appended_members) ||
        !make_index_append(
            G.base_count(),
            base_append.size(),
            output.appended_bases) ||
        !make_slot_append(
            G.object_count(),
            object_append.size(),
            object_handle::maximum_slot,
            output.appended_objects) ||
        !make_slot_append(
            G.object_construction_count(),
            object_construction_append.size(),
            graph_object_construction_slot_mask,
            output.appended_object_construction) ||
        !make_slot_append(
            G.link_count(),
            link_append.size(),
            link_handle::maximum_slot,
            output.appended_links) ||
        !make_slot_append(
            G.derived_type_count(),
            derived_append.size(),
            type_ref::maximum_payload,
            output.appended_derived_types) ||
        !make_slot_append(
            G.endpoint_path_count(),
            endpoint_path_append.size(),
            endpoint_path_handle::maximum_slot,
            output.appended_endpoint_paths) ||
        !make_index_append(
            G.endpoint_path_step_count(),
            endpoint_path_step_append.size(),
            output.appended_endpoint_path_steps)) {

        output = {};
        return compiled_project_image_result::
            invalid_state;
    }

    output.type_patch_count =
        static_cast<std::uint32_t>(
            G.type_patch_count());

    output.object_patch_count =
        static_cast<std::uint32_t>(
            G.object_patch_count());

    output.link_patch_count =
        static_cast<std::uint32_t>(
            G.link_patch_count());

    output.initialization_change_count =
        static_cast<std::uint32_t>(
            G.initialization_change_count());

    std::size_t payload_bytes = 0;

    const auto add_bytes =
        [&payload_bytes](
            std::size_t count,
            std::size_t record_size) noexcept {

            if (count != 0 &&
                record_size >
                    (std::numeric_limits<
                        std::size_t>::max)() /
                        count) {

                return false;
            }

            const auto bytes =
                count * record_size;

            if (payload_bytes >
                (std::numeric_limits<
                    std::size_t>::max)() -
                    bytes) {

                return false;
            }

            payload_bytes +=
                bytes;

            return true;
        };

    if (!add_bytes(
            output.type_patch_count,
            sizeof(type_entry)) ||
        !add_bytes(
            output.object_patch_count,
            sizeof(object_entry)) ||
        !add_bytes(
            output.link_patch_count,
            sizeof(link_record)) ||
        !add_bytes(
            output.initialization_change_count,
            sizeof(object_initialization_record)) ||
        !add_bytes(
            type_append.size(),
            sizeof(type_entry) +
                sizeof(identity_ref)) ||
        !add_bytes(
            member_append.size(),
            sizeof(member_record) +
                sizeof(construction_value)) ||
        !add_bytes(
            base_append.size(),
            sizeof(base_record)) ||
        !add_bytes(
            object_append.size(),
            sizeof(object_entry) +
                sizeof(identity_ref)) ||
        !add_bytes(
            object_construction_append.size(),
            sizeof(construction_value)) ||
        !add_bytes(
            link_append.size(),
            sizeof(link_record)) ||
        !add_bytes(
            derived_append.size(),
            sizeof(derived_type_record)) ||
        !add_bytes(
            endpoint_path_append.size(),
            sizeof(endpoint_path_record)) ||
        !add_bytes(
            endpoint_path_step_append.size(),
            sizeof(endpoint_path_step))) {

        output = {};
        return compiled_project_image_result::
            failed;
    }

    output.graph_payload_bytes =
        payload_bytes;

    return compiled_project_image_result::
        success;
}

}
