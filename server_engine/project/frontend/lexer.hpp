/*
 * Parallel per-file construction lexer.
 *
 * lexer reads one immutable physical file and produces one private compact
 * lexical_stream. It has no shared mutable state and performs no textual or
 * semantic interning, so independent files may be lexed concurrently.
 */
#pragma once

#include "lexical_stream.hpp"

#include <string_view>

namespace cw::server {

class lexer final {
public:
    [[nodiscard]] static server_status tokenize(
        file_id file,
        std::string_view source,
        lexical_stream& output) noexcept;
};

}
