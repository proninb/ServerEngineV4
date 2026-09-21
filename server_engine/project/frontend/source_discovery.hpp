/*
 * Project source-closure construction.
 *
 * Physical Header/Source bytes are materialized before lexing. Independent
 * physical files are lexed through persistent CPU-lane pools; directive
 * execution walks only sparse lexer-recorded directives. Identity-producing
 * preprocessing execution is single-owner and ordered by initial file_id, so
 * CPU count cannot change file_id or string_id assignment order.
 *
 * This stage deliberately does not finalize File Context topology. Assign and
 * any later dependency-producing domains must finish before the single terminal
 * finalize_dependency_topology() boundary.
 */
#pragma once

#include "directive_executor.hpp"
#include "lexical_generation.hpp"
#include "../preprocessor_configuration.hpp"
#include "../string/string_table.hpp"
#include "../../server_status.hpp"

#include <cstdint>

namespace cw::server {

enum class source_discovery_failure_kind : std::uint8_t {
    none = 0,
    lexical,
    directive,
    invalid_include,
    unsupported_include_form,
    include_resolution,
    include_depth_exceeded,
};

struct source_discovery_failure final {
    source_discovery_failure_kind kind =
        source_discovery_failure_kind::none;
    file_id file{};
    source_range source;
    lexical_error lexical;
    directive_execution_error_kind directive =
        directive_execution_error_kind::none;
};

[[nodiscard]] server_status discover_source_closure(
    file_context& files,
    lexical_generation& lexical,
    const preprocessor_configuration& configuration,
    string_table& strings,
    source_discovery_failure* failure = nullptr) noexcept;

}
