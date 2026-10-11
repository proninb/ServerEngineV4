#pragma once
#include "project/semantic/identity.hpp"
#include <cstddef>
#include <span>
namespace cw::server {
enum class graph_type_kind {record,intrinsic_alias};
enum class graph_record_kind {struct_type,class_type,union_type};
struct graph_range {uint32_t begin=0,count=0;};
struct type_entry { graph_range members{0,1}; graph_type_kind kind=graph_type_kind::record;graph_record_kind record_kind=graph_record_kind::struct_type;bool defined()const{return true;} };
struct member_record {string_id name{6};};
struct graph {
 std::size_t type_count() const {return 1;}
 std::size_t member_count() const {return 1;}
 std::size_t object_count() const {return 0;}
 std::size_t link_count() const {return 0;}
 std::span<const type_entry> type_entries()const{static type_entry entries[1];return entries;}
 std::span<const identity_ref> type_identity_entries()const{static identity_ref entries[1];return entries;}
 std::span<const member_record> member_entries()const{static member_record entries[1];return entries;}
};
}
