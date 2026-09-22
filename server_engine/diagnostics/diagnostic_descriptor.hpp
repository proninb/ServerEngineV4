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

// server.json is syntactically valid but violates the V4 schema.
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

// LOAD, BUILD, or REBUILD was requested while another Project is already active.
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



inline constexpr diagnostic_descriptor project_semantic_error{
    diagnostic_id{2025},
    diagnostic_domain::parser,
    diagnostic_severity::error,
    "project.semantic_error",
    "Project source semantic construction failed",
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

} // namespace diagnostics

}
