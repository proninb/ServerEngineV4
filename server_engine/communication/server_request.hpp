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
    get_object,
    get_type,
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

    // Project entry path for lifecycle commands; IC identity for IC commands.
    std::filesystem::path path;

    // Runtime query target.
    std::string name;

    // Optional Runtime query type filter.
    std::string type;

    // Batch object targets used by GET_TYPE.
    std::vector<std::string> objects;

    snap_ic_options snap_options = snap_ic_options::none;
    reset_ic_options reset_options = reset_ic_options::none;

 };

}
