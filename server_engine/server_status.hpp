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

    // Requested transport, provider, or operation is known but not implemented yet.
    unsupported,

    // server.license could not be opened/read.
    license_read_failed,

    // server.license violates the supported format/limits contract.
    license_invalid,

    // server.license is no longer valid at Server startup time.
    license_expired,

    // server.lease could not be opened/read.
    server_lease_read_failed,

    // server.lease violates its structural, time, or limit contract.
    server_lease_invalid,

    // server.lease is not valid yet.
    server_lease_not_active,

    // server.lease expired at startup or while the Server was running.
    server_lease_expired,

    // Server process identity initialization or token acquisition failed.
    server_identity_start_failed,

    // Authentication subsystem/provider initialization failed.
    authentication_start_failed,

    // Request origin/client identity is not allowed to submit this operation.
    access_denied,

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

    runtime_query_invalid,
    runtime_query_not_found,

    // One or more configured communication endpoints could not start.
    communication_start_failed,
};

// Returns true only for server_status::success.
[[nodiscard]] constexpr bool succeeded(server_status status) noexcept {
    return status == server_status::success;
}

}
