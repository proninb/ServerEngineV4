#include "compiled_project_build.hpp"

#include "../../filesystem_path.hpp"

#include <cstdint>
#include <limits>

namespace cw::server {

compiled_project_image_result
prepare_build_compiled_project_layout(
    const string_table& strings,
    const identity_space& identities,
    const graph_delta& G,
    const graph_dense_projection& projection,
    const assign_overlay_view& assigns,
    const file_context& files,
    const source_map_overlay_view& sources,
    compiled_project_layout& output) noexcept {

    output = {};

    if (!G.baseline_bound() ||
        !projection.prepared_for(G) ||
        !assigns.valid() ||
        !sources.valid() ||
        identities.size() == 0) {

        return compiled_project_image_result::
            invalid_state;
    }

    std::uint64_t source_path_bytes = 0;
    std::uint64_t source_contributions = 0;

    for (std::size_t index = 0;
         index < files.size();
         ++index) {

        const file_id file{
            static_cast<std::uint32_t>(
                index + 1)};

        std::size_t length = 0;

        if (filesystem_path_utf8_size(
                files.path(file),
                length) !=
                filesystem_path_result::
                    success ||
            length >
                (std::numeric_limits<
                    std::uint32_t>::max)() ||
            source_path_bytes >
                (std::numeric_limits<
                    std::uint32_t>::max)() -
                    length) {

            return compiled_project_image_result::
                invalid_state;
        }

        source_path_bytes +=
            length;

        std::size_t contribution_count = 0;

        if (!sources.contributions(
                file,
                contribution_count) ||
            contribution_count >
                (std::numeric_limits<
                    std::uint32_t>::max)() -
                    source_contributions) {

            return compiled_project_image_result::
                invalid_state;
        }

        source_contributions +=
            contribution_count;
    }

    const compiled_project_layout::
        preparation_counts counts{
            static_cast<std::uint64_t>(
                strings.size()),
            static_cast<std::uint64_t>(
                strings.byte_size()),
            static_cast<std::uint64_t>(
                identities.size()),
            static_cast<std::uint64_t>(
                projection.type_count()),
            static_cast<std::uint64_t>(
                projection.member_count()),
            static_cast<std::uint64_t>(
                projection.base_count()),
            static_cast<std::uint64_t>(
                projection.derived_type_count()),
            static_cast<std::uint64_t>(
                projection.object_count()),
            static_cast<std::uint64_t>(
                projection.object_construction_count()),
            static_cast<std::uint64_t>(
                projection.link_count()),
            static_cast<std::uint64_t>(
                G.initialization_count()),
            static_cast<std::uint64_t>(
                projection.endpoint_path_count()),
            static_cast<std::uint64_t>(
                projection.endpoint_path_step_count()),
            static_cast<std::uint64_t>(
                assigns.size()),
            static_cast<std::uint64_t>(
                assigns.byte_size()),
            source_contributions,
            static_cast<std::uint64_t>(
                files.size()),
            source_path_bytes,
        };

    return compiled_project_layout::
        prepare_counts(
            counts,
            output);
}

}
