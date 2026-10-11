#pragma once
namespace cw::server {
enum class identity_kind{root,namespace_scope,type,object};
struct identity_ref {uint32_t raw=0; static identity_ref from_raw(uint32_t v){return {v};}explicit operator bool()const{return raw!=0;}uint32_t slot()const{return raw;}uint32_t value()const{return raw;}};
struct identity_record { identity_ref parent; string_id name; };
struct identity_space{
 explicit identity_space(string_table&) {}
 server_status resolve(identity_ref,string_id name,identity_kind,identity_ref& id){id={name.value()==5?1u:7u};return server_status::success;}
 bool record(identity_ref,identity_record& v)const {v.name={5};v.parent={0};return true;}
 identity_ref root()const{return {1};}
 identity_ref find(identity_ref,string_id name,identity_kind kind)const{return (name.value()==7 && kind==identity_kind::type)?identity_ref{7}:identity_ref{0};}
};
}
