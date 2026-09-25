/*
 * Strongly typed representation of server.json.
 *
 * This file models configuration only. It does not perform I/O, create
 * communication endpoints, or execute Server lifecycle operations.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cw::server {

// Supported command ingress transport kinds.
enum class transport_kind {
    // Interactive application console using stdin/stdout.
    console,

    // TCP listener transport. The configuration contract exists before the
    // TCP backend is implemented so unsupported use can fail explicitly.
    tcp,
};

// Configuration for one named Server communication endpoint.
struct communication_endpoint_configuration {
    // Unique endpoint name within communication.endpoints.
    std::string name;

    // Transport implementation used by this endpoint.
    transport_kind transport = transport_kind::console;

    // Application protocol name used by transports that require one.
    // Empty for console; currently "json" for TCP.
    std::string protocol;

    // Local bind address for network transports.
    // Empty for console endpoints.
    std::string address;

    // Local network port. Zero for non-network transports.
    std::uint16_t port = 0;
};

// Configuration for all Server command ingress endpoints.
struct communication_configuration {
    // Ordered endpoint declarations materialized during Server startup.
    std::vector<communication_endpoint_configuration> endpoints;
};

// Startup policy for the optional Project.
enum class project_startup_mode {
    // Restore the committed final Project state without source construction.
    load,

    // Compile project.json from source and persist only final compiled.bin.
    publish,

    // Reconstruct the Project plus fresh BUILD acceleration state.
    rebuild,
};

// Optional startup Project entry.
struct project_startup_configuration {
    // Project configuration path. Relative paths resolve against server.json.
    std::filesystem::path path;

    // Startup policy. load is the default when omitted from server.json.
    project_startup_mode startup = project_startup_mode::load;
};

// Human-readable diagnostic logging configuration.
struct logging_configuration {
    // Minimum accepted log severity name.
    std::string level = "info";

    // Enables the console logging sink when true.
    bool console = false;

    // Optional log file path. Presence of a path enables file logging.
    std::optional<std::filesystem::path> file;
};

// Structured operational/performance telemetry configuration.
struct telemetry_configuration {
    // Enables telemetry output to the process console when true.
    bool console = false;

    // Enabled telemetry categories. Filtering affects emission only.
    std::vector<std::string> subsystems;
};

enum class abi_target : std::uint8_t {
    windows_x64 = 1,
    posix_x64 = 2,
};

// Process-wide physical layout contract shared by all Projects and the one SHM.
struct server_abi_configuration final {
    abi_target target = abi_target::windows_x64;
    std::uint32_t pack = 8;
};

// Selects how the one Server SHM participates in native Runtime execution.
enum class shm_runtime_mode : std::uint8_t {
    fixed_direct,
    relocatable_transfer,
};

// Process-wide Runtime/SHM materialization policy.
// Runtime/SHM size is derived from G + ABI and is never configured separately.
struct server_shm_configuration final {
    shm_runtime_mode mode = shm_runtime_mode::fixed_direct;

    // Required only by fixed_direct. Platform mapping code validates whether
    // this x64 virtual address can actually host the Runtime/SHM mapping.
    std::uint64_t fixed_base_address = 0;
};

// Server-wide standard filenames used inside each Project artifact directory.
// These names exist independently of the optional startup Project.
struct server_files_configuration final {
    std::filesystem::path manifest;
    std::filesystem::path source_save;
    std::filesystem::path database;
    std::filesystem::path compiled;
};

// Process-wide low-level Server settings shared by all Project lifecycle modes.
struct server_settings_configuration final {
    server_abi_configuration abi;
    server_shm_configuration shm;
    server_files_configuration files;
};

// Process-level configuration loaded before communication and Project lifecycle begin.
struct server_configuration {
    // server.json schema version. Current schema value is 5.
    std::uint32_t version = 0;

    // Required process-wide low-level Server settings.
    server_settings_configuration settings;

    // Required communication endpoint configuration.
    communication_configuration communication;

    // Optional Project startup request executed after communication startup.
    std::optional<project_startup_configuration> project;

    // Optional-in-JSON logging configuration represented with defaults here.
    logging_configuration logging;

    // Optional telemetry block. Absence means telemetry configuration is disabled.
    std::optional<telemetry_configuration> telemetry;
};

}
