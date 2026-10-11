#pragma once
#include "project/semantic/identity.hpp"
#include <cstddef>
#include <span>
#include <vector>
namespace cw::server {
enum class graph_type_kind {record,intrinsic_alias};
enum class intrinsic_type;
enum class graph_record_kind {struct_type,class_type,union_type};
struct graph_range {uint32_t begin=0,count=0;};
struct type_entry {graph_range members{0,1};graph_range bases{};graph_type_kind kind=graph_type_kind::record;graph_record_kind record_kind=graph_record_kind::struct_type;bool defined()const{return true;} intrinsic_type alias_intrinsic()const; bool polymorphic()const{return false;}};
enum class type_ref_kind{invalid=0,intrinsic=1,named=2,derived=3};
enum class intrinsic_type{none=0,void_type,bool_type,char_type,signed_char,unsigned_char,wchar_type,char8_type,char16_type,char32_type,signed_short,unsigned_short,signed_int,unsigned_int,signed_long,unsigned_long,signed_long_long,unsigned_long_long,float_type,double_type,long_double_type,nullptr_type};
inline intrinsic_type type_entry::alias_intrinsic()const{return intrinsic_type::unsigned_char;}
enum class derived_type_kind{const_qualified,volatile_qualified,pointer,lvalue_reference,rvalue_reference,bounded_array,unbounded_array};
struct type_ref{bool operator==(const type_ref&)const=default;type_ref_kind k=type_ref_kind::invalid;uint32_t p=0;explicit operator bool()const{return p!=0;}type_ref_kind kind()const{return k;}uint32_t payload()const{return p;}uint32_t value()const{return (static_cast<uint32_t>(k)<<30)|p;}};
struct derived_type_record{uint64_t payload=0;type_ref child{};derived_type_kind kind=derived_type_kind::pointer;};
enum class graph_member_access {public_access, protected_access,private_access};
struct base_record {identity_ref type{};graph_member_access access=graph_member_access::public_access;uint8_t flags=0;uint16_t reserved=0;};
struct member_record{string_id name{6};type_ref type{
#if defined(TEST_CTOR) || defined(TEST_FIELD_INIT)
 type_ref_kind::intrinsic,static_cast<uint32_t>(intrinsic_type::signed_int)
#else
 type_ref_kind::derived,1
#endif
 };graph_member_access access=graph_member_access::public_access;uint8_t reserved[3]{};};
struct member_index {uint32_t raw=0; explicit operator bool()const{return raw!=0;}};
struct type_handle {uint32_t raw=0;uint32_t value()const{return raw;}explicit operator bool()const{return raw!=0;}};
enum class construction_kind {zero,signed_integer,unsigned_integer,real,member_binding,object_binding,unsupported};
struct construction_value {
 std::uint64_t data=0; std::uint32_t operand=0; construction_kind kind=construction_kind::zero;
 std::uint64_t bits()const{return data;}
 static construction_value constant(construction_kind k,std::uint64_t n){return {n,0,k};}
 static construction_value member_binding(std::uint32_t n){return {0,n,construction_kind::member_binding};}
 bool operator==(const construction_value&)const=default;
};
struct constructor_default{ identity_ref owner;string_id path;construction_value value;};
struct constructor_default_table {
 bool independent=false;
 std::vector<constructor_default> emitted;
 server_status add(constructor_default v){emitted.push_back(v);return server_status::success;}
 std::span<const constructor_default> entries()const{
 if(independent)return emitted;
#ifdef TEST_GRAPH_ONLY_NESTED
 static constructor_default entry[1]{{identity_ref{1},string_id{8},construction_value::constant(construction_kind::unsigned_integer,2)}};
 return entry;
#else
 return {};
#endif
}};
#ifdef TEST_V321_RETURN
inline std::vector<derived_type_kind> v321_derived_log;
#endif
struct graph {
 constructor_default_table constructor_defaults;
 bool emitted=false;
 std::vector<member_record> producer_members;
 std::vector<construction_value> producer_values;
 server_status declare_record(identity_ref,graph_record_kind,type_handle& output){constructor_defaults.independent=true;output={1};return server_status::success;}
 server_status define_intrinsic_alias(identity_ref,intrinsic_type,type_handle& output){output={1};return server_status::success;}
 server_status begin_direct_record(type_handle,std::size_t count){
    emitted=false;producer_members.clear();producer_values.clear();
    producer_members.reserve(count);producer_values.reserve(count);
    return server_status::success;
 }
 server_status append_direct_member(type_handle,member_record m,construction_value v){
    producer_members.push_back(m);producer_values.push_back(v);
    return server_status::success;
 }
 member_record* direct_record_member(type_handle,std::size_t i){
    return i<producer_members.size()? &producer_members[i] : nullptr;
 }
 server_status set_direct_construction(type_handle,std::size_t i, construction_value v){
    if(i>=producer_values.size())return server_status::fail;
    producer_values[i]=v;return server_status::success;
 }
 server_status commit_direct_record(type_handle,graph_record_kind,
    std::span<const base_record>,bool){emitted=true;return server_status::success;}
 server_status commit_prevalidated_record(type_handle t,graph_record_kind k,
    std::span<const base_record> b,bool v){return commit_direct_record(t,k,b,v);}
 server_status finish_prevalidated_member_index(){return server_status::success;}
 std::span<const member_record> members(type_handle)const{return producer_members;}
 void abort_direct_record(type_handle){producer_members.clear();producer_values.clear();emitted=false;}
 server_status define_resolved_record(type_handle,graph_record_kind,std::span<const member_record> ms,
    std::span<const construction_value> vs,std::span<const base_record>,bool){
    producer_members.assign(ms.begin(),ms.end());
    producer_values.assign(vs.begin(),vs.end());emitted=true;
    return server_status::success;}
 bool named(type_ref, type_handle& o)const{o={1};return true;}
 member_index find_member(type_handle,string_id)const{return {1};}
 const member_record* member(type_handle,member_index)const{static member_record m;return &m;}
 type_ref intrinsic(intrinsic_type ty)const{return {type_ref_kind::intrinsic,static_cast<uint32_t>(ty)};}
 type_ref named(identity_ref ty)const{return {type_ref_kind::named,ty.slot()};}
 server_status derive_resolved(type_ref,derived_type_kind kind,uint64_t,type_ref& output){
#ifdef TEST_V321_RETURN
 v321_derived_log.push_back(kind);
#endif
 output={type_ref_kind::derived,1};return server_status::success;}
 std::span<const construction_value> member_construction_entries()const{
 if(emitted)return producer_values;
#ifdef TEST_FIELD_INIT
 #ifdef TEST_FIELD_INIT_MISMATCH
 static construction_value a[1]{construction_value::constant(construction_kind::unsigned_integer,8)};
 #else
 static construction_value a[1]{construction_value::constant(construction_kind::unsigned_integer,7)};
 #endif
#elif defined(TEST_GRAPH_ONLY_DIRECT)
 static construction_value a[1]{construction_value::constant(construction_kind::unsigned_integer,9)};
#elif defined(TEST_CTOR_MISMATCH)
 static construction_value a[1]{construction_value::constant(construction_kind::unsigned_integer,8)};
#elif defined(TEST_CTOR)
 static construction_value a[1]{construction_value::constant(construction_kind::unsigned_integer,5)};
#else
 static construction_value a[1];
#endif
 return a;}
 type_handle find_type(identity_ref id)const{return id.raw==7?type_handle{1}:type_handle{};}
 const type_entry* find(type_handle h)const{static type_entry x;
#ifdef TEST_ALIAS
 x.kind=graph_type_kind::intrinsic_alias;
#else
 x.kind=graph_type_kind::record;
#endif
 return h.value()==1?&x:nullptr;}
 std::size_t type_count()const{return 1;}std::size_t member_count()const{return 1;}
 std::span<const derived_type_record> derived_type_entries()const{
#ifdef TEST_DERIVED_V2_EXTRA
 if(!emitted){static derived_type_record extra[]{{0,{type_ref_kind::intrinsic,
    static_cast<std::uint32_t>(intrinsic_type::signed_int)},derived_type_kind::const_qualified}};
    return extra;}
#endif
 return {};
 }
 std::size_t derived_type_count()const{return 0;}
 std::size_t object_count()const{return 0;}std::size_t link_count()const{return 0;}
 std::span<const type_entry> type_entries()const{static type_entry e[1]; e[0].members.count=1;return e;}
 std::span<const identity_ref> type_identity_entries()const{static identity_ref e[1]{{1}};return e;}
 std::span<const base_record> base_entries()const{return {}; }
 std::span<const member_record> member_entries()const{
 if(emitted){
#ifdef TEST_V319_MEMBER_MISMATCH
 static member_record different[1]; different[0]=producer_members[0];different[0].name={7};return different;
#else
 return producer_members;
#endif
 }
 static member_record e[1];return e;}
 bool derived(type_ref type,derived_type_record& out)const{
    if(type.k!=type_ref_kind::derived)return false;
    out.kind=derived_type_kind::pointer;
#if defined(TEST_BAD_SHAPE) || defined(TEST_ARRAY7) || defined(TEST_ARRAY8)
    out.kind=derived_type_kind::bounded_array;
    out.payload=7;
#ifdef TEST_ARRAY8
    out.payload=8;
#endif
#endif
    out.child={type_ref_kind::intrinsic,static_cast<uint32_t>(intrinsic_type::void_type)};
    return true;
 }
};
}
