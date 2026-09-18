/*
 * Server lifecycle implementation.
 *
 * Every externally visible lifecycle action owns one operation_id and one
 * diagnostic_collection. Nested layers append to that same collection.
 */
#include "server.hpp"

#include "configuration/server_configuration_loader.hpp"
#include "diagnostics/diagnostic_builder.hpp"
#include "diagnostics/diagnostic_descriptor.hpp"
#include "project/project_build.hpp"
#include "project/project_load.hpp"
#include "project/project_rebuild.hpp"

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

    status = context.communications.start(
        context.configuration.communication,
        context.commands);

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
        auto request =
            context.commands.wait_pop();

        auto result =
            execute(
                request.command);

        request.origin.present(
            result);
    }

    // SHUTDOWN result has already been presented to its originating endpoint.
    shutdown();

    return 0;
}

server_command_result server::execute(
    const server_command& command) {

    server_command_result result;
    result.operation =
        next_operation();

    switch (command.kind) {
    case server_command_kind::load:
        result.status =
            load(
                command.path,
                result.operation,
                result.diagnostics);
        break;

    case server_command_kind::build:
        result.status =
            build(
                result.operation,
                result.diagnostics);
        break;

    case server_command_kind::unload:
        result.status =
            unload(
                result.operation,
                result.diagnostics);
        break;

    case server_command_kind::rebuild:
        result.status =
            rebuild(
                command.path,
                result.operation,
                result.diagnostics);
        break;

    case server_command_kind::shutdown:
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
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("BUILD requires an active Project")
                .build());

        return server_status::project_not_loaded;
    }

    std::unique_ptr<project> candidate;

    const auto status =
        build_project(
            *context.project,
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
}



}
