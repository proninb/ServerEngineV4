/*
 * Internal transport-neutral Server request representation.
 *
 * External transports translate their syntax/protocol into this representation
 * before Server execution. Transport code never owns Server semantics.
 */
#pragma once

#include "../project/runtime/runtime_ic.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cw::server {

enum class server_request_kind {
    load,
    publish,
    build,
    unload,
    rebuild,
    get_state,
    get_value,
    snap_ic,
    reset_ic,
    delete_ic,
    list_ic,
    run,
    freeze,
    shutdown,
};

struct server_request {
    server_request_kind kind = server_request_kind::shutdown;

    // Project entry path for lifecycle commands; IC data path for SNAP/RESET.
    std::filesystem::path path;

    // Runtime query target or IC leaf name.
    std::string name;

    snap_ic_options snap_options = snap_ic_options::none;
    reset_ic_options reset_options = reset_ic_options::none;

    // Optional ordered IC group path. Empty means root-level IC.
    std::vector<std::string> group;
};

}
