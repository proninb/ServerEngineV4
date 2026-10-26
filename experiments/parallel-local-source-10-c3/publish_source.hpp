#pragma once
#include "../parallel-local-source-08/publish_stats.hpp"
#include "stats.hpp"
#include "../parallel-local-source-02/extract.hpp"
#include "../parallel-local-source-02/planner.hpp"
#include "ordered_binder.hpp"
#include "../../server_engine/project/parser/source_parser_v2.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <optional>
namespace pls08c {
inline thread_local std::string error_detail;
inline cw::server::server_status parse(cw::server::file_context& files,cw::server::lexical_generation& lexical,
 std::size_t,const cw::server::preprocessor_configuration&,cw::server::string_table& strings,cw::server::identity_space& ids,
 cw::server::graph& G,cw::server::source_map& sources,cw::server::parser_failure* failure,
 std::vector<cw::server::parser_warning>* warnings,cw::server::semantic_parse_telemetry* telemetry,cw::server::source_preparation_v2*)noexcept{
 using namespace cw::server;const auto begin=clock::now();
 try {
  for(std::uint32_t i=1;i<=files.size();++i)if(files.kind(file_id{i})==file_kind::assign)throw pls02::failure("Ordered slice excludes Assign inputs");
  diagnostic_collection diagnostics;auto at=clock::now();auto tasks=pls02::plan_tasks(files,std::filesystem::path{files.path(file_id{1})},diagnostics);planner_ms=elapsed(at);
  auto count=[](const char* name,std::size_t fallback){const auto* p=std::getenv(name);return p?(std::max)(std::size_t(1),(std::min)(std::size_t(32),std::size_t(std::strtoul(p,nullptr,10)))):fallback;};
  const auto n=tasks.size();pls10c3::workers=(std::min)(count("PLS10_WORKERS",4),n);pls10c3::window=(std::max)(std::size_t(1),pls10c3::workers*2);pls10c3::peak_ready=0;pls10c3::files_committed=0;
  struct slot {std::optional<pls02::model> value;std::exception_ptr error;bool ready=false;};
  std::vector<slot> slots(pls10c3::window);std::mutex mutex;std::condition_variable cv;
  std::size_t claimed=0,front=0,ready=0;bool stop=false;std::vector<std::jthread> pool;
  const bool reverse=std::getenv("PLS10_REVERSE_TASKS")!=nullptr;
  struct joiner {std::mutex& m;std::condition_variable& cv;bool& stop;std::vector<std::jthread>& pool;
   ~joiner(){{std::lock_guard lock(m);stop=true;}cv.notify_all();pool.clear();}} cleanup{mutex,cv,stop,pool};
  for(std::size_t w=0;w<pls10c3::workers;++w)pool.emplace_back([&]{for(;;){std::size_t rank;
    {std::unique_lock lock(mutex);cv.wait(lock,[&]{return stop||claimed>=n||(reverse?claimed/pls10c3::window<=front/pls10c3::window:claimed<front+pls10c3::window);});if(stop||claimed>=n)return;
     const auto index=claimed++;const auto base=index/pls10c3::window*pls10c3::window;
     rank=reverse?base+(std::min)(pls10c3::window,n-base)-1-index%pls10c3::window:index;}
    std::optional<pls02::model> value;std::exception_ptr error;
    try{const auto t=tasks[rank];value=pls02::extractor(t,files.content(t.file),lexical.source_tokens(t.file)).run();}catch(...){error=std::current_exception();}
    {std::lock_guard lock(mutex);if(stop)return;auto& s=slots[rank%slots.size()];s.value=std::move(value);s.error=error;s.ready=true;++ready;pls10c3::peak_ready=(std::max)(pls10c3::peak_ready,ready);}cv.notify_all();
  }});
  at=clock::now();pls02::ordered_committer commit(G,strings,ids,sources,warnings);
  for(std::size_t rank=0;rank<n;++rank){std::optional<pls02::model> value;std::exception_ptr error;
   {std::unique_lock lock(mutex);cv.wait(lock,[&]{return slots[rank%slots.size()].ready;});auto& s=slots[rank%slots.size()];value=std::move(s.value);error=s.error;s={};--ready;}
   if(error)std::rethrow_exception(error);
   if(!succeeded(sources.begin_root(tasks[rank].file)))throw pls02::failure("SourceMap begin failed");
   commit.apply(*value);if(!succeeded(sources.end_root()))throw pls02::failure("SourceMap end failed");value.reset();
   {std::lock_guard lock(mutex);front=rank+1;++pls10c3::files_committed;}cv.notify_all();
  }
  commit.finish();linking_ms=elapsed(at);alias_ms=writer_ms=extraction_ms=release_ms=0;
  // All tasks were consumed; workers now only exit. Join before token release.
  {std::lock_guard lock(mutex);stop=true;}cv.notify_all();pool.clear();lexical.release_source_tokens();
  if(telemetry){telemetry->source_root_count=n;telemetry->source_ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-begin).count());}
  return server_status::success;
 }catch(const pls02::failure& e){error_detail=e.what();if(failure)*failure={parser_failure_kind::semantic,e.at.file,e.at.source,error_detail};return server_status::project_configuration_invalid;}
 catch(const std::exception& e){error_detail=e.what();if(failure)*failure={parser_failure_kind::semantic,{},{},error_detail};return server_status::project_configuration_invalid;}
}
}
