/* PARSER-V2-SOURCE-01: restricted Source syntax, direct Graph G producer. */
#pragma once

#include "parser.hpp"
#include <memory>

namespace cw::server {

// Owns captured Source descriptors and joins on every exit path. Caller keeps
// source bytes and lexical backing storage alive and immutable until join.
// Header semantics may mutate Graph, strings and dependency topology; workers
// read only captured spans, never file_context/lexical_generation metadata.
class source_preparation_v2 final {
public:
    source_preparation_v2() noexcept;
    ~source_preparation_v2() noexcept;
    source_preparation_v2(const source_preparation_v2&) = delete;
    source_preparation_v2& operator=(const source_preparation_v2&) = delete;
    [[nodiscard]] bool start(const file_context&, const lexical_generation&,
        std::size_t root_count) noexcept;
private:
    struct state;
    std::unique_ptr<state> value;
    friend server_status parse_source_roots_v2(file_context&, lexical_generation&,
        std::size_t, const preprocessor_configuration&, string_table&,
        identity_space&, graph&, source_map&, parser_failure*,
        std::vector<parser_warning>*, semantic_parse_telemetry*, source_preparation_v2*) noexcept;
};

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
    semantic_parse_telemetry* telemetry = nullptr,
    source_preparation_v2* preparation = nullptr) noexcept;

}
