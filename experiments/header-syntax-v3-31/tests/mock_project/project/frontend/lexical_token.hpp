#pragma once
#include "server_engine/project/frontend/lexical_token.hpp"
namespace cw::server {
class lexical_token{
 uint32_t word=0;
public:
 static constexpr uint32_t extended_delta=65535,extended_length=255;
 static lexical_token from_value(uint32_t w){lexical_token t;t.word=w;return t;}
 explicit operator bool()const {return word!=0;}
 token_kind kind()const{return static_cast<token_kind>(word>>24);}
 uint32_t delta()const{return (word>>8)&0xffff;}
 uint32_t length()const{return word&255;}
};
}
