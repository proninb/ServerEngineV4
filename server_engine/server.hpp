/*
 * Server lifecycle controller.
 *
 * server owns mode routing and publication. Each Project mode owns its own
 * temporary pipeline state; there is no universal Project construction context.
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
    [[nodiscard]] server_status start(
        const std::filesystem::path& configuration_path,
        diagnostic_collection& diagnostics);

    [[nodiscard]] int run();

    [[nodiscard]] server_command_result execute(
        const server_command& command);

    // Fast persisted-state restore. No Project/source construction checks.
    [[nodiscard]] server_status load(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    // Full source construction. Persists compiled.bin only.
    [[nodiscard]] server_status publish(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    // Incremental construction from persisted BUILD state.
    [[nodiscard]] server_status build(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    // Full construction mode. Ignores incremental construction state.
    [[nodiscard]] server_status rebuild(
        const std::filesystem::path& project_path,
        operation_id operation,
        diagnostic_collection& diagnostics);

    [[nodiscard]] server_status unload(
        operation_id operation,
        diagnostic_collection& diagnostics);

    void shutdown() noexcept;

private:
    [[nodiscard]] operation_id next_operation() noexcept;

    server_context context;
    std::uint64_t next_operation_value = 1;
    bool running = false;
};

}
