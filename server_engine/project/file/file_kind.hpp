/*
 * Construction file syntax-domain classification.
 *
 * One physical file identity has exactly one immutable kind inside a
 * construction lineage. The kind selects the syntax/discovery domain.
 */
#pragma once

#include <cstdint>

namespace cw::server {

enum class file_kind : std::uint8_t {
    project,
    header,
    source,
    assign,
};

}
