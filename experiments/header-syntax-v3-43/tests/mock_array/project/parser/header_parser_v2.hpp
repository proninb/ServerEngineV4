#pragma once
#include "server_engine/project/frontend/lexical_token.hpp"
#include <string_view>
namespace cw::server {
struct parser_v2_failure {std::string_view detail;};
struct identity_space;
struct graph;
class header_parser_v2 {
 semantic_preprocessor_v2& input;
public:
 header_parser_v2(semantic_preprocessor_v2& p,identity_space&,graph&,parser_v2_failure*):input(p){}
 server_status parse() { while(input.current()) { if(!succeeded(input.advance())) return server_status::fail; } return server_status::success; }
};
}
