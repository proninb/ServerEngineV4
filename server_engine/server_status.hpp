/*
 * Small transport-neutral status domain shared by Server lifecycle code.
 *
 * Status values are transport-neutral and exception-free. Human-readable
 * failure details are returned separately through diagnostic strings.
 */
#pragma once

namespace cw::server {

// Result codes returned by Server lifecycle and Project operations.
enum class server_status {
    // Operation completed successfully.
    success = 0,

    // server.json exists but violates the supported configuration contract.
    invalid_configuration,

    // project.json violates the supported Project construction contract.
    project_configuration_invalid,

    // Required file or stream I/O failed.
    io_error,

    // Requested transport or operation is known but not implemented yet.
    unsupported,

    // LOAD was requested while another Project is already active.
    project_already_loaded,

    // An operation requiring an active Project was requested while UNLOADED.
    project_not_loaded,

    // Project LOAD could not restore the requested Project.
    project_load_failed,

    // Persisted Project construction/runtime artifact failed structural validation.
    project_artifact_invalid,

    // Native Runtime layout or final Project SHM publication failed.
    project_runtime_failed,

    // One or more configured communication endpoints could not start.
    communication_start_failed,
};

// Returns true only for server_status::success.
[[nodiscard]] constexpr bool succeeded(server_status status) noexcept {
    return status == server_status::success;
}

}
