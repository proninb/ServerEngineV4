#include "project_rebuild.hpp"

#include "project_lifecycle_context.hpp"
#include "assign/assign_input.hpp"
#include "frontend/source_discovery.hpp"
#include "parser/parser.hpp"
#include "persistence/compiled_project.hpp"
#include "persistence/project_artifact.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../writable_file_mapping.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <string>
#include <string_view>

namespace cw::server {
namespace {

[[nodiscard]] server_status emit_source_failure(
    file_context& files,
    file_id file,
    source_range range,
    const diagnostic_descriptor& descriptor,
    std::string_view detail,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!file ||
        !files.contains(file) ||
        !files.content_available(file)) {

        return server_status::success;
    }

    try {
        const auto path_view =
            files.path(file);

        const std::filesystem::path path{
            path_view.begin(),
            path_view.end()};

        const auto source =
            files.content(file);

        const auto diagnostic_file =
            diagnostics.add_source(
                path,
                std::string{
                    source.data(),
                    source.size()});

        const auto location =
            diagnostics.locate(
                diagnostic_file,
                range.offset,
                range.length);

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .location(location)
                .detail(detail)
                .build());

        return server_status::success;
    }
    catch (...) {
        return server_status::io_error;
    }
}

}

server_status rebuild_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    output.reset();

    project_artifact_layout layout;

    if (make_project_artifact_layout(
            project_path,
            settings.files,
            layout) !=
        project_artifact_layout_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_rebuild_incomplete,
                operation)
                .file(project_path)
                .detail(
                    "Cannot construct Project artifact layout")
                .build());

        return server_status::io_error;
    }

    if (remove_project_artifacts(
            layout) !=
        project_artifact_io_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_rebuild_cleanup_failed,
                operation)
                .file(layout.root)
                .detail(
                    "REBUILD could not remove the complete persisted artifact set before starting the fresh lineage")
                .build());

        return server_status::io_error;
    }

    const auto rebuilt =
        [&]() -> server_status {

    rebuild_context context{
        settings};

    // Capture before file acquisition. Any later physical change is visible to
    // the next BUILD journal scan; no journal support is only an acceleration
    // fallback.
    (void)capture_file_change_checkpoint(
        project_path,
        context.change_checkpoint);

    const auto composed =
        compose_project_configuration(
            project_path,
            operation,
            diagnostics,
            context.manifest,
            context.files,
            context.preprocessor);

    if (!succeeded(composed)) {
        return composed;
    }

    const auto frontend_root_count =
        context.files.size();

    source_preparation_failure failure;

    const auto prepared_sources =
        prepare_source_lexical_state(
            context.files,
            context.lexical,
            &failure);

    if (!succeeded(prepared_sources)) {
        if (failure.kind ==
                source_preparation_failure_kind::lexical &&
            failure.file) {

            const source_range range{
                failure.lexical.offset,
                failure.lexical.length,
            };

            const auto emitted =
                emit_source_failure(
                    context.files,
                    failure.file,
                    range,
                    diagnostics::project_lexical_error,
                    lexical_error_message(
                        failure.lexical.reason),
                    operation,
                    diagnostics);

            return succeeded(emitted)
                ? prepared_sources
                : emitted;
        }

        return prepared_sources;
    }

    const auto assignments_materialized =
        materialize_assign_inputs(
            context.files);

    if (!succeeded(
            assignments_materialized)) {

        return assignments_materialized;
    }

    assign_parse_failure assign_failure;

    const auto assignments_parsed =
        parse_assign_inputs(
            context.files,
            context.assigns,
            &assign_failure);

    if (!succeeded(assignments_parsed)) {
        if (assign_failure.file) {
            const source_range range{
                assign_failure.offset,
                assign_failure.length,
            };

            const auto emitted =
                emit_source_failure(
                    context.files,
                    assign_failure.file,
                    range,
                    diagnostics::project_assign_invalid,
                    assign_failure.detail.empty()
                        ? std::string_view{
                            "Assign input is invalid"}
                        : assign_failure.detail,
                    operation,
                    diagnostics);

            return succeeded(emitted)
                ? assignments_parsed
                : emitted;
        }

        return assignments_parsed;
    }

    parser_failure semantic_failure;

    const auto parsed =
        parse_semantic_project(
            context.files,
            context.lexical,
            frontend_root_count,
            context.preprocessor,
            context.strings,
            context.identities,
            context.G,
            &semantic_failure);

    if (!succeeded(parsed)) {
        if (semantic_failure.file) {
            const auto emitted =
                emit_source_failure(
                    context.files,
                    semantic_failure.file,
                    semantic_failure.source,
                    semantic_failure.kind ==
                            parser_failure_kind::
                                lexical
                        ? diagnostics::
                            project_lexical_error
                        : semantic_failure.kind ==
                                parser_failure_kind::
                                    preprocessing
                            ? diagnostics::
                                project_preprocessing_error
                            : diagnostics::
                                project_semantic_error,
                    semantic_failure.detail.empty()
                        ? std::string_view{
                            "Parser/Semantic construction failed"}
                        : semantic_failure.detail,
                    operation,
                    diagnostics);

            return succeeded(emitted)
                ? parsed
                : emitted;
        }

        return parsed;
    }

    const auto topology_finalized =
        context.files.
            finalize_dependency_topology();

    if (!succeeded(topology_finalized)) {
        return topology_finalized;
    }

    compiled_project_layout compiled_layout;

    const auto prepared =
        prepare_compiled_project_layout(
            context.strings,
            context.identities,
            context.G,
            context.assigns,
            compiled_layout);

    if (prepared !=
        compiled_project_image_result::success) {

        return prepared ==
                compiled_project_image_result::failed
            ? server_status::io_error
            : server_status::
                project_artifact_invalid;
    }

    if (ensure_project_artifact_directory(
            layout) !=
        project_artifact_io_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_io_failed,
                operation)
                .file(layout.compiled)
                .detail(
                    "Cannot create the Project artifact directory")
                .build());

        return server_status::io_error;
    }

    writable_file_mapping compiled_mapping;

    if (compiled_mapping.create(
            layout.compiled,
            compiled_layout.size()) !=
        writable_file_mapping_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_io_failed,
                operation)
                .file(layout.compiled)
                .detail(
                    "Cannot create and memory-map compiled.bin for direct REBUILD encoding")
                .build());

        return server_status::io_error;
    }

    const auto compiled =
        encode_compiled_project_image(
            context.strings,
            context.identities,
            context.G,
            context.assigns,
            compiled_layout,
            compiled_mapping.bytes());

    if (compiled !=
        compiled_project_image_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Direct compiled.bin encoding failed")
                .build());

        return compiled ==
                compiled_project_image_result::failed
            ? server_status::io_error
            : server_status::
                project_artifact_invalid;
    }

    compiled_project_view compiled_view;

    if (compiled_view.bind(
            compiled_mapping.bytes()) !=
            compiled_project_image_result::success ||
        compiled_view.verify_contents() !=
            compiled_project_image_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Direct compiled.bin image failed structural or cold semantic validation")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    if (compiled_mapping.flush() !=
        writable_file_mapping_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_io_failed,
                operation)
                .file(layout.compiled)
                .detail(
                    "Cannot flush direct compiled.bin mapping")
                .build());

        return server_status::io_error;
    }

    // REBUILD is not yet a successful lifecycle operation. cleanup therefore
    // removes project.manifest/source.bin/database.bin/compiled.bin on return.
    diagnostics.emit(
        diagnostic(
            diagnostics::project_rebuild_incomplete,
            operation)
            .detail(
                "Direct writable-mmap compiled.bin construction from final G is complete for the supported semantic slice; remaining Phase-1 BUILD/LOAD/REBUILD completion and direct persistence of BUILD-lineage artifacts are not implemented yet")
            .build());

    return server_status::unsupported;
        }();

    if (succeeded(rebuilt)) {
        return rebuilt;
    }

    // The operation scope above is already destroyed here, including any open
    // artifact mappings. Failed REBUILD must leave no persisted Project state.
    if (remove_project_artifacts(
            layout) !=
        project_artifact_io_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_rebuild_cleanup_failed,
                operation)
                .file(layout.root)
                .detail(
                    "Failed REBUILD could not remove the complete persisted artifact set")
                .build());

        return server_status::io_error;
    }

    return rebuilt;
}

}
