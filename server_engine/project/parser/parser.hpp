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
    std::uint64_t source_member_lookups = 0;
    std::uint64_t source_initialization_commit_ns = 0;
    std::uint64_t source_link_commit_ns = 0;
    std::uint64_t source_provenance_ns = 0;
    std::uint64_t source_decode_ns = 0;
    std::uint64_t source_intern_ns = 0;
    std::uint64_t source_token_count = 0;
    std::uint64_t source_identifier_count = 0;


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
