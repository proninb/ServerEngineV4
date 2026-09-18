/*
 * Process-lifetime state container for one Server instance.
 *
 * server_context is an ownership object. It is not a generic service locator
 * and must not accumulate BUILD/REBUILD-only temporary state.
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

    // Server-owned Project. nullptr exactly represents the UNLOADED state.
    std::unique_ptr<project> project;
};

}
