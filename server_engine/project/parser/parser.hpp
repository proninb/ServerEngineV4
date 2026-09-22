/*
 * Direct C++ declaration Parser/Semantic.
 *
 * Parser consumes active semantic_input tokens, resolves string_id/identity_ref,
 * and writes final semantic state directly into G. No facts, Semantic DB, or
 * Builder representation exists between Parser and graph.
 */
#pragma once

#include "../file/file_context.hpp"
#include "../frontend/lexical_generation.hpp"
#include "../frontend/directive_decoder.hpp"
#include "../graph/graph.hpp"
#include "../preprocessor_configuration.hpp"
#include "../semantic/identity.hpp"
#include "../string/string_table.hpp"
#include "../../server_status.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

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

[[nodiscard]] server_status parse_semantic_project(
    file_context& files,
    lexical_generation& lexical,
    std::size_t frontend_root_count,
    const preprocessor_configuration& configuration,
    string_table& strings,
    identity_space& identities,
    graph& G,
    parser_failure* failure = nullptr) noexcept;

}
