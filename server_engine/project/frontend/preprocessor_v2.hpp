/*
 * Parser V2 restricted semantic preprocessor.
 *
 * Physical include resolution/read/lex has already completed. This layer owns
 * only active directive execution, macro state, dependency publication and the
 * streaming include stack consumed by Header Parser V2.
 */
#pragma once

#include "prepared_include_v2.hpp"
#include "semantic_cursor_v2.hpp"
#include "../preprocessor/preprocessor.hpp"
#include "../preprocessor_configuration.hpp"
#include "../../server_status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace cw::server {

class file_context;
class string_table;

inline constexpr std::size_t preprocessor_v2_include_depth_limit = 256;
inline constexpr std::size_t preprocessor_v2_conditional_depth_limit = 256;

enum class preprocessor_v2_failure_kind : std::uint8_t {
    none = 0,
    invalid_input,
    malformed_directive,
    unsupported_directive,
    active_include_missing,
    active_include_invalid,
    include_depth_exceeded,
    conditional_mismatch,
};

struct preprocessor_v2_failure final {
    preprocessor_v2_failure_kind kind =
        preprocessor_v2_failure_kind::none;
    file_id file{};
    source_range source;
    std::string_view detail;
};

class semantic_preprocessor_v2 final {
public:
    semantic_preprocessor_v2(
        file_context& files,
        prepared_include_view_v2 prepared,
        const preprocessor_configuration& configuration,
        string_table& strings,
        preprocessor_v2_failure* failure = nullptr) noexcept;

    semantic_preprocessor_v2(
        const semantic_preprocessor_v2&) = delete;

    semantic_preprocessor_v2& operator=(
        const semantic_preprocessor_v2&) = delete;

    [[nodiscard]] server_status start(
        file_id root) noexcept;

    [[nodiscard]] bool finished() const noexcept {
        return started &&
            !current_valid &&
            frame_depth == 0;
    }

    [[nodiscard]] const prepared_token* current() const noexcept {
        return current_valid
            ? &current_value
            : nullptr;
    }

    [[nodiscard]] bool at(
        token_kind kind) const noexcept {

        return current_valid &&
            current_value.kind == kind;
    }

    [[nodiscard]] server_status advance() noexcept;

private:
    struct frame final {
        file_id file{};
        semantic_cursor_v2 cursor;
        std::size_t conditional_floor = 0;
        std::size_t include_occurrence = 0;
    };

    struct conditional_frame final {
        file_id file{};
        source_range source;
        bool parent_active = true;
        bool condition = false;
        bool branch_active = false;
        bool else_seen = false;
    };

    struct resolution_record final {
        file_id file{};
        lexical_symbol_resolution_v2 resolution;
    };

    struct resolution_slot final {
        file_id file{};
        std::uint32_t record = 0;
    };

    [[nodiscard]] static std::uint32_t file_hash(
        std::uint32_t value) noexcept;

    [[nodiscard]] server_status fail(
        preprocessor_v2_failure_kind kind,
        file_id file,
        source_range source,
        std::string_view detail,
        server_status status =
            server_status::project_configuration_invalid) noexcept;

    [[nodiscard]] bool active() const noexcept;

    [[nodiscard]] server_status initialize_configuration() noexcept;

    [[nodiscard]] lexical_symbol_resolution_v2* find_resolution(
        file_id file) noexcept;

    [[nodiscard]] server_status ensure_resolution_index_capacity(
        std::size_t additional) noexcept;

    void insert_resolution_index(
        std::vector<resolution_slot>& index,
        file_id file,
        std::uint32_t record) const noexcept;

    [[nodiscard]] server_status ensure_resolution(
        file_id file,
        lexical_symbol_resolution_v2*& output) noexcept;

    [[nodiscard]] bool once_contains(
        file_id file) const noexcept;

    [[nodiscard]] server_status ensure_once_capacity(
        std::size_t additional) noexcept;

    [[nodiscard]] server_status mark_once(
        file_id file) noexcept;

    [[nodiscard]] server_status enter_file(
        file_id file) noexcept;

    [[nodiscard]] server_status leave_file() noexcept;

    [[nodiscard]] server_status seek_next() noexcept;

    [[nodiscard]] server_status consume_directive(
        frame& input) noexcept;

    [[nodiscard]] server_status consume_to_end(
        frame& input) noexcept;

    [[nodiscard]] server_status parse_one_identifier(
        frame& input,
        string_id& identifier) noexcept;

    [[nodiscard]] server_status parse_define(
        frame& input,
        file_id file,
        source_range source) noexcept;

    [[nodiscard]] server_status parse_undef(
        frame& input,
        file_id file,
        source_range source) noexcept;

    [[nodiscard]] server_status begin_conditional(
        frame& input,
        file_id file,
        source_range source,
        bool inverted) noexcept;

    [[nodiscard]] server_status execute_include(
        frame& input,
        file_id file,
        source_range source,
        std::size_t occurrence) noexcept;

    file_context& files;
    prepared_include_view_v2 prepared;
    const preprocessor_configuration& configuration;
    string_table& strings;

    preprocessor preprocessing;

    preprocessor_v2_failure* failure = nullptr;

    std::array<frame, preprocessor_v2_include_depth_limit>
        frames{};
    std::size_t frame_depth = 0;

    std::array<conditional_frame, preprocessor_v2_conditional_depth_limit>
        conditionals{};
    std::size_t conditional_depth = 0;

    std::vector<std::unique_ptr<resolution_record>> resolutions;
    std::vector<resolution_slot> resolution_index;

    std::vector<file_id> once_index;
    std::size_t once_count = 0;

    prepared_token current_value;
    bool started = false;
    bool current_valid = false;
};

}
