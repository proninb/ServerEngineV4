#pragma once
#include "../parallel-local-source-02/model.hpp"
#include "runtime_probe.hpp"
#include "../../server_engine/project/project.hpp"
#include <unordered_map>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
namespace pls08c {
// Cold oracle across every object byte, including untouched Header defaults,
// arrays, padding and reference slots. Both arms use the same fixed SHM base.
inline void object_storage_audit(const cw::server::project& project,std::ostream& out){
 const auto* flag=std::getenv("PLS08_OBJECT_AUDIT");if(!flag||flag[0]!='1')return;
 using namespace cw::server;const auto& view=project.compiled();shm_layout layout;
 const server_abi_configuration abi{abi_target::windows_x64,8};
 pls08::require(prepare_shm_layout(view,abi,layout)==shm_layout_result::success,"Object storage audit layout failed");
 struct hash_guard {BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<unsigned char> scratch;~hash_guard(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} state;
 pls08::require(BCryptOpenAlgorithmProvider(&state.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Object storage audit SHA provider failed");
 DWORD bytes=0,size=0;pls08::require(BCryptGetProperty(state.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof size,&bytes,0)>=0,"Object storage audit SHA size failed");
 state.scratch.resize(size);pls08::require(BCryptCreateHash(state.algorithm,&state.hash,state.scratch.data(),size,nullptr,0,0)>=0,"Object storage audit SHA create failed");
 std::uint64_t total=0;const auto memory=project.shm().bytes();
 for(std::size_t i=0;i<view.object_count();++i){const auto h=view.object_at(i);object_entry object;shm_offset at=0;shm_value_layout width;
  pls08::require(view.object(h,object)&&layout.object_offset(h,at)&&pls08::value_layout(view,layout,object.type,width)&&at<=memory.size()&&width.size<=memory.size()-at,"Object storage audit bounds invalid");
  const std::uint64_t metadata[]={view.identity(h).value(),object.type.value(),at,width.size};
  pls08::require(BCryptHashData(state.hash,reinterpret_cast<PUCHAR>(const_cast<std::uint64_t*>(metadata)),sizeof metadata,0)>=0,"Object storage audit metadata hash failed");
  for(std::size_t done=0;done<width.size;){const auto count=static_cast<ULONG>((std::min)(width.size-done,static_cast<shm_offset>(UINT32_MAX)));
   pls08::require(BCryptHashData(state.hash,reinterpret_cast<PUCHAR>(const_cast<std::byte*>(memory.data()+at+done)),count,0)>=0,"Object storage audit data hash failed");done+=count;}
  total+=width.size;
 }
 std::array<unsigned char,32> digest{};pls08::require(BCryptFinishHash(state.hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0,"Object storage audit SHA finish failed");
 // Destroy while the caller-provided hash object buffer is still alive.
 pls08::require(BCryptDestroyHash(state.hash)>=0,"Object storage audit SHA destroy failed");state.hash=nullptr;
 const char* hex="0123456789abcdef";std::string text;for(auto b:digest){text+=hex[b>>4];text+=hex[b&15];}
 out<<"pls08_object_storage_sha256="<<text<<",pls08_object_storage_bytes="<<total<<",pls08_object_storage_count="<<view.object_count()<<'\n';
}
struct decoded_endpoint {std::vector<cw::server::endpoint_path_step> steps;cw::server::type_ref type;};
inline decoded_endpoint decode(const cw::server::compiled_project_view& view,cw::server::object_endpoint ep){
 using namespace cw::server;decoded_endpoint out;object_entry object;
 pls08::require(view.object(view.find_object(ep.object),object),"Resident oracle WHO absent");
 if(ep.member.is_member()){type_handle h;type_entry record;member_record member;const auto type=pls08::plain(view,object.type);
  pls08::require(view.named(type,h)&&view.type(h,record)&&ep.member.direct_member().value()<record.members.count&&view.member_at(record.members.begin+ep.member.direct_member().value(),member),"Resident oracle direct member absent");
  out.steps.push_back({ep.member.direct_member().value(),endpoint_path_step_kind::member,{}});out.type=member.type;
 }else{endpoint_path_record path;pls08::require(view.endpoint_path(ep.member.path(),path),"Resident oracle path absent");out.type=path.value_type;
  for(std::size_t i=0;i<path.steps.count;++i){endpoint_path_step step;pls08::require(view.endpoint_path_step_at(path.steps.begin+i,step),"Resident oracle path step absent");out.steps.push_back(step);}}
 return out;
}
inline std::string endpoint_key(const cw::server::compiled_project_view& view,cw::server::object_endpoint ep){
 const auto path=decode(view,ep);std::string key=std::to_string(ep.object.value());
 for(const auto& step:path.steps)key+=':'+std::to_string(static_cast<unsigned>(step.kind))+','+std::to_string(step.value);return key;
}
inline void expected_image(const cw::server::project& project,std::ostream& out){
 const auto* image=std::getenv("PLS08_EXPECTED_IMAGE");if(!image||!image[0])return;
 using namespace cw::server;read_only_file_mapping mapping;
 pls08::require(mapping.open(std::filesystem::path{image})==read_only_file_mapping_result::success,"Cannot open independent expected image");
 compiled_project_view expected;const auto& actual=project.compiled();
 pls08::require(expected.bind(mapping.bytes())==compiled_project_image_result::success&&expected.verify_contents()==compiled_project_image_result::success,"Expected image invalid");
 pls08::require(actual.object_count()==expected.object_count()&&actual.link_count()==expected.link_count()&&actual.initialization_count()==expected.initialization_count(),"Resident oracle Graph counts differ");
 shm_layout layout;const server_abi_configuration abi{abi_target::windows_x64,8};pls08::require(prepare_shm_layout(actual,abi,layout)==shm_layout_result::success,"Resident oracle layout failed");
 for(std::size_t i=0;i<expected.object_count();++i){const auto object=expected.object_at(i);object_entry a,b;const auto who=expected.identity(object);
  pls08::require(expected.object(object,a)&&actual.object(actual.find_object(who),b)&&a.type==b.type,"Resident oracle object type differs");}
 std::unordered_map<std::string,construction_value> values;
 for(std::size_t i=0;i<actual.initialization_count();++i){object_initialization_record value;pls08::require(actual.initialization_at(i,value),"Resident query init absent");pls08::require(values.emplace(endpoint_key(actual,value.target),value.value).second,"Resident duplicate init target");}
 const auto memory=project.shm().bytes();
 for(std::size_t i=0;i<expected.initialization_count();++i){object_initialization_record value;pls08::require(expected.initialization_at(i,value),"Expected init absent");
  const auto found=values.find(endpoint_key(expected,value.target));pls08::require(found!=values.end()&&found->second==value.value,"Resident query final value differs");
  const auto path=decode(expected,value.target);const auto at=pls08::offset(actual,layout,value.target.object,path.steps);const auto bytes=pls08::expected_value(actual,layout,path.type,value.value);
  pls08::require(at<=memory.size()&&bytes.size()<=memory.size()-at&&std::equal(bytes.begin(),bytes.end(),memory.begin()+at),"Resident SHM scalar differs");}
 std::unordered_map<std::string,std::string> bindings;
 for(std::size_t i=0;i<actual.link_count();++i){link_record link;pls08::require(actual.link(actual.link_at(i),link),"Resident query link absent");pls08::require(bindings.emplace(endpoint_key(actual,link.target),endpoint_key(actual,link.source)).second,"Resident duplicate binding target");}
 for(std::size_t i=0;i<expected.link_count();++i){link_record link;pls08::require(expected.link(expected.link_at(i),link),"Expected link absent");
  const auto found=bindings.find(endpoint_key(expected,link.target));pls08::require(found!=bindings.end()&&found->second==endpoint_key(expected,link.source),"Resident query binding differs");
  const auto target=decode(expected,link.target),source=decode(expected,link.source);
  const auto ta=pls08::offset(actual,layout,link.target.object,target.steps),sa=pls08::offset(actual,layout,link.source.object,source.steps);std::uintptr_t pointer=0;
  pls08::require(ta<=memory.size()&&sizeof pointer<=memory.size()-ta&&sa<memory.size(),"Resident pointer range invalid");std::memcpy(&pointer,memory.data()+ta,sizeof pointer);
  pls08::require(pointer==reinterpret_cast<std::uintptr_t>(memory.data()+sa),"Resident native pointer differs");}
 out<<"pls08_expected_values="<<expected.initialization_count()<<",pls08_expected_links="<<expected.link_count()<<'\n';
}
inline void cold_check(const cw::server::project& project,std::ostream& out){
 object_storage_audit(project,out);
 expected_image(project,out);
 const auto* flag=std::getenv("PLS08_COLD_CHECK");if(!flag||flag[0]!='1')return;
 using namespace cw::server;const auto& view=project.compiled();
 pls08::require(view.verify_contents()==compiled_project_image_result::success,"Resident compiled audit failed");
 shm_layout layout;const server_abi_configuration abi{abi_target::windows_x64,8};
 pls08::require(prepare_shm_layout(view,abi,layout)==shm_layout_result::success,"Resident audit layout failed");
 pls08::require(view.object_count()<100,"Use PLS08-A/B full corpus oracle for large resident projects");
 pls08::dumper dump{view,layout,project.shm().bytes()};
 for(std::size_t i=0;i<view.object_count();++i){const auto object=view.object_at(i);object_entry entry;shm_offset at=0;
  pls08::require(view.object(object,entry)&&layout.object_offset(object,at),"Resident audit object absent");
  std::string name;auto who=view.identity(object);std::vector<std::string> parts;
  while(who&&view.identity_parent(who)){parts.emplace_back(view.string(view.identity_name(who)));who=view.identity_parent(who);}
  for(auto it=parts.rbegin();it!=parts.rend();++it){if(!name.empty())name+="::";name+=*it;}
  dump.walk(entry.type,at,name);
 }
 out<<"pls08_resident={\"fields\":{"<<dump.fields.str()<<"},\"addresses\":{"<<dump.addresses.str()<<"},\"pointers\":{"<<dump.pointers.str()<<"}}\n";
}
}
