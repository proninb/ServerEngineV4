/*
 * Process-lifetime state container for one Server instance.
 *
 * server_context owns only process-lifetime Server state. It is not a service
 * locator and must never retain project_context, Source Manager, parser,
 * Builder, or other BUILD/REBUILD-only construction state.
 */
#pragma once

#include "communication/command_queue.hpp"
#include "communication/communication.hpp"
#include "configuration/server_configuration.hpp"
#include "project/project.hpp"

#include <filesystem>
#include <memory>

namespace cw::server {

// Owns mutable state and subsystems whose lifetime is bounded by one Server instance.
class server_context final {
public:
    // Parsed server.json configuration currently governing the process.
    server_configuration configuration;

    // Absolute normalized path to the active server.json file.
    std::filesystem::path configuration_path;

    // Parent directory used as the base for relative Server-owned paths.
    std::filesystem::path configuration_directory;

    // Common ingress queue used by communication endpoint producers.
    command_queue commands;

    // Owner of all endpoints materialized from configuration.communication.
    communication communications;

    // Resident Project runtime state. nullptr exactly represents UNLOADED.
    // Construction-only Project state is never stored here.
    std::unique_ptr<project> project;
};

}
