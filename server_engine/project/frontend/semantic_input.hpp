/*
 * Active semantic token stream.
 *
 * semantic_input replays preprocessing over retained physical lexical state and
 * yields only active C++ tokens to Parser/Semantic. Includes synchronously enter
 * the already-discovered Header while preserving the caller's semantic scope.
 */
#pragma once

#include "directive_executor.hpp"
#include "frontend_input.hpp"
#include "lexer.hpp"
#include "../preprocessor/preprocessor.hpp"
#include "../preprocessor_configuration.hpp"
#include "../string/string_table.hpp"
#include "../file/file_context.hpp"
#include "../../server_status.hpp"
#include "../../string_id.hpp"

#include <cstdint>
#include <string_view>

namespace cw::server {

struct semantic_token final {
    file_id file{};
    token_kind kind = token_kind::invalid;
    std::uint32_t source_offset = 0;
    std::uint32_t source_length = 0;

    // Effective identifier after object-like identifier macro expansion.
    // Invalid for non-identifier tokens.
    string_id identifier{};
};

enum class semantic_input_failure_kind : std::uint8_t {
    none = 0,
    lexical,
    preprocessing,
};

struct semantic_input_failure final {
    semantic_input_failure_kind kind =
        semantic_input_failure_kind::none;
    file_id file{};
    source_range source;
    lexical_error lexical;
    std::string_view detail;
};

// Replays one Project-declared frontend root. Physical lexical storage remains
// owned by lexical_generation; this class owns only bounded traversal and
// preprocessing execution state.
class semantic_input final {
public:
    semantic_input(
        file_context& files,
        lexical_generation& lexical,
        const preprocessor_configuration& configuration,
        string_table& strings) noexcept;

    semantic_input(const semantic_input&) = delete;
    semantic_input& operator=(const semantic_input&) = delete;

    [[nodiscard]] server_status start(
        file_id root) noexcept;

    [[nodiscard]] server_status next(
        semantic_token& output) noexcept;

    [[nodiscard]] bool finished() const noexcept {
        return finished_value;
    }

    [[nodiscard]] const semantic_input_failure&
    failure() const noexcept {
        return failure_value;
    }

private:
    [[nodiscard]] server_status fail(
        file_id file,
        source_range source,
        std::string_view detail,
        server_status status,
        semantic_input_failure_kind kind =
            semantic_input_failure_kind::preprocessing) noexcept;

    [[nodiscard]] server_status consume_directive(
        std::uint32_t word_offset,
        std::uint32_t source_base) noexcept;

    [[nodiscard]] server_status resolve_include(
        const include_request& request,
        file_id& output) noexcept;

    [[nodiscard]] server_status materialize_and_lex(
        file_id file) noexcept;

    [[nodiscard]] server_status effective_identifier(
        const frontend_token& token,
        string_id& output,
        bool& empty) noexcept;

    file_context& files;
    lexical_generation& lexical;
    const preprocessor_configuration& configuration;
    string_table& strings;

    frontend_input input;
    frontend_input directive_input;
    preprocessor preprocessing;
    directive_executor executor;
    lexical_stream include_stream;

    semantic_input_failure failure_value;
    bool started = false;
    bool finished_value = false;
};

}
