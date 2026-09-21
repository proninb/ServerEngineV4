#include "project_artifact.hpp"

#include <string_view>

namespace cw::server {

project_artifact_layout_result make_project_artifact_layout(
    const std::filesystem::path& root_project_path,
    const server_files_configuration& files,
    project_artifact_layout& output) noexcept {

    output = {};

    try {
        if (root_project_path.empty() ||
            root_project_path.filename().empty()) {

            return project_artifact_layout_result::failed;
        }

        output.root =
            root_project_path.parent_path() /
            ".serverengine" /
            root_project_path.filename();

        output.baseline =
            output.root /
            files.baseline;

        constexpr std::array<const char*, 2> names{
            "slot0",
            "slot1",
        };

        for (std::size_t index = 0;
             index < output.slots.size();
             ++index) {

            auto& slot =
                output.slots[index];

            slot.directory =
                output.root /
                names[index];

            slot.manifest =
                slot.directory /
                files.manifest;

            slot.source_save =
                slot.directory /
                files.source_save;

            slot.database =
                slot.directory /
                files.database;

            slot.compiled =
                slot.directory /
                files.compiled;
        }

        return project_artifact_layout_result::success;
    }
    catch (...) {
        output = {};
        return project_artifact_layout_result::failed;
    }
}

void finalize_project_artifact_image(
    project_artifact_image& image) noexcept {

    image.hash =
        hash_file_content(
            std::string_view{
                reinterpret_cast<const char*>(
                    image.bytes.data()),
                image.bytes.size()});
}

}

