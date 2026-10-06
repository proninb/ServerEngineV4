#include "project_load.hpp"

#include "project_lifecycle_context.hpp"
#include "runtime/project_runtime.hpp"
#include "persistence/compiled_project.hpp"
#include "persistence/runtime_project.hpp"
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
    std::unique_ptr<project>& output,
    project_runtime_telemetry* telemetry) {

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

    compiled.attach(
        mapping.bytes());

    read_only_file_mapping runtime_mapping;
    const auto runtime_opened = runtime_mapping.open(layout.runtime);

    if (runtime_opened != read_only_file_mapping_result::success) {
        diagnostics.emit(
            diagnostic(diagnostics::project_runtime_failed, operation)
                .file(layout.runtime)
                .detail(runtime_opened == read_only_file_mapping_result::not_found
                    ? "LOAD requires runtime.bin"
                    : "Cannot memory-map runtime.bin")
                .build());

        return runtime_opened == read_only_file_mapping_result::failed
            ? server_status::io_error
            : server_status::project_artifact_invalid;
    }

    runtime_project_view runtime_project;
    if (runtime_project.bind(runtime_mapping.bytes()) !=
        runtime_project_image_result::success) {
        diagnostics.emit(
            diagnostic(diagnostics::project_runtime_failed, operation)
                .file(layout.runtime)
                .detail("runtime.bin failed structural binding")
                .build());
        return server_status::project_artifact_invalid;
    }

    return create_resident_project(
        project_path,
        settings,
        operation,
        diagnostics,
        std::move(mapping),
        compiled,
        std::move(runtime_mapping),
        runtime_project,
        output,
        telemetry);
}

}
