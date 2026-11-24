#pragma once

#include "../frontend/prepared_token.hpp"
#include "../semantic/identity.hpp"
#include "../graph/type_ref.hpp"
#include <unordered_map>
#include <vector>

namespace cw::server {

struct header_template_parameter_v2 { string_id name{}; bool type = false; };

// Construction-lifetime grammar definitions, shared across Header roots.
// Contains prepared tokens, never instantiated semantic fields or a second G.
struct header_template_definition_v2 {
    identity_ref identity{}, scope{};
    file_id root{};
    string_id index_member{}, index_parameter{};
    std::int64_t index_origin = 0;
    std::vector<header_template_parameter_v2> parameters;
    std::vector<prepared_token> tokens;
};

struct header_relative_binding_v2 {
    identity_ref owner{};
    string_id target{}, referent{};
    identity_ref static_referent{};
    file_id file{};
    source_range source{};
    bool address = false;
};

struct header_template_definitions_v2 {
    std::vector<header_relative_binding_v2> relative_bindings;
    std::vector<header_template_definition_v2> records;
    std::unordered_map<std::uint32_t, type_ref> array_aliases;
};

}
