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
            root_project_path.filename().empty() ||
            files.manifest.empty() ||
            files.source_save.empty() ||
            files.database.empty() ||
            files.compiled.empty()) {

            return project_artifact_layout_result::failed;
        }

        output.root =
            root_project_path.parent_path() /
            ".serverengine" /
            root_project_path.filename();

        output.manifest = output.root / files.manifest;
        output.source_save = output.root / files.source_save;
        output.database = output.root / files.database;
        output.compiled = output.root / files.compiled;

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
