/*
 * Server lifecycle controller.
 *
 * server owns lifecycle behavior. server_context owns process-lifetime Server
 * state. BUILD/REBUILD construction state must remain outside server_context.
 * All lifecycle commands execute on the Server control thread.
 */
#pragma once

#include "communication/server_command.hpp"
#include "communication/server_command_result.hpp"
#include "diagnostics/diagnostic_collection.hpp"
#include "operation.hpp"
#include "server_context.hpp"
#include "server_status.hpp"

#include <cstdint>
#include <filesystem>

namespace cw::server {

// Coordinates Server startup, command execution, Project ownership, and shutdown.
class server final {
public:
    // Loads server.json, starts communication, and executes the configured
    // Project startup policy when project.path is present.
    [[nodiscard]] server_status start(
        const std::filesystem::path& configuration_path,
        diagnostic_collection& diagnostics);

    // Runs the Server command loop until SHUTDOWN is executed.
    [[nodiscard]] int run();

    // Executes one transport-neutral command and returns its complete result.
    [[nodiscard]] server_command_result execute(
        const server_command& command);

    // Restores one persisted Project while UNLOADED. Source change detection
    // and reconstruction belong to BUILD/REBUILD, not LOAD.
    [[nodiscard]] server_status load(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    // Releases the active Project and returns to UNLOADED.
    [[nodiscard]] server_status unload(
        operation_id operation,
        diagnostic_collection& diagnostics);

    // Stops communication and releases active Project ownership.
    void shutdown() noexcept;

private:
    // Allocates the next process-local operation identity.
    [[nodiscard]] operation_id next_operation() noexcept;

    // Sole owner of this Server instance's mutable state and subsystems.
    server_context context;

    // Monotonic operation identity source. Zero remains reserved as invalid.
    std::uint64_t next_operation_value = 1;

    // True only while run() should continue accepting internal commands.
    bool running = false;
};

}
