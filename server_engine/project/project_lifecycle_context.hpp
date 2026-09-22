/*
 * Mode-specific temporary Project lifecycle state.
 *
 * LOAD, BUILD, and REBUILD deliberately have separate contexts. There is no
 * universal builder context; each mode owns only the temporary state it needs.
 */
#pragma once

#include "project_configuration_manifest.hpp"
#include "preprocessor_configuration.hpp"
#include "file/file_context.hpp"
#include "frontend/lexical_generation.hpp"
#include "graph/graph.hpp"
#include "semantic/identity.hpp"
#include "string/string_table.hpp"
#include "../configuration/server_configuration.hpp"

namespace cw::server {

// Temporary LOAD state; borrows process-wide Server settings.
class load_context final {
public:
    explicit load_context(
        const server_settings_configuration& settings) noexcept
        : settings(settings) {
    }

    const server_settings_configuration& settings;
};

// Temporary BUILD state over persisted construction state.
class build_context final {
public:
    explicit build_context(
        const server_settings_configuration& settings) noexcept
        : settings(settings) {
    }

    const server_settings_configuration& settings;
    project_configuration_manifest manifest;

    preprocessor_configuration preprocessor;
};

// Temporary REBUILD state for constructing a fresh Project lineage.
class rebuild_context final {
public:
    explicit rebuild_context(
        const server_settings_configuration& settings) noexcept
        : settings(settings),
          identities(strings) {
    }

    const server_settings_configuration& settings;
    project_configuration_manifest manifest;
    file_change_checkpoint change_checkpoint;
    file_context files;
    lexical_generation lexical;
    string_table strings;
    identity_space identities;
    graph G;

    preprocessor_configuration preprocessor;
};

}
