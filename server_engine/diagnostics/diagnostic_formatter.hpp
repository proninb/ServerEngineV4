/*
 * Clang-style diagnostics formatter.
 *
 * Output contract:
 *
 *   file:line:column: severity: message
 *   <source line>
 *       ^~~~~
 *   detail: <what is wrong>
 *
 * Source text is recovered from diagnostic_collection::sources().
 */
#pragma once

#include "diagnostic_collection.hpp"
#include "diagnostic_registry.hpp"

#include <ostream>

namespace cw::server {

// Writes all records using stable Clang-style text presentation.
void format_diagnostics(
    std::ostream& output,
    const diagnostic_collection& diagnostics,
    diagnostic_registry_view registry = diagnostic_registry);

}
