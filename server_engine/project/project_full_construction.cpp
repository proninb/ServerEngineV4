#include "project_full_construction.hpp"

#include "project_lifecycle_context.hpp"
#include "project_configuration_manifest_store.hpp"
#include "runtime/project_runtime.hpp"
#include "assign/assign_input.hpp"
#include "construction/execution_lanes.hpp"
#include "frontend/source_discovery.hpp"
#include "parser/parser.hpp"
#include "persistence/compiled_project.hpp"
#include "persistence/database.hpp"
#include "persistence/project_artifact.hpp"
#include "persistence/source_save.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../read_only_file_mapping.hpp"
#include "../writable_file_mapping.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

enum class full_persistence_status : std::uint8_t {
    pending,
    success,
    io_failed,
    invalid,
};

enum class full_persistence_stage : std::uint8_t {
    none,
    prepare,
    create,
    encode,
    validate,
    flush,
    dispatch,
};

struct full_persistence_result final {
    full_persistence_status status =
        full_persistence_status::pending;

    full_persistence_stage stage =
        full_persistence_stage::none;
};

enum class full_persistence_artifact : std::size_t {
    compiled,
    source,
    database,
    manifest,
    count,
};

inline constexpr std::size_t
    full_persistence_artifact_count =
        static_cast<std::size_t>(
            full_persistence_artifact::count);

[[nodiscard]] constexpr full_persistence_result
persistence_success() noexcept {

    return {
        full_persistence_status::success,
        full_persistence_stage::none,
    };
}

[[nodiscard]] constexpr full_persistence_result
persistence_io_failure(
    full_persistence_stage stage) noexcept {

    return {
        full_persistence_status::io_failed,
        stage,
    };
}

[[nodiscard]] constexpr full_persistence_result
persistence_invalid(
    full_persistence_stage stage) noexcept {

    return {
        full_persistence_status::invalid,
        stage,
    };
}

[[nodiscard]] full_persistence_result
persist_compiled(
    const project_artifact_layout& paths,
    const full_construction_context& context) noexcept {

    compiled_project_layout layout;

    const auto prepared =
        prepare_compiled_project_layout(
            context.strings,
            context.identities,
            context.G,
            context.assigns,
            context.files,
            context.sources,
            layout);

    if (prepared !=
        compiled_project_image_result::success) {

        return prepared ==
                compiled_project_image_result::failed
            ? persistence_io_failure(
                full_persistence_stage::prepare)
            : persistence_invalid(
                full_persistence_stage::prepare);
    }

    writable_file_mapping mapping;

    if (mapping.create(
            paths.compiled,
            layout.size()) !=
        writable_file_mapping_result::success) {

        return persistence_io_failure(
            full_persistence_stage::create);
    }

    const auto encoded =
        encode_compiled_project_image(
            context.strings,
            context.identities,
            context.G,
            context.assigns,
            context.files,
            context.sources,
            layout,
            mapping.bytes());

    if (encoded !=
        compiled_project_image_result::success) {

        return encoded ==
                compiled_project_image_result::failed
            ? persistence_io_failure(
                full_persistence_stage::encode)
            : persistence_invalid(
                full_persistence_stage::encode);
    }

    // Encoding already ends with a structural bind. Full semantic/CRC audit
    // belongs to explicit artifact verification, not to every fresh publish.

    if (mapping.flush() !=
        writable_file_mapping_result::success) {

        return persistence_io_failure(
            full_persistence_stage::flush);
    }

    return persistence_success();
}

[[nodiscard]] full_persistence_result
persist_source_save(
    const project_artifact_layout& paths,
    const full_construction_context& context) noexcept {

    source_save_layout layout;

    const source_save_build_options options{
        context.change_checkpoint};

    const auto prepared =
        prepare_source_save_layout(
            context.files,
            context.sources,
            options,
            layout);

    if (prepared !=
        source_save_result::success) {

        return prepared ==
                source_save_result::failed
            ? persistence_io_failure(
                full_persistence_stage::prepare)
            : persistence_invalid(
                full_persistence_stage::prepare);
    }

    writable_file_mapping mapping;

    if (mapping.create(
            paths.source_save,
            layout.size()) !=
        writable_file_mapping_result::success) {

        return persistence_io_failure(
            full_persistence_stage::create);
    }

    const auto encoded =
        encode_source_save_image(
            context.files,
            context.sources,
            layout,
            mapping.bytes());

    if (encoded !=
        source_save_result::success) {

        return encoded ==
                source_save_result::failed
            ? persistence_io_failure(
                full_persistence_stage::encode)
            : persistence_invalid(
                full_persistence_stage::encode);
    }

    const auto validated =
        validate_source_save_image(
            mapping.bytes());

    if (validated !=
        source_save_result::success) {

        return validated ==
                source_save_result::failed
            ? persistence_io_failure(
                full_persistence_stage::validate)
            : persistence_invalid(
                full_persistence_stage::validate);
    }

    if (mapping.flush() !=
        writable_file_mapping_result::success) {

        return persistence_io_failure(
            full_persistence_stage::flush);
    }

    return persistence_success();
}

[[nodiscard]] full_persistence_result
persist_database(
    const project_artifact_layout& paths,
    const full_construction_context& context) noexcept {

    database_layout layout;

    const auto prepared =
        prepare_database_layout(
            context.files,
            context.lexical,
            layout);

    if (prepared !=
        database_image_result::success) {

        return prepared ==
                database_image_result::failed
            ? persistence_io_failure(
                full_persistence_stage::prepare)
            : persistence_invalid(
                full_persistence_stage::prepare);
    }

    writable_file_mapping mapping;

    if (mapping.create(
            paths.database,
            layout.size()) !=
        writable_file_mapping_result::success) {

        return persistence_io_failure(
            full_persistence_stage::create);
    }

    const auto encoded =
        encode_database_image(
            context.files,
            context.lexical,
            layout,
            mapping.bytes());

    if (encoded !=
        database_image_result::success) {

        return encoded ==
                database_image_result::failed
            ? persistence_io_failure(
                full_persistence_stage::encode)
            : persistence_invalid(
                full_persistence_stage::encode);
    }

    const auto validated =
        verify_database_image(
            mapping.bytes(),
            context.files,
            context.lexical);

    if (validated !=
        database_image_result::success) {

        return validated ==
                database_image_result::failed
            ? persistence_io_failure(
                full_persistence_stage::validate)
            : persistence_invalid(
                full_persistence_stage::validate);
    }

    if (mapping.flush() !=
        writable_file_mapping_result::success) {

        return persistence_io_failure(
            full_persistence_stage::flush);
    }

    return persistence_success();
}

[[nodiscard]] full_persistence_result
persist_manifest(
    const project_artifact_layout& paths,
    const full_construction_context& context) noexcept {

    project_configuration_manifest_layout layout;

    const auto prepared =
        prepare_project_configuration_manifest_layout(
            context.manifest,
            layout);

    if (prepared !=
        project_configuration_manifest_store_result::success) {

        return prepared ==
                project_configuration_manifest_store_result::io_failed
            ? persistence_io_failure(
                full_persistence_stage::prepare)
            : persistence_invalid(
                full_persistence_stage::prepare);
    }

    writable_file_mapping mapping;

    if (mapping.create(
            paths.manifest,
            layout.size()) !=
        writable_file_mapping_result::success) {

        return persistence_io_failure(
            full_persistence_stage::create);
    }

    const auto encoded =
        encode_project_configuration_manifest(
            context.manifest,
            layout,
            mapping.bytes());

    if (encoded !=
        project_configuration_manifest_store_result::success) {

        return encoded ==
                project_configuration_manifest_store_result::io_failed
            ? persistence_io_failure(
                full_persistence_stage::encode)
            : persistence_invalid(
                full_persistence_stage::encode);
    }

    if (mapping.flush() !=
        writable_file_mapping_result::success) {

        return persistence_io_failure(
            full_persistence_stage::flush);
    }

    return persistence_success();
}

struct full_persistence_job final {
    const project_artifact_layout& paths;
    const full_construction_context& context;

    std::size_t active_lanes = 1;
    std::size_t artifact_count =
        full_persistence_artifact_count;

    std::array<
        full_persistence_result,
        full_persistence_artifact_count>
        results{};
};

void persist_full_artifacts(
    void* value,
    std::size_t lane) noexcept {

    auto& job =
        *static_cast<full_persistence_job*>(
            value);

    for (std::size_t index = lane;
         index < job.artifact_count;
         index += job.active_lanes) {

        switch (
            static_cast<full_persistence_artifact>(
                index)) {

        case full_persistence_artifact::compiled:
            job.results[index] =
                persist_compiled(
                    job.paths,
                    job.context);
            break;

        case full_persistence_artifact::source:
            job.results[index] =
                persist_source_save(
                    job.paths,
                    job.context);
            break;

        case full_persistence_artifact::database:
            job.results[index] =
                persist_database(
                    job.paths,
                    job.context);
            break;

        case full_persistence_artifact::manifest:
            job.results[index] =
                persist_manifest(
                    job.paths,
                    job.context);
            break;

        case full_persistence_artifact::count:
            break;
        }
    }
}

[[nodiscard]] std::string_view
persistence_stage_detail(
    full_persistence_stage stage) noexcept {

    switch (stage) {
    case full_persistence_stage::prepare:
        return "BUILD-acceleration artifact layout preparation failed";
    case full_persistence_stage::create:
        return "BUILD-acceleration artifact writable mapping creation failed";
    case full_persistence_stage::encode:
        return "BUILD-acceleration artifact direct encoding failed";
    case full_persistence_stage::validate:
        return "BUILD-acceleration artifact cold validation failed";
    case full_persistence_stage::flush:
        return "BUILD-acceleration artifact flush failed";
    case full_persistence_stage::dispatch:
        return "BUILD-acceleration artifact parallel persistence dispatch failed";
    case full_persistence_stage::none:
        break;
    }

    return "BUILD-acceleration artifact persistence failed";
}

void emit_build_cache_warning(
    const full_persistence_result& result,
    const std::filesystem::path& path,
    const diagnostic_descriptor& io_descriptor,
    const diagnostic_descriptor& invalid_descriptor,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (result.status ==
        full_persistence_status::success) {

        return;
    }

    const auto& descriptor =
        result.status ==
                full_persistence_status::invalid
            ? invalid_descriptor
            : io_descriptor;

    diagnostics.emit(
        diagnostic(
            descriptor,
            operation)
            .severity(
                diagnostic_severity::warning)
            .file(path)
            .detail(
                persistence_stage_detail(
                    result.stage))
            .build());
}

}

server_status construct_full_project(
    const std::filesystem::path& project_path,
    const server_settings_configuration& settings,
    full_construction_mode mode,
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
                diagnostics::project_invalid_configuration,
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

        const auto& descriptor =
            mode == full_construction_mode::publish
            ? diagnostics::project_publish_cleanup_failed
            : diagnostics::project_rebuild_cleanup_failed;

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .file(layout.root)
                .detail(
                    mode == full_construction_mode::publish
                    ? "PUBLISH could not remove the previous artifact set before full source construction"
                    : "REBUILD could not remove the complete persisted artifact set before starting the fresh lineage")
                .build());

        return server_status::io_error;
    }

    const auto rebuilt =
        [&]() -> server_status {

    full_construction_context context{
        settings};

    // Only REBUILD creates a reusable BUILD lineage. PUBLISH deliberately
    // performs no BUILD-acceleration capture.
    if (mode ==
        full_construction_mode::rebuild) {

        // Capture before file acquisition. Any later physical change is visible
        // to the next BUILD journal scan.
        (void)capture_file_change_checkpoint(
            project_path,
            context.change_checkpoint);
    }

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

    context.sources.set_build_acceleration_capture(
        mode == full_construction_mode::rebuild);

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
            context.sources,
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

    full_persistence_job persistence{
        layout,
        context};

    persistence.artifact_count =
        mode == full_construction_mode::publish
        ? 1
        : full_persistence_artifact_count;

    auto active_lanes =
        execution_lane_capacity();

    if (active_lanes == 0) {
        active_lanes = 1;
    }

    if (active_lanes >
        persistence.artifact_count) {

        active_lanes =
            persistence.artifact_count;
    }

    persistence.active_lanes =
        active_lanes;

    bool parallel_started = false;
    server_status parallel_status =
        server_status::success;

    {
        execution_lanes lanes;

        const auto started =
            lanes.start(
                active_lanes);

        if (succeeded(started)) {
            parallel_started = true;

            parallel_status =
                lanes.run(
                    active_lanes,
                    persist_full_artifacts,
                    &persistence);
        }
    }

    if (!parallel_started) {
        persistence.active_lanes = 1;

        persist_full_artifacts(
            &persistence,
            0);
    }
    else if (!succeeded(
                 parallel_status)) {

        const auto compiled_index =
            static_cast<std::size_t>(
                full_persistence_artifact::
                    compiled);

        if (persistence.results[
                compiled_index].status ==
            full_persistence_status::
                pending) {

            persistence.results[
                compiled_index] =
                    persist_compiled(
                        layout,
                        context);
        }

        for (std::size_t index = 1;
             index <
                persistence.artifact_count;
             ++index) {

            if (persistence.results[
                    index].status ==
                full_persistence_status::
                    pending) {

                persistence.results[
                    index] = {
                        full_persistence_status::
                            io_failed,
                        full_persistence_stage::
                            dispatch,
                    };
            }
        }
    }

    const auto compiled_index =
        static_cast<std::size_t>(
            full_persistence_artifact::
                compiled);

    const auto& compiled_result =
        persistence.results[
            compiled_index];

    if (compiled_result.status !=
        full_persistence_status::success) {

        const auto& descriptor =
            compiled_result.status ==
                    full_persistence_status::
                        invalid
                ? diagnostics::
                    project_compiled_invalid
                : diagnostics::
                    project_compiled_io_failed;

        const auto detail =
            compiled_result.stage ==
                    full_persistence_stage::prepare
                ? std::string_view{
                    "Cannot prepare compiled.bin direct-encoding layout"}
                : compiled_result.stage ==
                        full_persistence_stage::create
                    ? std::string_view{
                        "Cannot create and memory-map compiled.bin for direct full-construction encoding"}
                    : compiled_result.stage ==
                            full_persistence_stage::encode
                        ? std::string_view{
                            "Direct compiled.bin encoding failed"}
                        : compiled_result.stage ==
                                full_persistence_stage::validate
                            ? std::string_view{
                                "Direct compiled.bin image failed structural or cold semantic validation"}
                            : std::string_view{
                                "Cannot flush direct compiled.bin mapping"};

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .file(layout.compiled)
                .detail(detail)
                .build());

        return compiled_result.status ==
                full_persistence_status::io_failed
            ? server_status::io_error
            : server_status::
                project_artifact_invalid;
    }

    if (mode ==
        full_construction_mode::rebuild) {

        emit_build_cache_warning(
        persistence.results[
            static_cast<std::size_t>(
                full_persistence_artifact::source)],
        layout.source_save,
        diagnostics::project_source_save_io_failed,
        diagnostics::project_source_save_invalid,
        operation,
        diagnostics);

    emit_build_cache_warning(
        persistence.results[
            static_cast<std::size_t>(
                full_persistence_artifact::database)],
        layout.database,
        diagnostics::project_database_io_failed,
        diagnostics::project_database_invalid,
        operation,
        diagnostics);

    emit_build_cache_warning(
        persistence.results[
            static_cast<std::size_t>(
                full_persistence_artifact::manifest)],
        layout.manifest,
        diagnostics::project_manifest_io_failed,
        diagnostics::project_manifest_invalid,
        operation,
        diagnostics);
    }

    read_only_file_mapping resident_mapping;

    if (resident_mapping.open(
            layout.compiled) !=
        read_only_file_mapping_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_io_failed,
                operation)
                .file(layout.compiled)
                .detail(
                    "Cannot reopen final compiled.bin for resident publication")
                .build());

        return server_status::io_error;
    }

    compiled_project_view resident_compiled;

    if (resident_compiled.bind(
            resident_mapping.bytes()) !=
        compiled_project_image_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::project_compiled_invalid,
                operation)
                .file(layout.compiled)
                .detail(
                    "Final compiled.bin failed resident structural bind")
                .build());

        return server_status::
            project_artifact_invalid;
    }

    return create_resident_project(
        project_path,
        settings,
        operation,
        diagnostics,
        std::move(resident_mapping),
        resident_compiled,
        output);
        }();

    if (succeeded(rebuilt)) {
        return rebuilt;
    }

    // The operation scope above is already destroyed here, including any open
    // mappings. Failed full construction must leave no mixed/stale artifact set.
    if (remove_project_artifacts(
            layout) !=
        project_artifact_io_result::success) {

        const auto& descriptor =
            mode == full_construction_mode::publish
            ? diagnostics::project_publish_cleanup_failed
            : diagnostics::project_rebuild_cleanup_failed;

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .file(layout.root)
                .detail(
                    mode == full_construction_mode::publish
                    ? "Failed PUBLISH could not remove the incomplete artifact set"
                    : "Failed REBUILD could not remove the complete persisted artifact set")
                .build());

        return server_status::io_error;
    }

    return rebuilt;
}

}
