/*
 * Semantic token stream shared by the two frontend syntax domains.
 *
 * Header mode replays C++ preprocessing/includes over retained lexical state.
 * Source mode exposes retained lexical tokens without C++ preprocessing;
 * preprocessing directives fail closed in the Source domain.
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
#include <unordered_set>
#include <unordered_map>
#include "../construction/execution_lanes.hpp"

namespace cw::server {

enum class semantic_input_mode : std::uint8_t {
    header,
    source,
};

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

struct semantic_input_telemetry final {
    bool detailed_source = true;
    std::uint64_t include_ns = 0;
    std::uint64_t include_count = 0;
    std::uint64_t include_lexical_ns = 0;
    std::uint64_t prepared_include_count = 0;
    std::uint64_t source_decode_ns = 0;
    std::uint64_t source_intern_ns = 0;
    std::uint64_t source_token_count = 0;
    std::uint64_t source_identifier_count = 0;
};

// Replays one Project-declared frontend root. Physical lexical storage remains
// owned by lexical_generation; preprocessing and include caches are local to
// this construction, never persisted into the Project.
class semantic_input final {
public:
    semantic_input(
        file_context& files,
        lexical_generation& lexical,
        const preprocessor_configuration& configuration,
        string_table& strings,
        semantic_input_telemetry* telemetry = nullptr) noexcept;

    semantic_input(const semantic_input&) = delete;
    semantic_input& operator=(const semantic_input&) = delete;

    [[nodiscard]] server_status start(
        file_id root,
        semantic_input_mode mode) noexcept;

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
    struct prepared_include final {
        std::filesystem::path path;
        file_acquire_result acquired;
        lexical_stream stream;
        bool ready = false;
    };
    void prepare_includes(file_id file) noexcept;
    static void prepare_include_lane(void* context, std::size_t lane) noexcept;
    std::vector<prepared_include> include_frontier;
    std::unordered_map<std::filesystem::path, prepared_include> prepared_includes;
    std::unordered_set<std::filesystem::path> preparation_attempted;
    std::unordered_set<std::uint32_t> preparation_scanned;
    execution_lanes include_workers;
    std::size_t include_lane_count = 0;

    [[nodiscard]] server_status fail(
        file_id file,
        source_range source,
        std::string_view detail,
        server_status status,
        semantic_input_failure_kind kind =
            semantic_input_failure_kind::preprocessing) noexcept;

    [[nodiscard]] server_status next_source(
        semantic_token& output) noexcept;

    [[nodiscard]] server_status next_passthrough_header(
        semantic_token& output) noexcept;

    [[nodiscard]] server_status next_preprocessed_header(
        semantic_token& output) noexcept;

    [[nodiscard]] server_status consume_directive(
        std::uint32_t word_offset,
        std::uint32_t source_base) noexcept;

    [[nodiscard]] server_status resolve_include(
        const include_request& request,
        file_id& output) noexcept;

    [[nodiscard]] server_status materialize_and_lex(
        file_id file) noexcept;

    [[nodiscard]] server_status physical_identifier(
        const frontend_token& token,
        string_id& output) noexcept;

    [[nodiscard]] server_status effective_identifier(
        const frontend_token& token,
        string_id& output,
        bool& empty) noexcept;

    file_context& files;
    lexical_generation& lexical;
    const preprocessor_configuration& configuration;
    string_table& strings;

    // Source has no include stack or lexical mutation while a root is consumed.
    lexical_word_view source_words;
    std::span<const std::uint32_t> source_native_words;
    file_id source_root{};
    std::uint32_t source_word_offset = 0;
    std::uint32_t source_offset = 0;
    frontend_input input;
    frontend_input directive_input;
    preprocessor preprocessing;
    directive_executor executor;
    lexical_stream include_stream;
    // Physical file identities protected by an executed pragma, per root replay.
    std::unordered_set<std::uint32_t> once_files;
    // Successful resolutions belong to this construction's file snapshot.
    // Keep quote/angle search precedence distinct and still stage every edge.
    std::unordered_map<std::filesystem::path, file_id> quoted_includes;
    std::unordered_map<std::filesystem::path, file_id> angled_includes;

    semantic_input_failure failure_value;
    semantic_input_mode mode_value =
        semantic_input_mode::header;

    file_id source_cache_file{};
    std::string_view source_cache;

    bool header_passthrough = false;
    bool started = false;
    bool finished_value = false;
    semantic_input_telemetry* telemetry = nullptr;
};

}
