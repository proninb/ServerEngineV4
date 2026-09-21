#include "project_rebuild.hpp"

#include "project_lifecycle_context.hpp"
#include "frontend/source_discovery.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"

#include <string>

namespace cw::server {
namespace {

[[nodiscard]] std::string_view preprocessing_detail(
    const source_discovery_failure& failure) noexcept {

    if (failure.kind ==
        source_discovery_failure_kind::
            unsupported_include_form) {

        return "Angled #include requires configured include roots, which are not part of the current Project contract";
    }

    if (failure.kind ==
        source_discovery_failure_kind::
            invalid_include) {

        return "Direct #include header name is invalid";
    }

    if (failure.kind ==
        source_discovery_failure_kind::
            include_resolution) {

        return "Executed #include could not be resolved or materialized";
    }

    if (failure.kind ==
        source_discovery_failure_kind::
            include_depth_exceeded) {

        return "Executed #include nesting exceeds the supported depth";
    }

    switch (failure.directive) {
    case directive_execution_error_kind::malformed_operand:
        return "Preprocessing directive operand is malformed";
    case directive_execution_error_kind::invalid_macro_definition:
        return "Macro redefinition conflicts with the active definition";
    case directive_execution_error_kind::unsupported_directive:
        return "Preprocessing directive is not supported by the current language contract";
    case directive_execution_error_kind::invalid_include:
        return "Include directive does not contain a supported direct header name";
    case directive_execution_error_kind::unmatched_else:
        return "Unmatched #else in this physical file";
    case directive_execution_error_kind::duplicate_else:
        return "Conditional group contains more than one #else";
    case directive_execution_error_kind::unmatched_endif:
        return "Unmatched #endif in this physical file";
    case directive_execution_error_kind::conditional_depth_exceeded:
        return "Conditional nesting exceeds the supported depth";
    case directive_execution_error_kind::unterminated_conditional:
        return "Conditional group is not closed before physical file end";
    case directive_execution_error_kind::none:
        break;
    }

    return "Project source preprocessing failed";
}

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
    const server_abi_configuration& abi,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output) {

    (void)output;

    rebuild_context context{abi};

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

    source_discovery_failure failure;

    const auto discovered =
        discover_source_closure(
            context.files,
            context.lexical,
            context.preprocessor,
            context.strings,
            &failure);

    if (!succeeded(discovered)) {
        if (failure.kind ==
                source_discovery_failure_kind::lexical &&
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
                ? discovered
                : emitted;
        }

        if (failure.kind !=
                source_discovery_failure_kind::none &&
            failure.file) {

            const auto emitted =
                emit_source_failure(
                    context.files,
                    failure.file,
                    failure.source,
                    diagnostics::project_preprocessing_error,
                    preprocessing_detail(
                        failure),
                    operation,
                    diagnostics);

            return succeeded(emitted)
                ? discovered
                : emitted;
        }

        return discovered;
    }

    // The candidate manifest belongs to candidate G0. Persist it only as part
    // of the eventual coordinated successful REBUILD commit.

    diagnostics.emit(
        diagnostic(
            diagnostics::project_rebuild_incomplete,
            operation)
            .detail(
                "Project configuration manifest, source lexical closure, and executed quoted-include discovery are complete; Assign processing, dependency-topology finalization, Parser/Semantic construction, and Graph construction are not implemented yet")
            .build());

    return server_status::unsupported;
}

}
