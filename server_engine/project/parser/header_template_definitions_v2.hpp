#pragma once

#include "../frontend/prepared_token.hpp"
#include "../semantic/identity.hpp"
#include <vector>

namespace cw::server {

struct header_template_parameter_v2 { string_id name{}; bool type = false; };

// Construction-lifetime grammar definitions, shared across Header roots.
// Contains prepared tokens, never instantiated semantic fields or a second G.
struct header_template_definition_v2 {
    identity_ref identity{}, scope{};
    file_id root{};
    std::vector<header_template_parameter_v2> parameters;
    std::vector<prepared_token> tokens;
};

struct header_template_definitions_v2 {
    std::vector<header_template_definition_v2> records;
};

}
