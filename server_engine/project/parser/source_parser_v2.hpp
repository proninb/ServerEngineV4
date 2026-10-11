/* PARSER-V2-SOURCE-01: restricted Source syntax, direct Graph G producer. */
#pragma once

#include "parser.hpp"

namespace cw::server {

// Source Parser V2 consumes the type domain already in G.  It NEVER resets G,
// identities or source_map.  Caller owns root ordering and finalization.
// The Source domain deliberately has no C++ preprocessor or type declarations.
[[nodiscard]] server_status parse_source_roots_v2(
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

}
