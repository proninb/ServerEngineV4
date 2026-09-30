/*
 * Immutable compile-time diagnostic catalog entries.
 *
 * IDs and names are stable contracts intended for logs, clients, tests,
 * and future TCP/JSON responses. Human-readable message text may evolve.
 */
#pragma once

#include "diagnostic.hpp"

#include <string_view>

namespace cw::server {

// Static definition shared by every occurrence of one diagnostic.
struct diagnostic_descriptor {
    // Stable numeric identifier.
    diagnostic_id id;

    // Architectural owner of the diagnostic.
    diagnostic_domain domain = diagnostic_domain::unknown;

    // Severity applied by default when emitting this diagnostic.
    diagnostic_severity default_severity = diagnostic_severity::error;

    // Stable machine-readable symbolic name.
    std::string_view name;

    // Default human-readable message.
    std::string_view message;
};

namespace diagnostics {

// Server startup could not complete because of an unexpected internal failure.
inline constexpr diagnostic_descriptor server_initialization_failed{
    diagnostic_id{1001},
    diagnostic_domain::server,
    diagnostic_severity::fatal,
    "server.initialization_failed",
    "Server initialization failed",
};

// server.json could not be opened/read.
inline constexpr diagnostic_descriptor configuration_read_failed{
    diagnostic_id{1101},
    diagnostic_domain::configuration,
    diagnostic_severity::error,
    "configuration.read_failed",
    "Server configuration could not be read",
};

// server.json contains malformed JSON/JSON-with-comments syntax.
inline constexpr diagnostic_descriptor configuration_invalid_json{
    diagnostic_id{1102},
    diagnostic_domain::configuration,
    diagnostic_severity::error,
    "configuration.invalid_json",
    "Server configuration contains invalid JSON",
};

// server.json is syntactically valid but violates the current schema.
inline constexpr diagnostic_descriptor configuration_invalid{
    diagnostic_id{1103},
    diagnostic_domain::configuration,
    diagnostic_severity::error,
    "configuration.invalid",
    "Server configuration is invalid",
};

// server.json uses a schema version not supported by this executable.
inline constexpr diagnostic_descriptor configuration_unsupported_version{
    diagnostic_id{1104},
    diagnostic_domain::configuration,
    diagnostic_severity::error,
    "configuration.unsupported_version",
    "Server configuration version is unsupported",
};

// server.license could not be opened/read.
inline constexpr diagnostic_descriptor license_read_failed{
    diagnostic_id{1121},
    diagnostic_domain::license,
    diagnostic_severity::fatal,
    "license.read_failed",
    "server.license could not be read",
};

// server.license violates the supported structural/value contract.
inline constexpr diagnostic_descriptor license_invalid{
    diagnostic_id{1122},
    diagnostic_domain::license,
    diagnostic_severity::fatal,
    "license.invalid",
    "server.license is invalid",
};

// server.license expiration has been reached.
inline constexpr diagnostic_descriptor license_expired{
    diagnostic_id{1123},
    diagnostic_domain::license,
    diagnostic_severity::fatal,
    "license.expired",
    "server.license has expired",
};

inline constexpr diagnostic_descriptor server_lease_read_failed{
    diagnostic_id{1124},
    diagnostic_domain::license,
    diagnostic_severity::fatal,
    "license.lease_read_failed",
    "server.lease could not be read",
};

inline constexpr diagnostic_descriptor server_lease_invalid{
    diagnostic_id{1125},
    diagnostic_domain::license,
    diagnostic_severity::fatal,
    "license.lease_invalid",
    "server.lease is invalid",
};

inline constexpr diagnostic_descriptor server_lease_not_active{
    diagnostic_id{1126},
    diagnostic_domain::license,
    diagnostic_severity::fatal,
    "license.lease_not_active",
    "server.lease is not active yet",
};

inline constexpr diagnostic_descriptor server_lease_expired{
    diagnostic_id{1127},
    diagnostic_domain::license,
    diagnostic_severity::fatal,
    "license.lease_expired",
    "server.lease has expired",
};

inline constexpr diagnostic_descriptor server_identity_start_failed{
    diagnostic_id{1171},
    diagnostic_domain::server,
    diagnostic_severity::fatal,
    "server.identity_start_failed",
    "Server identity could not be established",
};

inline constexpr diagnostic_descriptor server_identity_unsupported{
    diagnostic_id{1172},
    diagnostic_domain::server,
    diagnostic_severity::fatal,
    "server.identity_unsupported",
    "Server identity provider is not implemented on this platform",
};

// Authentication subsystem could not initialize the configured provider.
inline constexpr diagnostic_descriptor authentication_start_failed{
    diagnostic_id{1151},
    diagnostic_domain::authentication,
    diagnostic_severity::error,
    "authentication.start_failed",
    "Authentication subsystem could not be started",
};

// Configuration requests an Authentication mode whose provider is not implemented.
inline constexpr diagnostic_descriptor authentication_unsupported_mode{
    diagnostic_id{1152},
    diagnostic_domain::authentication,
    diagnostic_severity::error,
    "authentication.unsupported_mode",
    "Authentication mode is not implemented",
};

// Authentication could not establish FULL operation; Server continues in DEMO.
inline constexpr diagnostic_descriptor server_demo_mode{
    diagnostic_id{1003},
    diagnostic_domain::server,
    diagnostic_severity::warning,
    "server.demo_mode",
    "Server is running in DEMO mode",
};

// Server Policy rejected a request from its presented origin/client identity.
inline constexpr diagnostic_descriptor server_policy_denied{
    diagnostic_id{1002},
    diagnostic_domain::server,
    diagnostic_severity::error,
    "server.policy_denied",
    "Server Policy denied the request",
};

// A configured communication endpoint could not be started.
inline constexpr diagnostic_descriptor communication_start_failed{
    diagnostic_id{1201},
    diagnostic_domain::communication,
    diagnostic_severity::error,
    "communication.start_failed",
    "Communication endpoint could not be started",
};

// Configuration requests a known transport that has no backend yet.
inline constexpr diagnostic_descriptor communication_unsupported_transport{
    diagnostic_id{1202},
    diagnostic_domain::communication,
    diagnostic_severity::error,
    "communication.unsupported_transport",
    "Communication transport is not implemented",
};

// LOAD, PUBLISH, BUILD, or REBUILD was requested while another Project is already active.
inline constexpr diagnostic_descriptor project_already_loaded{
    diagnostic_id{2001},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.already_loaded",
    "A Project is already loaded",
};

// UNLOAD was requested while the Server is already UNLOADED.
inline constexpr diagnostic_descriptor project_not_loaded{
    diagnostic_id{2002},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.not_loaded",
    "No Project is loaded",
};

// Project configuration could not be opened during bootstrap LOAD.
inline constexpr diagnostic_descriptor project_load_failed{
    diagnostic_id{2003},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.load_failed",
    "Project configuration could not be loaded",
};

// LOAD cannot proceed until committed generation restore is implemented.
inline constexpr diagnostic_descriptor project_load_incomplete{
    diagnostic_id{2009},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.load_incomplete",
    "Project LOAD pipeline is incomplete",
};

// BUILD reached the first construction stage that is not implemented yet.
inline constexpr diagnostic_descriptor project_build_incomplete{
    diagnostic_id{2007},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.build_incomplete",
    "Project BUILD pipeline is incomplete",
};

// REBUILD reached the first construction stage that is not implemented yet.
inline constexpr diagnostic_descriptor project_rebuild_incomplete{
    diagnostic_id{2008},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.rebuild_incomplete",
    "Project REBUILD pipeline is incomplete",
};

// Startup BUILD/REBUILD was requested before those execution paths exist.
inline constexpr diagnostic_descriptor project_startup_unsupported{
    diagnostic_id{2004},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.startup_unsupported",
    "Configured Project startup mode is not implemented",
};

inline constexpr diagnostic_descriptor project_invalid_json{
    diagnostic_id{2005},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.invalid_json",
    "Project configuration contains invalid JSON",
};

inline constexpr diagnostic_descriptor project_invalid_configuration{
    diagnostic_id{2006},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.invalid_configuration",
    "Project configuration is invalid",
};

inline constexpr diagnostic_descriptor project_configuration_read_failed{
    diagnostic_id{2011},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.configuration_read_failed",
    "Project configuration input could not be read",
};

inline constexpr diagnostic_descriptor project_configuration_cycle{
    diagnostic_id{2012},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.configuration_cycle",
    "Project configuration contains a recursive Project cycle",
};

inline constexpr diagnostic_descriptor project_manifest_invalid{
    diagnostic_id{2013},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.manifest_invalid",
    "Persisted Project configuration manifest is invalid",
};

inline constexpr diagnostic_descriptor project_manifest_io_failed{
    diagnostic_id{2014},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.manifest_io_failed",
    "Project configuration manifest I/O failed",
};

inline constexpr diagnostic_descriptor project_manifest_missing{
    diagnostic_id{2015},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.manifest_missing",
    "Committed Project configuration manifest is missing",
};

inline constexpr diagnostic_descriptor project_duplicate_construction_input{
    diagnostic_id{2016},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.duplicate_construction_input",
    "Project construction input is duplicated",
};

inline constexpr diagnostic_descriptor project_lexical_error{
    diagnostic_id{2017},
    diagnostic_domain::parser,
    diagnostic_severity::error,
    "project.lexical_error",
    "Project source contains a lexical error",
};

inline constexpr diagnostic_descriptor project_configuration_depth_exceeded{
    diagnostic_id{2018},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.configuration_depth_exceeded",
    "Project configuration nesting depth exceeds the supported limit",
};

inline constexpr diagnostic_descriptor project_preprocessing_error{
    diagnostic_id{2019},
    diagnostic_domain::parser,
    diagnostic_severity::error,
    "project.preprocessing_error",
    "Project source preprocessing failed",
};

inline constexpr diagnostic_descriptor project_rebuild_cleanup_failed{
    diagnostic_id{2020},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.rebuild_cleanup_failed",
    "REBUILD artifact cleanup failed",
};

inline constexpr diagnostic_descriptor project_publish_cleanup_failed{
    diagnostic_id{2031},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.publish_cleanup_failed",
    "PUBLISH artifact cleanup failed",
};

inline constexpr diagnostic_descriptor project_semantic_error{
    diagnostic_id{2025},
    diagnostic_domain::parser,
    diagnostic_severity::error,
    "project.semantic_error",
    "Project source semantic construction failed",
};

inline constexpr diagnostic_descriptor project_duplicate_initialization{
    diagnostic_id{2034},
    diagnostic_domain::parser,
    diagnostic_severity::warning,
    "project.duplicate_initialization",
    "Object member is initialized more than once",
};

inline constexpr diagnostic_descriptor project_assign_invalid{
    diagnostic_id{2026},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.assign_invalid",
    "Project Assign input is invalid",
};

inline constexpr diagnostic_descriptor project_compiled_invalid{
    diagnostic_id{2027},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.compiled_invalid",
    "Compiled Project artifact is invalid",
};

inline constexpr diagnostic_descriptor project_compiled_io_failed{
    diagnostic_id{2028},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.compiled_io_failed",
    "Compiled Project artifact I/O failed",
};

inline constexpr diagnostic_descriptor project_source_save_invalid{
    diagnostic_id{2023},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.source_save_invalid",
    "Committed Project SourceSave is invalid",
};

inline constexpr diagnostic_descriptor project_source_save_io_failed{
    diagnostic_id{2024},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.source_save_io_failed",
    "Committed Project SourceSave I/O failed",
};

inline constexpr diagnostic_descriptor project_database_invalid{
    diagnostic_id{2029},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.database_invalid",
    "Committed Project database is invalid",
};

inline constexpr diagnostic_descriptor project_database_io_failed{
    diagnostic_id{2030},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.database_io_failed",
    "Committed Project database I/O failed",
};

inline constexpr diagnostic_descriptor project_runtime_unsupported{
    diagnostic_id{2032},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.runtime_unsupported",
    "Configured Project Runtime mode is not implemented",
};

inline constexpr diagnostic_descriptor project_runtime_failed{
    diagnostic_id{2033},
    diagnostic_domain::project,
    diagnostic_severity::error,
    "project.runtime_failed",
    "Project Runtime/SHM publication failed",
};

inline constexpr diagnostic_descriptor runtime_query_invalid{
    diagnostic_id{3001},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.query_invalid",
    "Runtime query is invalid",
};

inline constexpr diagnostic_descriptor runtime_query_not_found{
    diagnostic_id{3002},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.query_not_found",
    "Runtime query target was not found",
};

inline constexpr diagnostic_descriptor runtime_query_unsupported{
    diagnostic_id{3003},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.query_unsupported",
    "Runtime query target type is not supported",
};

inline constexpr diagnostic_descriptor runtime_query_failed{
    diagnostic_id{3004},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.query_failed",
    "Runtime query could not read a valid Runtime value",
};

inline constexpr diagnostic_descriptor runtime_ic_invalid{
    diagnostic_id{3011},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.ic_invalid",
    "Runtime IC image or request is invalid",
};

inline constexpr diagnostic_descriptor runtime_ic_not_found{
    diagnostic_id{3012},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.ic_not_found",
    "Runtime IC file or semantic target was not found",
};

inline constexpr diagnostic_descriptor runtime_ic_type_mismatch{
    diagnostic_id{3013},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.ic_type_mismatch",
    "Runtime IC value is incompatible with the current Runtime type",
};

inline constexpr diagnostic_descriptor runtime_ic_failed{
    diagnostic_id{3014},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.ic_failed",
    "Runtime IC operation failed",
};

inline constexpr diagnostic_descriptor runtime_ic_io_failed{
    diagnostic_id{3015},
    diagnostic_domain::persistence,
    diagnostic_severity::error,
    "runtime.ic_io_failed",
    "Runtime IC file I/O failed",
};

inline constexpr diagnostic_descriptor runtime_state_invalid{
    diagnostic_id{3021},
    diagnostic_domain::runtime,
    diagnostic_severity::error,
    "runtime.state_invalid",
    "Runtime control request is invalid in the current Project state",
};

} // namespace diagnostics

}
