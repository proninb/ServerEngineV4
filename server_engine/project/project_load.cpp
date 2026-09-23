#include "project_load.hpp"

#include "project_lifecycle_context.hpp"
#include "persistence/compiled_project.hpp"
#include "persistence/project_artifact.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"
#include "../read_only_file_mapping.hpp"

#include <memory>
#include <utility>

namespace cw::server {

server_status load_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    output.reset();

    load_context context{
        settings};

    project_artifact_layout layout;

    if (make_project_artifact_layout(
            project_path,
            context.settings.files,
            layout) !=
            project_artifact_layout_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_load_failed,
                operation)
                .file(project_path)
                .detail(
                    "Cannot construct Project artifact layout")
                .build());

        return server_status::io_error;
    }

    read_only_file_mapping mapping;

    const auto opened =
        mapping.open(
            layout.compiled);

    if (opened ==
        read_only_file_mapping_result::failed) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_io_failed,
                operation)
                .file(layout.compiled)
                .detail(
                    "Cannot memory-map compiled.bin")
                .build());

        return server_status::io_error;
    }

    if (opened !=
        read_only_file_mapping_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    opened ==
                        read_only_file_mapping_result::not_found
                    ? "LOAD requires compiled.bin"
                    : "compiled.bin is empty")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    compiled_project_view compiled;

    const auto bound =
        compiled.bind(
            mapping.bytes());

    if (bound !=
        compiled_project_image_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "compiled.bin structural bind failed")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    try {
        output =
            std::make_unique<project>(
                project_path,
                std::move(mapping),
                compiled);
    }
    catch (...) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_load_failed,
                operation)
                .file(layout.compiled)
                .detail(
                    "Cannot publish resident Project from compiled.bin")
                .build());

        return server_status::
            project_load_failed;
    }

    return server_status::success;
}

}
