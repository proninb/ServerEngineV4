/*
 * Restricted preprocessing directive execution.
 *
 * directive_executor owns conditional execution state plus the conditional
 * floor of each active frontend file entry. The entry boundary is distinct from
 * file_id because recursive inclusion may enter the same physical file more
 * than once. Include resolution and File Context mutation stay outside here.
 */
#pragma once

#include "directive_decoder.hpp"
#include "../preprocessor_configuration.hpp"
#include "../../server_status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cw::server {

class preprocessor;
class string_table;

inline constexpr std::size_t directive_conditional_depth_limit = 256;

enum class directive_execution_kind : std::uint8_t {
    none = 0,
    include,
};

enum class directive_execution_error_kind : std::uint8_t {
    none = 0,
    malformed_operand,
    invalid_macro_definition,
    unsupported_directive,
    invalid_include,
    file_entry_mismatch,
    unmatched_else,
    duplicate_else,
    unmatched_endif,
    conditional_depth_exceeded,
    unterminated_conditional,
};

struct directive_execution_error final {
    directive_execution_error_kind kind =
        directive_execution_error_kind::none;
    file_id file{};
    source_range source;
};

struct include_request final {
    file_id source{};
    include_form form = include_form::none;
    source_range locator;
};

struct directive_execution_result final {
    directive_execution_kind kind =
        directive_execution_kind::none;
    include_request include;
};

[[nodiscard]] server_status initialize_preprocessor(
    const preprocessor_configuration& configuration,
    string_table& strings,
    preprocessor& state) noexcept;

// Executes the restricted preprocessing contract for one frontend execution.
// The caller supplies the complete source bytes for directive.range.file so
// identifier ranges can be interned without a second lexical representation.
class directive_executor final {
public:
    directive_executor(
        string_table& strings,
        preprocessor& state) noexcept
        : strings(strings),
          state(state) {
    }

    directive_executor(const directive_executor&) = delete;
    directive_executor& operator=(const directive_executor&) = delete;

    // Starts another frontend root using the already-reset mutable
    // preprocessor state. Conditional groups never cross root executions.
    void reset() noexcept {
        conditional_depth = 0;
        file_depth = 0;
    }

    // Records the conditional floor for one physical frontend entry. Recursive
    // inclusion of the same file_id therefore remains a distinct entry.
    [[nodiscard]] server_status enter_file(
        file_id file) noexcept;

    [[nodiscard]] server_status execute(
        const preprocessing_directive& directive,
        std::string_view source,
        directive_execution_result& output,
        directive_execution_error* error = nullptr) noexcept;

    // Validates only conditional groups opened since the matching file entry
    // and then leaves that entry.
    [[nodiscard]] server_status finish_file(
        file_id file,
        directive_execution_error* error = nullptr) noexcept;

    [[nodiscard]] bool active() const noexcept;

    [[nodiscard]] std::size_t depth() const noexcept {
        return conditional_depth;
    }

private:
    struct conditional_frame final {
        file_id file{};
        source_range source;
        bool parent_active = true;
        bool condition = false;
        bool branch_active = false;
        bool else_seen = false;
    };

    struct file_entry_frame final {
        file_id file{};
        std::size_t conditional_floor = 0;
    };

    [[nodiscard]] bool current_file_entry(
        file_id file,
        std::size_t& conditional_floor) const noexcept;

    [[nodiscard]] server_status fail(
        directive_execution_error_kind kind,
        file_id file,
        source_range source,
        directive_execution_error* error) const noexcept;

    [[nodiscard]] server_status begin_conditional(
        const preprocessing_directive& directive,
        std::string_view source,
        bool inverted,
        directive_execution_error* error) noexcept;

    string_table& strings;
    preprocessor& state;
    std::array<conditional_frame, directive_conditional_depth_limit>
        conditionals{};
    std::size_t conditional_depth = 0;

    std::array<file_entry_frame, frontend_include_depth_limit>
        file_entries{};
    std::size_t file_depth = 0;
};

}
