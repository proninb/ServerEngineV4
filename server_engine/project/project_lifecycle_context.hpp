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
#include "../configuration/server_configuration.hpp"

#include <vector>

namespace cw::server {

// Temporary LOAD state; borrows the process-wide Server ABI.
class load_context final {
public:
    explicit load_context(
        const server_abi_configuration& abi) noexcept
        : abi(abi) {
    }

    const server_abi_configuration& abi;
};

// Temporary BUILD state for constructing Gn+1 from resident Gn.
class build_context final {
public:
    explicit build_context(
        const server_abi_configuration& abi) noexcept
        : abi(abi) {
    }

    const server_abi_configuration& abi;
    project_configuration_manifest manifest;

    preprocessor_configuration preprocessor;
};

// Temporary REBUILD state for constructing a fresh G0.
class rebuild_context final {
public:
    explicit rebuild_context(
        const server_abi_configuration& abi) noexcept
        : abi(abi) {
    }

    const server_abi_configuration& abi;
    project_configuration_manifest manifest;
    file_context files;
    lexical_generation lexical;

    preprocessor_configuration preprocessor;
};

}
