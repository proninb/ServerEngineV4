/*
 * Server request dispatch and lifecycle implementation.
 *
 * Every externally visible Server request owns one operation_id and one
 * diagnostic_collection. Nested layers append to that same collection.
 */
#include "server.hpp"

#include "configuration/server_configuration_loader.hpp"
#include "diagnostics/diagnostic_builder.hpp"
#include "diagnostics/diagnostic_descriptor.hpp"
#include "license/server_license_loader.hpp"
#include "license/server_lease_loader.hpp"
#include "project/project_build.hpp"
#include "project/project_load.hpp"
#include "project/project_publish.hpp"
#include "project/project_rebuild.hpp"
#include "project/runtime/runtime_query.hpp"

#include <chrono>
#include <memory>

namespace cw::server {
namespace {

[[nodiscard]] std::filesystem::path resolve(
    const std::filesystem::path& base,
    const std::filesystem::path& path) {

    return path.is_absolute()
        ? path.lexically_normal()
        : (base / path).lexically_normal();
}


}

operation_id server::next_operation() noexcept {
    const auto value = next_operation_value++;

    // uint64 wrap is practically unreachable, but zero remains reserved.
    if (next_operation_value == 0) {
        next_operation_value = 1;
    }

    return operation_id{value};
}

server_status server::start(
    const std::filesystem::path& configuration_path,
    diagnostic_collection& diagnostics) {

    diagnostics.clear();

    const auto operation = next_operation();

    context.configuration_path =
        std::filesystem::absolute(
            configuration_path).lexically_normal();

    context.configuration_directory =
        context.configuration_path.parent_path();

    auto status = load_server_configuration(
        context.configuration_path,
        operation,
        diagnostics,
        context.configuration);

    if (!succeeded(status)) {
        return status;
    }

    status = load_server_license(
        context.configuration_directory /
            "server.license",
        std::chrono::system_clock::now(),
        operation,
        diagnostics,
        context.license);

    if (!succeeded(status)) {
        return status;
    }

    status = context.identity.start(
        context.configuration.server_identity,
        std::chrono::system_clock::now());

    if (!succeeded(status)) {
        const auto& descriptor =
            status == server_status::unsupported
                ? diagnostics::server_identity_unsupported
                : diagnostics::server_identity_start_failed;

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .detail(
                    context.identity.detail().empty()
                        ? "Failed to establish Server identity"
                        : std::string(context.identity.detail()))
                .build());

        context.identity.stop();
        context.license.clear();
        return status;
    }

    status = load_server_lease(
        context.configuration_directory /
            "server.lease",
        std::chrono::system_clock::now(),
        context.license,
        operation,
        diagnostics,
        context.lease);

    if (!succeeded(status)) {
        context.identity.stop();
        context.license.clear();
        return status;
    }

    status = context.authentication.start(
        context.configuration.authentication,
        context.configuration_directory);

    if (succeeded(status)) {
        context.mode =
            server_mode::full;
    } else {
        const auto detail =
            context.authentication.detail().empty()
                ? std::string("Authentication unavailable")
                : std::string(context.authentication.detail());

        context.authentication.stop();
        context.mode =
            server_mode::demo;

        diagnostics.emit(
            diagnostic(
                diagnostics::server_demo_mode,
                operation)
                .detail(detail)
                .build());
    }

    status = context.communications.start(
        context.configuration.communication,
        context.requests);

    if (!succeeded(status)) {
        const auto& descriptor =
            status == server_status::unsupported
                ? diagnostics::communication_unsupported_transport
                : diagnostics::communication_start_failed;

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .detail(
                    status == server_status::unsupported
                        ? "Configured communication transport has no backend"
                        : "Failed to start configured communication endpoints")
                .build());

        context.authentication.stop();
        context.identity.stop();
        return status;
    }

    running = true;

    if (context.configuration.project) {
        const auto& startup =
            *context.configuration.project;

        switch (startup.startup) {
        case project_startup_mode::load:
            status = load(
                startup.path,
                operation,
                diagnostics);
            break;

        case project_startup_mode::publish:
            status = publish(
                startup.path,
                operation,
                diagnostics);
            break;

        case project_startup_mode::rebuild:
            status = rebuild(
                startup.path,
                operation,
                diagnostics);
            break;
        }

        if (!succeeded(status)) {
            shutdown();
            return status;
        }
    }

    return server_status::success;
}

int server::run() {
    while (running) {
        const auto now =
            std::chrono::system_clock::now();

        if (!context.lease.valid_at(now)) {
            running = false;
            break;
        }

        server_request_message message;

        if (!context.requests.wait_pop_until(
                context.lease.expires_at(),
                message)) {

            running = false;
            break;
        }

        if (!context.lease.valid_at(
                std::chrono::system_clock::now())) {

            running = false;
            break;
        }

        auto result =
            execute_message(
                message);

        message.origin.present(
            result);
    }

    // SHUTDOWN result has already been presented to its originating endpoint.
    shutdown();

    return 0;
}

server_response server::execute_message(
    const server_request_message& message) {

    if (context.policy.allows(
            context.mode,
            message.request.kind)) {

        return execute(
            message.request);
    }

    server_response result;
    result.operation =
        next_operation();
    result.status =
        server_status::access_denied;

    result.diagnostics.emit(
        diagnostic(
            diagnostics::server_policy_denied,
            result.operation)
            .detail(
                "Request is not allowed for this origin/client identity")
            .build());

    result.diagnostics.sort_deterministic();
    return result;
}

server_response server::execute(
    const server_request& request) {

    server_response result;
    result.operation =
        next_operation();

    switch (request.kind) {
    case server_request_kind::load:
        result.status =
            load(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::publish:
        result.status =
            publish(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::build:
        result.status =
            build(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::unload:
        result.status =
            unload(
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::rebuild:
        result.status =
            rebuild(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::get_state:
        result.payload =
            server_response_payload_kind::state;
        result.state.project =
            context.project
                ? project_state::loaded
                : project_state::unloaded;
        result.status =
            server_status::success;
        break;

    case server_request_kind::get_value: {
        if (!context.project) {
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::project_not_loaded,
                    result.operation)
                    .detail(
                        "GET_VALUE requires an active Project")
                    .build());

            result.status =
                server_status::project_not_loaded;
            break;
        }

        const auto bytes =
            context.project->shm().bytes();

        const auto logical_size =
            static_cast<std::size_t>(
                context.project->runtime_size());

        if (logical_size > bytes.size()) {
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_failed,
                    result.operation)
                    .detail(request.name)
                    .build());

            result.status =
                server_status::project_runtime_failed;
            break;
        }

        runtime_value value;

        const auto queried =
            get_runtime_value(
                context.project->compiled(),
                context.project->runtime_bindings(),
                bytes.first(logical_size),
                request.name,
                value);

        switch (queried) {
        case runtime_query_result::success:
            result.payload =
                server_response_payload_kind::runtime_value;
            result.value = value;
            result.status =
                server_status::success;
            break;

        case runtime_query_result::invalid_input:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_invalid,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::runtime_query_invalid;
            break;

        case runtime_query_result::not_found:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_not_found,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::runtime_query_not_found;
            break;

        case runtime_query_result::unsupported_type:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_unsupported,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::unsupported;
            break;

        case runtime_query_result::invalid_runtime:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_failed,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::project_runtime_failed;
            break;
        }

        break;
    }

    case server_request_kind::shutdown:
        // Keep the originating endpoint alive until run() presents this result.
        running = false;
        result.status =
            server_status::success;
        break;
    }

    result.diagnostics.sort_deterministic();
    return result;
}

server_status server::load(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_already_loaded,
                operation)
                .detail("UNLOAD the active Project before LOAD")
                .build());

        return server_status::project_already_loaded;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            project_path);

    std::unique_ptr<project> candidate;

    const auto status =
        load_project(
            path,
            context.configuration.settings,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(status)) {
        context.project.reset();
        return status;
    }

    context.project =
        std::move(candidate);

    return server_status::success;
}

server_status server::publish(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_already_loaded,
                operation)
                .detail("UNLOAD the active Project before PUBLISH")
                .build());

        return server_status::project_already_loaded;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            project_path);

    std::unique_ptr<project> candidate;

    const auto status =
        publish_project(
            path,
            context.configuration.settings,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(status)) {
        context.project.reset();
        return status;
    }

    context.project =
        std::move(candidate);

    return server_status::success;
}

server_status server::build(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_already_loaded,
                operation)
                .detail("UNLOAD the active Project before BUILD")
                .build());

        return server_status::project_already_loaded;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            project_path);

    std::unique_ptr<project> candidate;

    const auto status =
        build_project(
            path,
            context.configuration.settings,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(status)) {
        context.project.reset();
        return status;
    }

    context.project =
        std::move(candidate);

    return server_status::success;
}

server_status server::rebuild(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_already_loaded,
                operation)
                .detail("UNLOAD the active Project before REBUILD")
                .build());

        return server_status::project_already_loaded;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            project_path);

    std::unique_ptr<project> candidate;

    const auto status =
        rebuild_project(
            path,
            context.configuration.settings,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(status)) {
        context.project.reset();
        return status;
    }

    context.project =
        std::move(candidate);

    return server_status::success;
}

server_status server::unload(
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("UNLOAD requires an active Project")
                .build());

        return server_status::project_not_loaded;
    }



    context.project.reset();

    return server_status::success;
}

void server::shutdown() noexcept {
    running = false;
    context.communications.stop();
    context.project.reset();
    context.authentication.stop();
    context.lease.clear();
    context.identity.stop();
    context.license.clear();
}



}
