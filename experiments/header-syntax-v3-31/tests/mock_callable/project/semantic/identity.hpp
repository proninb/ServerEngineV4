#pragma once
namespace cw::server {
enum class identity_kind{root,namespace_scope,type,object};
struct identity_ref {uint32_t raw=0;explicit operator bool()const{return raw!=0;}uint32_t slot()const{return raw;}uint32_t value()const{return raw;}};
struct identity_record { identity_ref parent; string_id name; };
struct identity_space{
 explicit identity_space(string_table&) {}
 bool record(identity_ref,identity_record& v)const {v.name={5};v.parent={0};return true;}
 identity_ref root()const{return {1};}
 identity_ref find(identity_ref,string_id,identity_kind)const{return {0};}
};
}
