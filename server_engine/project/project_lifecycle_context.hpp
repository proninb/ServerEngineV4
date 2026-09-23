/*
 * Mode-specific temporary Project lifecycle state.
 *
 * LOAD, BUILD, and REBUILD deliberately have separate contexts. There is no
 * universal builder context; each mode owns only the temporary state it needs.
 */
#pragma once

#include "project_configuration_manifest.hpp"
#include "assign/assign_table.hpp"
#include "preprocessor_configuration.hpp"
#include "file/file_context.hpp"
#include "frontend/lexical_generation.hpp"
#include "graph/graph.hpp"
#include "persistence/compiled_project.hpp"
#include "persistence/database.hpp"
#include "persistence/source_save.hpp"
#include "semantic/identity.hpp"
#include "source/source_map.hpp"
#include "string/string_table.hpp"
#include "../configuration/server_configuration.hpp"
#include "../read_only_file_mapping.hpp"

namespace cw::server {

class load_context final {
public:
    explicit load_context(
        const server_settings_configuration& settings) noexcept
        : settings(settings) {
    }

    const server_settings_configuration& settings;
};

class build_context final {
public:
    explicit build_context(
        const server_settings_configuration& settings) noexcept
        : settings(settings),
          identities(strings) {
    }

    const server_settings_configuration& settings;
    project_configuration_manifest manifest;

    read_only_file_mapping manifest_mapping;
    read_only_file_mapping source_mapping;
    read_only_file_mapping database_mapping;
    read_only_file_mapping compiled_mapping;

    source_save_view source;
    file_context files;
    database_view database;
    compiled_project_view compiled;

    string_table strings;
    identity_space identities;

    preprocessor_configuration preprocessor;
};

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
    source_map sources;
    assign_table assigns;

    preprocessor_configuration preprocessor;
};

}
