/*
 * Server request dispatcher and lifecycle controller.
 *
 * server owns transport-neutral request dispatch plus Project lifecycle routing.
 * Each Project mode owns its own temporary pipeline state; there is no universal
 * Project construction context. All requests execute on the Server control thread.
 */
#pragma once

#include "communication/server_request.hpp"
#include "communication/server_request_message.hpp"
#include "communication/server_response.hpp"
#include "communication/control/communication_control.hpp"
#include "diagnostics/diagnostic_collection.hpp"
#include "operation.hpp"
#include "project/ic/ic_catalog.hpp"
#include "server_context.hpp"
#include "server_status.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cw::server {

class server final {
public:
    [[nodiscard]] server_status start(
        const std::filesystem::path& configuration_path,
        diagnostic_collection& diagnostics);

    [[nodiscard]] int run();

    [[nodiscard]] server_response execute(
        const server_request& request);

    [[nodiscard]] server_status load(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status publish(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status build(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status rebuild(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status unload(
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status snap_ic(
        const std::string& name,
        const std::vector<std::string>& group,
        const std::filesystem::path& path,
        snap_ic_options options,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status reset_ic(
        const std::string& name,
        const std::vector<std::string>& group,
        const std::filesystem::path& path,
        reset_ic_options options,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status delete_ic(
        const std::string& name,
        const std::vector<std::string>& group,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status list_ic(
        ic_catalog& output,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status run_project(
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status freeze_project(
        operation_id operation,
        diagnostic_collection& diagnostics);

    void shutdown() noexcept;

private:
    [[nodiscard]] server_response execute_message(
        const server_request_message& message);

    void execute_login(login_control_message& message);
    void execute_connection_close(connection_close_control_message& message) noexcept;
    void execute_client(client_control_message& message);

    [[nodiscard]] project_state current_project_state() const noexcept;
    [[nodiscard]] operation_id next_operation() noexcept;

    server_context context;
    project_state project_state_value = project_state::unloaded;
    std::uint64_t next_operation_value = 1;
    bool running = false;
};

}
