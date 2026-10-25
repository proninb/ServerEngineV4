#pragma once
// Included only by the generated experimental parser.cpp, never engine sources.
#include "publish_stats.hpp"
#include "../parallel-local-source-02/extract.hpp"
#include "../parallel-local-source-02/planner.hpp"
#include "aliases.hpp"
#include "../parallel-local-source-07/ownership.hpp"
#include "writer.hpp"
#include "../../server_engine/project/parser/source_parser_v2.hpp"
#include <deque>

namespace pls08c {
inline thread_local std::string error_detail;
inline thread_local std::deque<std::string> warning_details;

// Narrower than A/B: no Source path through a union. This structural guard
// avoids needing a diagnostic SHM allocation/range scan on the timed path.
inline void disjoint_domain(const pls07b1::cell_view& cell,const cw::server::graph& G){
 using namespace cw::server;auto type=cell.root_type;
 for(const auto& step:cell.steps){type=pls02::unqualified(G,type);derived_type_record d;
  if(step.kind==endpoint_path_step_kind::array_index){pls08::require(G.derived(type,d)&&d.kind==derived_type_kind::bounded_array,"PLS08-C invalid array path");type=d.child;}
  else {type_handle h;pls08::require(G.named(type,h),"PLS08-C invalid record path");const auto* entry=G.find(h);
   pls08::require(entry&&entry->record_kind!=graph_record_kind::union_type,"PLS08-C excludes Source paths through union storage");
   if(step.kind==endpoint_path_step_kind::member){const auto members=G.members(h);pls08::require(step.value<members.size(),"PLS08-C member absent");type=members[step.value].type;}
   else {const auto bases=G.bases(h);pls08::require(step.kind==endpoint_path_step_kind::base&&step.value<bases.size(),"PLS08-C base absent");type=G.named(G.find_type(bases[step.value].type));}}
 }
}
inline cw::server::server_status parse(cw::server::file_context& files,cw::server::lexical_generation& lexical,
 std::size_t,const cw::server::preprocessor_configuration&,cw::server::string_table& strings,cw::server::identity_space& ids,
 cw::server::graph& G,cw::server::source_map& sources,cw::server::parser_failure* failure,
 std::vector<cw::server::parser_warning>* warnings,cw::server::semantic_parse_telemetry* telemetry,cw::server::source_preparation_v2*)noexcept{
 using namespace cw::server;const auto started=clock::now();
 struct timer {semantic_parse_telemetry* t;clock::time_point at;~timer(){if(t)t->source_ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-at).count());}} elapsed_scope{telemetry,started};
 try {
  warning_details.clear();
  for(std::uint32_t i=1;i<=files.size();++i)pls08::require(files.kind(file_id{i})!=file_kind::assign,"PLS08-C Assign inputs unsupported");
  const auto header_objects=G.object_count(),header_links=G.link_count(),header_values=G.initialization_count();
  auto at=clock::now();diagnostic_collection diagnostics;
  auto tasks=pls02::plan_tasks(files,std::filesystem::path{files.path(file_id{1})},diagnostics);planner_ms=elapsed(at);
  at=clock::now();std::vector<pls02::model> locals(tasks.size());
  for(std::size_t i=0;i<tasks.size();++i)locals[i]=pls02::extractor(tasks[i],files.content(tasks[i].file),lexical.source_tokens(tasks[i].file)).run();
  lexical.release_source_tokens();extraction_ms=elapsed(at);
  at=clock::now();auto bound=pls02::semantic_linker(G,strings,ids).run(locals);locals.clear();locals.shrink_to_fit();linking_ms=elapsed(at);
  at=clock::now();auto final=pls02::alias_linker(G,bound).run();alias_ms=elapsed(at);
  {
   pls07::owned_result owner{std::move(final),std::move(bound)};bound={};
   const pls08::catalog catalog{G,files,header_objects};std::vector<pls07b1::source_task> ordered;
   for(const auto& task:tasks)ordered.push_back({task.file,task.source_order});
   const pls07b1::owned_reader reader{owner,catalog,pls07b1::domain_id{1,1},std::span<const pls07b1::source_task>{ordered}};
   at=clock::now();for(std::size_t i=0;i<reader.cell_count();++i)disjoint_domain(reader.cell_at(i),G);
   auto output=pls08::write_supported_projection(reader,catalog,G,sources,header_links,header_values,false);
   if(warnings)for(std::size_t i=0;i<reader.warning_count();++i){const auto w=reader.warning_at(i);
    warning_details.push_back("Final Source cell overwritten; previous file="+std::to_string(w.previous.file.value())+" offset="+std::to_string(w.previous.range.offset)+" length="+std::to_string(w.previous.range.length)+"; current file="+std::to_string(w.current.file.value())+" offset="+std::to_string(w.current.range.offset)+" length="+std::to_string(w.current.range.length));
    warnings->push_back({parser_warning_kind::duplicate_initialization,w.current.file,w.current.range,warning_details.back()});}
   writer_ms=elapsed(at);at=clock::now();
  }
  release_ms=elapsed(at);if(telemetry)telemetry->source_root_count=tasks.size();return server_status::success;
 }catch(const pls02::failure& e){error_detail=e.what();if(failure)*failure={parser_failure_kind::semantic,e.at.file,e.at.source,error_detail};return server_status::project_configuration_invalid;}
 catch(const std::exception& e){error_detail=e.what();if(failure)*failure={parser_failure_kind::semantic,{},{},error_detail};return server_status::project_configuration_invalid;}
}
}
