#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <cstdint>
#include <utility>
#include "server_engine/project/frontend/lexical_token.hpp"
namespace cw::server {
class file_id {
    uint32_t id=0;
public:
    file_id()=default;
    explicit file_id(uint32_t v):id(v){}
    explicit operator bool()const{return id!=0;}
    uint32_t value()const{return id;}
};
enum class server_status{success,fail};
inline bool succeeded(server_status s){return s==server_status::success;}
struct operation_id {uint32_t val;};
enum class file_kind{header,source};
struct file_context{
 uint32_t size()const{return 2;}
 file_kind kind(file_id id)const{return id.value()==1?file_kind::header:file_kind::source;}
 bool contains(file_id)const{return true;}
 bool content_available(file_id)const{return true;}
 std::string_view content(file_id)const{return "struct S { int field(); };";}
};
struct project_configuration_manifest{};
struct preprocessor_configuration{};
struct diagnostic_collection{};
struct string_id {uint32_t raw=0; explicit operator bool()const{return raw!=0;} uint32_t value()const{return raw;} };
struct string_table { std::string_view get(string_id id)const { return id.raw==5?"S":id.raw==6?"field":id.raw==7?"T":"unknown"; } string_id find(std::string_view v) const { return v=="T"?string_id{7}:string_id{}; } };
inline server_status compose_project_configuration(const std::filesystem::path&,operation_id, diagnostic_collection&,project_configuration_manifest&,file_context&,preprocessor_configuration&){return server_status::success;}
struct prepared_include_failure_v2{file_id file;};
struct fake_file{std::span<const uint32_t> words;};
struct prepared_include_view_v2{
 const fake_file* file(file_id id)const { static constexpr uint32_t values[]={
  (static_cast<uint32_t>(token_kind::kw_struct)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::identifier)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::l_brace)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::kw_int)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::identifier)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::l_paren)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::r_paren)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::semicolon)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::r_brace)<<24)|(1<<8)|1,
  (static_cast<uint32_t>(token_kind::semicolon)<<24)|(1<<8)|1};
 static constexpr uint32_t root_words[]={
  (static_cast<uint32_t>(token_kind::pp_include)<<24)|(1<<8)|8,
  (static_cast<uint32_t>(token_kind::identifier)<<24)|(9<<8)|7,
  (static_cast<uint32_t>(token_kind::pp_end)<<24)|(8<<8)|1};
 static const fake_file root{root_words};
 static const fake_file x{values};return id.value()==1?&root:&x;}
};
struct prepared_include_closure_v2{
 prepared_include_view_v2 view()const{return {};}
 uint64_t file_count()const{return 2;}
};
inline server_status prepare_header_include_closure_v2(file_context&,std::span<const file_id>,const preprocessor_configuration&,prepared_include_closure_v2&,prepared_include_failure_v2*){return server_status::success;}
enum class preprocessor_v2_failure_kind{none};
struct preprocessor_v2_failure{preprocessor_v2_failure_kind kind=preprocessor_v2_failure_kind::none;};
enum class prepared_number_kind_v2 {none,unsigned_integer};
struct mock_number{prepared_number_kind_v2 kind=prepared_number_kind_v2::none;uint64_t bits=0;};
struct prepared_token{file_id file;token_kind kind;uint32_t source_offset;uint32_t source_length;string_id identifier{};mock_number number{};};
struct semantic_preprocessor_v2{
 uint32_t i=0;
 semantic_preprocessor_v2(file_context&,prepared_include_view_v2, const preprocessor_configuration&, string_table&,preprocessor_v2_failure*){}
 server_status start(file_id){i=0;return server_status::success;}
 const prepared_token* current()const{
 static const prepared_token value[]={
 {file_id{2},token_kind::kw_struct,1,1},
 {file_id{2},token_kind::identifier,2,1,string_id{5}},
 {file_id{2},token_kind::l_brace,3,1},
 {file_id{2},token_kind::kw_int,4,1},
 {file_id{2},token_kind::identifier,5,1,string_id{6}},
 {file_id{2},token_kind::l_paren,6,1},
 {file_id{2},token_kind::r_paren,7,1},
 {file_id{2},token_kind::semicolon,8,1},
 {file_id{2},token_kind::r_brace,9,1},
 {file_id{2},token_kind::semicolon,10,1}};
 return i<10?&value[i]:nullptr;
 }
 server_status advance(){++i;return server_status::success;}
 bool finished()const{return i==10;}
};
}
