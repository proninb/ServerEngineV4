/*
 * Small status domain shared by the Server control layer.
 *
 * Status values are transport-neutral and exception-free. Human-readable
 * failure details are returned separately through diagnostic strings.
 */
#pragma once

namespace cw::server {

// Result codes returned by Server lifecycle and bootstrap operations.
enum class server_status {
    // Operation completed successfully.
    success = 0,

    // server.json exists but violates the supported configuration contract.
    invalid_configuration,

    // Required file or stream I/O failed.
    io_error,

    // Requested transport or operation is known but not implemented yet.
    unsupported,

    // LOAD was requested while another Project is already active.
    project_already_loaded,

    // An operation requiring an active Project was requested while UNLOADED.
    project_not_loaded,

    // Project bootstrap could not open or validate the requested Project.
    project_load_failed,

    // One or more configured communication endpoints could not start.
    communication_start_failed,
};

// Returns true only for server_status::success.
[[nodiscard]] constexpr bool succeeded(server_status status) noexcept {
    return status == server_status::success;
}

}
