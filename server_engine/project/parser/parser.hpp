/*
 * Direct Header/Source Parser/Semantic.
 *
 * Header execution builds the C++ Type domain with preprocessing/includes;
 * Source execution consumes that completed domain for objects, initialization,
 * and links. Both write directly into one G with no facts/Builder layer.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../frontend/lexical_generation.hpp"
#include "../frontend/directive_decoder.hpp"
#include "../graph/graph.hpp"
#include "../graph/graph_delta.hpp"
#include "../preprocessor_configuration.hpp"
#include "../semantic/identity.hpp"
#include "../source/source_map.hpp"
#include "../string/string_table.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cw::server {

// Bounds source-controlled recursive semantic-scope descent before it can
// consume the process stack.
inline constexpr std::size_t parser_scope_depth_limit = 256;

enum class parser_failure_kind : std::uint8_t {
    none = 0,
    lexical,
    preprocessing,
    syntax,
    semantic,
    unsupported,
};

struct parser_failure final {
    parser_failure_kind kind =
        parser_failure_kind::none;
    file_id file{};
    source_range source;
    std::string_view detail;
};

enum class parser_warning_kind : std::uint8_t {
    duplicate_initialization = 1,
};

struct parser_warning final {
    parser_warning_kind kind =
        parser_warning_kind::duplicate_initialization;
    file_id file{};
    source_range source;
    std::string_view detail;
};

struct semantic_parse_telemetry final {
    bool detailed_source = true;
    std::uint64_t header_ns = 0;
    std::uint64_t source_ns = 0;
    std::uint64_t finalize_ns = 0;
    std::uint64_t include_ns = 0;
    std::uint64_t include_count = 0;
    std::uint64_t include_lexical_ns = 0;
    std::uint64_t prepared_include_count = 0;
    std::uint64_t source_object_ns = 0;
    std::uint64_t source_assignment_ns = 0;
    std::uint64_t source_link_ns = 0;
    std::uint64_t source_endpoint_ns = 0;
    std::uint64_t source_member_ns = 0;

    // GRAPH-RESOLVED-V2-PERF-01. Zero-clock lookup shape.
    std::uint64_t header_member_lookups = 0;
    std::uint64_t header_member_lookup_probes = 0;
    std::uint64_t source_member_lookups = 0;
    std::uint64_t source_member_lookup_probes = 0;
    std::uint64_t source_initialization_commit_ns = 0;
    std::uint64_t source_link_commit_ns = 0;
    std::uint64_t source_provenance_ns = 0;
    std::uint64_t source_decode_ns = 0;
    std::uint64_t source_intern_ns = 0;
    std::uint64_t source_token_count = 0;
    std::uint64_t source_identifier_count = 0;

    // PARSER-PERF-01.
    // Hot primitives are counters only; detailed sub-parser clocks are
    // enabled only when detailed_source is true.
    std::uint64_t header_scope_ns = 0;
    std::uint64_t header_record_ns = 0;
    std::uint64_t header_declared_type_ns = 0;
    std::uint64_t header_type_specifier_ns = 0;
    std::uint64_t header_declarator_ns = 0;

    std::uint64_t header_scope_calls = 0;
    std::uint64_t header_record_count = 0;
    std::uint64_t header_data_members = 0;
    std::uint64_t header_special_member_paths = 0;
    std::uint64_t header_constructors = 0;
    std::uint64_t header_access_labels = 0;

    std::uint64_t header_advance_calls = 0;
    std::uint64_t header_peek_calls = 0;
    std::uint64_t header_at_checks = 0;
    std::uint64_t header_input_next_calls = 0;
    std::uint64_t header_buffered_advance_calls = 0;

    std::uint64_t header_declared_type_calls = 0;
    std::uint64_t header_type_specifier_calls = 0;
    std::uint64_t header_declarator_calls = 0;
    std::uint64_t header_type_identity_lookups = 0;
    std::uint64_t header_type_identity_scope_steps = 0;
    std::uint64_t header_find_type_calls = 0;
    std::uint64_t header_read_type_calls = 0;
    std::uint64_t header_dependency_adds = 0;
    std::uint64_t header_derive_calls = 0;
    std::uint64_t header_declare_record_calls = 0;
    std::uint64_t header_define_record_calls = 0;



    // PUBLISH-SOURCE-COARSE-PROFILE-01.
    // Root-level clocks only; no clocks are read inside Source statement loops.
    std::uint64_t source_root_count = 0;
    std::uint64_t source_root_setup_ns = 0;
    std::uint64_t source_replay_ns = 0;
    std::uint64_t source_root_finish_ns = 0;

    // Zero-clock Source workload shape.
    std::uint64_t source_object_statements = 0;
    std::uint64_t source_value_assignment_statements = 0;
    std::uint64_t source_link_statements = 0;
    std::uint64_t source_string_assignment_statements = 0;
    std::uint64_t source_string_assignment_elements = 0;
    std::uint64_t source_endpoint_count = 0;
    std::uint64_t source_endpoint_steps = 0;

    // PUBLISH-SOURCE-ENDPOINT-SHAPE-02.
    // Zero-clock endpoint/member shape.
    std::uint64_t source_endpoint_direct_members = 0;
    std::uint64_t source_endpoint_path_endpoints = 0;
    std::uint64_t source_endpoint_member_steps = 0;
    std::uint64_t source_endpoint_array_steps = 0;
    std::uint64_t source_endpoint_dereference_steps = 0;
    std::uint64_t source_endpoint_base_steps = 0;
};

[[nodiscard]] server_status parse_semantic_project(
    file_context& files,
    lexical_generation& lexical,
    std::size_t frontend_root_count,
    const preprocessor_configuration& configuration,
    string_table& strings,
    identity_space& identities,
    graph& G,
    source_map& sources,
    parser_failure* failure = nullptr,
    std::vector<parser_warning>* warnings = nullptr,
    semantic_parse_telemetry* telemetry = nullptr) noexcept;

// Replays only selected BUILD semantic roots. Input roots must be unique and
// ascending by file_id; execution is canonical Header pass first, then Source
// pass, matching full REBUILD regardless of dependency-discovery order.
[[nodiscard]] server_status parse_semantic_roots(
    file_context& files,
    lexical_generation& lexical,
    std::span<const file_id> roots,
    const preprocessor_configuration& configuration,
    string_table& strings,
    identity_space& identities,
    graph_delta& G,
    source_map_delta& sources,
    parser_failure* failure = nullptr,
    std::vector<parser_warning>* warnings = nullptr) noexcept;

}
