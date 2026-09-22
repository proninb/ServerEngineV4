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

project_artifact_io_result
ensure_project_artifact_directory(
    const project_artifact_layout& layout) noexcept {

    if (layout.root.empty()) {
        return project_artifact_io_result::
            failed;
    }

    try {
        std::error_code error;

        std::filesystem::create_directories(
            layout.root,
            error);

        if (error) {
            return project_artifact_io_result::
                failed;
        }

        const auto directory =
            std::filesystem::is_directory(
                layout.root,
                error);

        return !error &&
            directory
            ? project_artifact_io_result::
                success
            : project_artifact_io_result::
                failed;
    }
    catch (...) {
        return project_artifact_io_result::
            failed;
    }
}

project_artifact_io_result
remove_project_artifacts(
    const project_artifact_layout& layout) noexcept {

    bool failed = false;

    const std::filesystem::path* paths[]{
        &layout.manifest,
        &layout.source_save,
        &layout.database,
        &layout.compiled,
    };

    for (const auto* path : paths) {
        if (path == nullptr ||
            path->empty()) {

            failed = true;
            continue;
        }

        try {
            std::error_code error;

            (void)std::filesystem::remove(
                *path,
                error);

            if (error) {
                failed = true;
            }
        }
        catch (...) {
            failed = true;
        }
    }

    return failed
        ? project_artifact_io_result::failed
        : project_artifact_io_result::success;
}

}
