/*
 * Transport-neutral response produced for one Server request.
 */
#pragma once

#include "../diagnostics/diagnostic_collection.hpp"
#include "../operation.hpp"
#include "../project/ic/ic_catalog.hpp"
#include "../project/runtime/runtime_query.hpp"
#include "../server_state.hpp"
#include "../server_status.hpp"

namespace cw::server {

enum class server_response_payload_kind : std::uint8_t {
    none = 0,
    state,
    runtime_object,
    runtime_type,
    runtime_link,
    runtime_value,
    ic_catalog,
};

struct server_response {
    bool has_operation_metrics = false;
    std::uint64_t elapsed_us = 0;
    std::uint64_t elapsed_ms = 0, types = 0, objects = 0, links = 0, members = 0, runtime_bytes = 0;
    bool has_project_metrics = false;
    bool link_index_ready = false;
    operation_id operation;
    server_status status = server_status::success;

    server_response_payload_kind payload =
        server_response_payload_kind::none;

    server_state_snapshot state;
    runtime_object_query object;
    runtime_type_query type;
    runtime_link_query link;
    runtime_value value;
    ic_catalog catalog;

    diagnostic_collection diagnostics;
};

}
