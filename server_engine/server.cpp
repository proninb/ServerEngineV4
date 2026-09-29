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
#include "project/runtime/runtime_ic_codec.hpp"
#include "project/runtime/runtime_ic_reset.hpp"
#include "project/runtime/runtime_ic_snapshot.hpp"
#include "project/runtime/runtime_query.hpp"
#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"

#include <chrono>
#include <cstring>
#include <memory>
#include <system_error>
#include <vector>

namespace cw::server {
namespace {

inline constexpr auto shutdown_ack_timeout =
    std::chrono::milliseconds{2000};

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
        context.requests,
        context.mode,
        context.lease.limits().max_connections);

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

        communication_control_message message;

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

        if (auto* request =
                std::get_if<server_request_message>(&message)) {

            const auto old_state =
                current_project_state();

            auto result =
                execute_message(*request);

            if (request->request.kind ==
                server_request_kind::shutdown) {

                request->origin.arm_written();
            }

            request->origin.present(result);

            if (request->request.kind ==
                server_request_kind::shutdown) {

                static_cast<void>(
                    request->origin.wait_written(
                        shutdown_ack_timeout));
            }

            const auto new_state =
                current_project_state();

            context.communications.publish_server_state(
                old_state,
                new_state);
        } else if (auto* login =
                       std::get_if<login_control_message>(&message)) {

            execute_login(*login);
        } else if (auto* close =
                       std::get_if<connection_close_control_message>(&message)) {

            execute_connection_close(*close);
        } else if (auto* client =
                       std::get_if<client_control_message>(&message)) {

            execute_client(*client);
        }
    }

    // SHUTDOWN result has already been presented to its originating endpoint.
    shutdown();

    return 0;
}

project_state server::current_project_state() const noexcept {
    return context.project
        ? project_state::loaded
        : project_state::unloaded;
}

void server::execute_login(
    login_control_message& message) {

    if (message.connection == nullptr) {
        return;
    }

    server_response result;
    result.operation = next_operation();

    const auto login =
        message.connection->commit_login(
            message.name);

    if (login != client_session_login_result::success ||
        !context.communications.register_connection(
            *message.connection)) {

        result.status =
            server_status::communication_invalid_request;

        (void)message.connection->enqueue_response(
            message.request,
            result);
        return;
    }

    result.payload =
        server_response_payload_kind::state;

    result.state.project =
        current_project_state();

    result.status =
        server_status::success;

    (void)message.connection->enqueue_response(
        message.request,
        result);
}

void server::execute_connection_close(
    connection_close_control_message& message) noexcept {

    if (message.connection == nullptr) {
        return;
    }

    (void)message.connection->begin_close();

    context.communications.unregister_connection(
        message.connection->id());
}

void server::execute_client(
    client_control_message& message) {

    if (message.connection == nullptr) {
        return;
    }

    server_response result;
    result.operation = next_operation();

    if (!message.connection->logged_in() ||
        message.connection->closing()) {

        result.status =
            server_status::communication_invalid_request;
    } else {
        (void)context.communications.route_client(
            *message.connection,
            message.message);

        result.status =
            server_status::success;
    }

    (void)message.connection->enqueue_response(
        message.request,
        result);
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
            current_project_state();
        result.status =
            server_status::success;
        break;

    case server_request_kind::snap_ic:
        result.status =
            snap_ic(
                request.path,
                request.snap_options,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::reset_ic:
        result.status =
            reset_ic(
                request.path,
                request.reset_options,
                result.operation,
                result.diagnostics);
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

server_status server::snap_ic(
    const std::filesystem::path& ic_path,
    snap_ic_options options,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (options != snap_ic_options::none) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail("SNAP_IC options are invalid")
                .build());
        return server_status::runtime_ic_invalid;
    }

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("SNAP_IC requires an active Project")
                .build());
        return server_status::project_not_loaded;
    }

    if (ic_path.empty()) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail("SNAP_IC requires a file path")
                .build());
        return server_status::runtime_ic_invalid;
    }

    const auto runtime =
        static_cast<const project&>(
            *context.project).shm().bytes();

    const auto logical_size =
        context.project->runtime_size();

    if (logical_size == 0 ||
        logical_size >
            static_cast<std::uint64_t>(
                runtime.size())) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail("Runtime SHM size is invalid")
                .build());
        return server_status::runtime_ic_failed;
    }

    std::vector<std::byte> image;

    if (snapshot_runtime_ic_binary(
            context.project->compiled(),
            context.project->runtime_bindings(),
            runtime.first(
                static_cast<std::size_t>(
                    logical_size)),
            image) !=
        runtime_ic_snapshot_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail("SNAP_IC could not capture Runtime")
                .build());
        return server_status::runtime_ic_failed;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            ic_path);

    writable_file_mapping mapping;

    if (mapping.create(
            path,
            image.size()) !=
        writable_file_mapping_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_io_failed,
                operation)
                .detail(path.generic_string())
                .build());
        return server_status::io_error;
    }

    std::memcpy(
        mapping.bytes().data(),
        image.data(),
        image.size());

    if (mapping.flush() !=
        writable_file_mapping_result::success) {

        mapping.reset();

        std::error_code ignored;
        std::filesystem::remove(
            path,
            ignored);

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_io_failed,
                operation)
                .detail(path.generic_string())
                .build());
        return server_status::io_error;
    }

    return server_status::success;
}

server_status server::reset_ic(
    const std::filesystem::path& ic_path,
    reset_ic_options options,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!valid_reset_ic_options(options)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail("RESET_IC options are invalid")
                .build());
        return server_status::runtime_ic_invalid;
    }

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("RESET_IC requires an active Project")
                .build());
        return server_status::project_not_loaded;
    }

    if (ic_path.empty()) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail("RESET_IC requires a file path")
                .build());
        return server_status::runtime_ic_invalid;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            ic_path);

    read_only_file_mapping mapping;

    const auto opened =
        mapping.open(path);

    if (opened !=
        read_only_file_mapping_result::success) {

        const bool missing =
            opened ==
            read_only_file_mapping_result::not_found;

        diagnostics.emit(
            diagnostic(
                missing
                    ? diagnostics::runtime_ic_not_found
                    : diagnostics::runtime_ic_io_failed,
                operation)
                .detail(path.generic_string())
                .build());

        return missing
            ? server_status::runtime_ic_not_found
            : server_status::io_error;
    }

    runtime_ic_binary_view image;

    if (image.bind(
            mapping.bytes()) !=
        runtime_ic_codec_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail(path.generic_string())
                .build());
        return server_status::runtime_ic_invalid;
    }

    auto runtime =
        context.project->shm().bytes();

    const auto logical_size =
        context.project->runtime_size();

    if (logical_size == 0 ||
        logical_size >
            static_cast<std::uint64_t>(
                runtime.size())) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail("Runtime SHM size is invalid")
                .build());
        return server_status::runtime_ic_failed;
    }

    const auto reset =
        reset_runtime_ic_binary(
            context.project->compiled(),
            context.project->runtime_bindings(),
            runtime.first(
                static_cast<std::size_t>(
                    logical_size)),
            image,
            nullptr,
            options);

    switch (reset) {
    case runtime_ic_reset_result::success:
        return server_status::success;

    case runtime_ic_reset_result::invalid_input:
    case runtime_ic_reset_result::invalid_image:
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail(path.generic_string())
                .build());
        return server_status::runtime_ic_invalid;

    case runtime_ic_reset_result::not_found:
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_not_found,
                operation)
                .detail(path.generic_string())
                .build());
        return server_status::runtime_ic_not_found;

    case runtime_ic_reset_result::unsupported_type:
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail("RESET_IC encountered an unsupported Runtime type")
                .build());
        return server_status::unsupported;

    case runtime_ic_reset_result::type_mismatch:
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_type_mismatch,
                operation)
                .detail(path.generic_string())
                .build());
        return server_status::runtime_ic_type_mismatch;

    case runtime_ic_reset_result::invalid_runtime:
    case runtime_ic_reset_result::failed:
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail(path.generic_string())
                .build());
        return server_status::runtime_ic_failed;
    }

    return server_status::runtime_ic_failed;
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
    context.requests.stop_accepting_and_discard();
    context.communications.stop();
    context.project.reset();
    context.authentication.stop();
    context.lease.clear();
    context.identity.stop();
    context.license.clear();
}



}
