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
#include "project/project_configuration_loader.hpp"
#include "project/ic/ic_catalog.hpp"
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

[[nodiscard]] server_status load_resident_project_configuration(
    project& value,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    project_runtime_configuration configuration;

    const auto status =
        load_project_runtime_configuration(
            value.path(),
            operation,
            diagnostics,
            configuration);

    if (!succeeded(status)) {
        return status;
    }

    ic_catalog catalog;

    if (configuration.ic.empty()) {
        value.set_ic_catalog(
            std::move(catalog));
        return server_status::success;
    }

    const auto catalog_path =
        resolve(
            value.path().parent_path(),
            configuration.ic);

    value.set_ic_catalog_path(
        catalog_path);

    const auto loaded =
        load_ic_catalog(
            catalog_path,
            catalog);

    if (loaded == ic_catalog_result::not_found) {
        value.set_ic_catalog(
            std::move(catalog));
        return server_status::success;
    }

    if (loaded != ic_catalog_result::success) {
        diagnostics.emit(
            diagnostic(
                loaded == ic_catalog_result::invalid
                    ? diagnostics::runtime_ic_invalid
                    : diagnostics::runtime_ic_io_failed,
                operation)
                .file(catalog_path)
                .detail(
                    loaded == ic_catalog_result::invalid
                        ? "Configured IC.json is invalid"
                        : "Configured IC.json could not be read")
                .build());

        return loaded == ic_catalog_result::invalid
            ? server_status::runtime_ic_invalid
            : server_status::io_error;
    }

    value.set_ic_catalog(
        std::move(catalog));

    return server_status::success;
}

[[nodiscard]] server_status require_ic_catalog(
    const project& value,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!value.ic_catalog_path().empty()) {
        return server_status::success;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::runtime_ic_invalid,
            operation)
            .detail(
                "Root project.json does not configure an IC catalog path")
            .build());

    return server_status::runtime_ic_invalid;
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
    return project_state_value;
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

    case server_request_kind::delete_ic:
        result.status =
            delete_ic(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::list_ic:
        result.status =
            list_ic(
                result.catalog,
                result.operation,
                result.diagnostics);

        if (succeeded(result.status)) {
            result.payload =
                server_response_payload_kind::ic_catalog;
        }
        break;

    case server_request_kind::run:
        result.status =
            run_project(
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::freeze:
        result.status =
            freeze_project(
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
        project_state_value =
            project_state::unloaded;
        return status;
    }

    const auto configured =
        load_resident_project_configuration(
            *candidate,
            operation,
            diagnostics);

    if (!succeeded(configured)) {
        candidate.reset();
        context.project.reset();
        project_state_value =
            project_state::unloaded;
        return configured;
    }

    context.project =
        std::move(candidate);

    project_state_value =
        project_state::loaded;

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
        project_state_value =
            project_state::unloaded;
        return status;
    }

    const auto configured =
        load_resident_project_configuration(
            *candidate,
            operation,
            diagnostics);

    if (!succeeded(configured)) {
        candidate.reset();
        context.project.reset();
        project_state_value =
            project_state::unloaded;
        return configured;
    }

    context.project =
        std::move(candidate);

    project_state_value =
        project_state::loaded;

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
        project_state_value =
            project_state::unloaded;
        return status;
    }

    const auto configured =
        load_resident_project_configuration(
            *candidate,
            operation,
            diagnostics);

    if (!succeeded(configured)) {
        candidate.reset();
        context.project.reset();
        project_state_value =
            project_state::unloaded;
        return configured;
    }

    context.project =
        std::move(candidate);

    project_state_value =
        project_state::loaded;

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
        project_state_value =
            project_state::unloaded;
        return status;
    }

    const auto configured =
        load_resident_project_configuration(
            *candidate,
            operation,
            diagnostics);

    if (!succeeded(configured)) {
        candidate.reset();
        context.project.reset();
        project_state_value =
            project_state::unloaded;
        return configured;
    }

    context.project =
        std::move(candidate);

    project_state_value =
        project_state::loaded;

    return server_status::success;
}

server_status server::snap_ic(
    const std::filesystem::path& ic_path,
    snap_ic_options options,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (options != snap_ic_options::none ||
        ic_path.empty()) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail(
                    "SNAP_IC requires path and valid options")
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

    if (!project_state_can_ic(project_state_value)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("SNAP_IC requires LOADED or FREEZE")
                .build());

        return server_status::runtime_state_invalid;
    }

    if (!succeeded(require_ic_catalog(
            *context.project,
            operation,
            diagnostics))) {
        return server_status::runtime_ic_invalid;
    }

    const auto path = resolve(
        context.project->ic_catalog_path().parent_path(),
        ic_path);

    ic_catalog catalog_candidate;

    try {
        catalog_candidate =
            context.project->
                ic_catalog_data();
    }
    catch (...) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail(
                    "Cannot allocate SNAP_IC catalog candidate")
                .build());

        return server_status::runtime_ic_failed;
    }

    if (!upsert_ic_catalog_entry(
            catalog_candidate,
            context.project->ic_catalog_path(),
            ic_path,
            0)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail(
                    "SNAP_IC path is invalid for IC.json catalog")
                .build());

        return server_status::runtime_ic_invalid;
    }

    const auto previous_state = project_state_value;
    project_state_value = project_state::snapping_ic;
    context.communications.publish_server_state(
        previous_state,
        project_state_value);

    const auto status = [&]() -> server_status {
        const auto runtime =
            static_cast<const project&>(*context.project).shm().bytes();
        const auto logical_size = context.project->runtime_size();

        if (logical_size == 0 ||
            logical_size > static_cast<std::uint64_t>(runtime.size())) {

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
                runtime.first(static_cast<std::size_t>(logical_size)),
                image) != runtime_ic_snapshot_result::success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_failed,
                    operation)
                    .detail("SNAP_IC could not capture Runtime")
                    .build());
            return server_status::runtime_ic_failed;
        }

        std::filesystem::path backup_path;
        bool backup_active = false;

        std::error_code file_error;

        const auto target_exists =
            std::filesystem::exists(
                path,
                file_error);

        if (file_error) {
            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(path.generic_string())
                    .build());

            return server_status::io_error;
        }

        if (target_exists) {
            if (!std::filesystem::is_regular_file(
                    path,
                    file_error) ||
                file_error) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(path.generic_string())
                        .build());

                return server_status::io_error;
            }

            backup_path = path;
            backup_path += ".snap-backup";

            file_error.clear();

            const auto backup_exists =
                std::filesystem::exists(
                    backup_path,
                    file_error);

            if (file_error ||
                backup_exists) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            backup_path.generic_string())
                        .build());

                return server_status::io_error;
            }

            std::filesystem::rename(
                path,
                backup_path,
                file_error);

            if (file_error) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(path.generic_string())
                        .build());

                return server_status::io_error;
            }

            backup_active = true;
        }

        const auto rollback_target =
            [&]() noexcept {

                std::error_code rollback_error;

                std::filesystem::remove(
                    path,
                    rollback_error);

                if (rollback_error) {
                    return false;
                }

                if (!backup_active) {
                    return true;
                }

                std::filesystem::rename(
                    backup_path,
                    path,
                    rollback_error);

                return !rollback_error;
            };

        writable_file_mapping mapping;
        if (mapping.create(path, image.size()) !=
            writable_file_mapping_result::success) {

            if (!rollback_target()) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            "SNAP_IC failed to restore the previous snapshot file")
                        .build());
            }

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(path.generic_string())
                    .build());

            return server_status::io_error;
        }

        std::memcpy(mapping.bytes().data(), image.data(), image.size());

        if (mapping.flush() != writable_file_mapping_result::success) {
            mapping.reset();

            if (!rollback_target()) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            "SNAP_IC failed to restore the previous snapshot file")
                        .build());
            }

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(path.generic_string())
                    .build());

            return server_status::io_error;
        }

        mapping.reset();

        if (!upsert_ic_catalog_entry(
                catalog_candidate,
                context.project->ic_catalog_path(),
                ic_path,
                static_cast<std::uint64_t>(
                    image.size()))) {

            if (!rollback_target()) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            "SNAP_IC failed to restore the previous snapshot file")
                        .build());
            }

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_invalid,
                    operation)
                    .detail(
                        "SNAP_IC catalog candidate became invalid")
                    .build());

            return server_status::runtime_ic_invalid;
        }

        if (save_ic_catalog(
                context.project->ic_catalog_path(),
                catalog_candidate) != ic_catalog_result::success) {

            if (!rollback_target()) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            "SNAP_IC failed to restore the previous snapshot file")
                        .build());
            }

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(
                        context.project->ic_catalog_path().generic_string())
                    .build());

            return server_status::io_error;
        }

        context.project->set_ic_catalog(
            std::move(
                catalog_candidate));

        // Catalog is now authoritative. Cleanup failures must not roll back
        // the successfully committed logical IC.
        std::error_code ignored;

        if (backup_active) {
            std::filesystem::remove(
                backup_path,
                ignored);
        }

        return server_status::success;
    }();

    project_state_value = previous_state;
    context.communications.publish_server_state(
        project_state::snapping_ic,
        previous_state);

    return status;
}

server_status server::reset_ic(
    const std::filesystem::path& ic_path,
    reset_ic_options options,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!valid_reset_ic_options(options) ||
        ic_path.empty()) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail(
                    "RESET_IC requires path and valid options")
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

    if (!project_state_can_ic(project_state_value)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("RESET_IC requires LOADED or FREEZE")
                .build());
        return server_status::runtime_state_invalid;
    }

    if (!succeeded(require_ic_catalog(
            *context.project,
            operation,
            diagnostics))) {
        return server_status::runtime_ic_invalid;
    }

    const auto* node =
        find_ic_catalog_entry(
            context.project->ic_catalog_data(),
            context.project->ic_catalog_path(),
            ic_path);

    if (node == nullptr) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_not_found,
                operation)
                .detail(ic_path.generic_string())
                .build());

        return server_status::runtime_ic_not_found;
    }

    const auto path = resolve(
        context.project->ic_catalog_path().parent_path(),
        node->path);

    const auto previous_state = project_state_value;
    project_state_value = project_state::resetting_ic;
    context.communications.publish_server_state(
        previous_state,
        project_state_value);

    const auto status = [&]() -> server_status {
        read_only_file_mapping mapping;
        const auto opened = mapping.open(path);

        if (opened != read_only_file_mapping_result::success) {
            const bool missing =
                opened == read_only_file_mapping_result::not_found;

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
        if (image.bind(mapping.bytes()) !=
            runtime_ic_codec_result::success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_invalid,
                    operation)
                    .detail(path.generic_string())
                    .build());
            return server_status::runtime_ic_invalid;
        }

        auto runtime = context.project->shm().bytes();
        const auto logical_size = context.project->runtime_size();

        if (logical_size == 0 ||
            logical_size > static_cast<std::uint64_t>(runtime.size())) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_failed,
                    operation)
                    .detail("Runtime SHM size is invalid")
                    .build());
            return server_status::runtime_ic_failed;
        }

        const auto reset = reset_runtime_ic_binary(
            context.project->compiled(),
            context.project->runtime_bindings(),
            runtime.first(static_cast<std::size_t>(logical_size)),
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
                    .detail(
                        "RESET_IC encountered an unsupported Runtime type")
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
    }();

    project_state_value = previous_state;
    context.communications.publish_server_state(
        project_state::resetting_ic,
        previous_state);

    return status;
}

server_status server::delete_ic(
    const std::filesystem::path& ic_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (ic_path.empty()) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail("DELETE_IC requires path")
                .build());
        return server_status::runtime_ic_invalid;
    }

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("DELETE_IC requires an active Project")
                .build());
        return server_status::project_not_loaded;
    }

    if (!succeeded(require_ic_catalog(
            *context.project,
            operation,
            diagnostics))) {
        return server_status::runtime_ic_invalid;
    }

    ic_catalog catalog;

    try {
        catalog =
            context.project->ic_catalog_data();
    }
    catch (...) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail(
                    "Cannot allocate DELETE_IC catalog candidate")
                .build());

        return server_status::runtime_ic_failed;
    }

    std::filesystem::path removed_path;
    if (!erase_ic_catalog_entry(
            catalog,
            context.project->ic_catalog_path(),
            ic_path,
            removed_path)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_not_found,
                operation)
                .detail(ic_path.generic_string())
                .build());
        return server_status::runtime_ic_not_found;
    }

    if (save_ic_catalog(
            context.project->ic_catalog_path(),
            catalog) != ic_catalog_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_io_failed,
                operation)
                .detail(
                    context.project->ic_catalog_path().generic_string())
                .build());
        return server_status::io_error;
    }

    context.project->set_ic_catalog(
        std::move(catalog));

    const auto data_path = resolve(
        context.project->ic_catalog_path().parent_path(),
        removed_path);

    // The catalog is authoritative after publication. Failure to remove
    // the unreferenced binary is cleanup only and does not roll back DELETE_IC.
    std::error_code ignored;
    (void)std::filesystem::remove(
        data_path,
        ignored);

    return server_status::success;
}

server_status server::list_ic(
    ic_catalog& output,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    output = {};

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("LIST_IC requires an active Project")
                .build());
        return server_status::project_not_loaded;
    }

    if (!succeeded(require_ic_catalog(
            *context.project,
            operation,
            diagnostics))) {
        return server_status::runtime_ic_invalid;
    }

    try {
        output =
            context.project->ic_catalog_data();
    }
    catch (...) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail(
                    "Cannot allocate LIST_IC response")
                .build());

        return server_status::runtime_ic_failed;
    }

    return server_status::success;
}

server_status server::run_project(
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("RUN requires an active Project")
                .build());

        return server_status::project_not_loaded;
    }

    if (!project_state_can_run(
            project_state_value)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("RUN requires LOADED or FREEZE")
                .build());

        return server_status::runtime_state_invalid;
    }

    project_state_value =
        project_state::run;

    return server_status::success;
}

server_status server::freeze_project(
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("FREEZE requires an active Project")
                .build());

        return server_status::project_not_loaded;
    }

    if (!project_state_can_freeze(
            project_state_value)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("FREEZE requires RUN")
                .build());

        return server_status::runtime_state_invalid;
    }

    project_state_value =
        project_state::freeze;

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

    if (!project_state_can_unload(
            project_state_value)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("UNLOAD requires LOADED or FREEZE")
                .build());

        return server_status::runtime_state_invalid;
    }

    context.project.reset();
    project_state_value =
        project_state::unloaded;

    return server_status::success;
}

void server::shutdown() noexcept {
    running = false;
    context.requests.stop_accepting_and_discard();
    context.communications.stop();
    context.project.reset();
    project_state_value =
        project_state::unloaded;
    context.authentication.stop();
    context.lease.clear();
    context.identity.stop();
    context.license.clear();
}



}
