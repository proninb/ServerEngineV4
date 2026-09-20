/*
 * Project BUILD pipeline.
 *
 * BUILD transforms the currently resident Project Gn into candidate Gn+1.
 * Persisted construction proof is loaded only for that active Project. On any
 * failure the Server owner discards Gn and transitions to UNLOADED.
 */
#pragma once

#include "project.hpp"
#include "../configuration/server_configuration.hpp"
#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../server_status.hpp"

#include <filesystem>
#include <memory>

namespace cw::server {

[[nodiscard]] server_status build_project(
    const project& resident,
    const server_abi_configuration& abi,
    operation_id operation,
    diagnostic_collection& diagnostics,
    std::unique_ptr<project>& output);

}
