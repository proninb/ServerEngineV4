from pathlib import Path
import subprocess,sys
repo,out=map(lambda s:Path(s).resolve(),sys.argv[1:]);own=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c7/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a))
 return s.replace(a,b)
inc='#include "'+(own/'coarse.hpp').as_posix()+'"\n'
s=(repo/'experiments/parallel-local-source-10-c3/publish_source.hpp').read_text()
s=inc+s
import re
s=re.sub(r'#include "([^\"]+)"',lambda m:'#include "'+((repo/'experiments/parallel-local-source-10-c3'/m[1]).resolve().as_posix())+'"' if m[1].startswith('../') or m[1]=='stats.hpp' else m[0],s)
s=once(s,'using namespace cw::server;const auto begin=clock::now();','using namespace cw::server;pls10c9::source_scope source_observer;const auto begin=clock::now();')
s=once(s,'try{const auto t=tasks[rank];','try{pls10c9::scope extraction_observer(pls10c9::parse);const auto t=tasks[rank];')
s=once(s,'.source_tokens(t.file)).run();}', '.source_tokens(t.file)).run();pls10c9::model_add(value->retained_bytes(),value->events.size(),value->token_reads);}')
s=once(s,'{std::unique_lock lock(mutex);cv.wait(lock,[&]{return slots','{pls10c9::scope waiting_observer(pls10c9::wait);std::unique_lock lock(mutex);cv.wait(lock,[&]{return slots')
s=once(s,'commit.apply(*value);','{pls10c9::scope apply_observer(pls10c9::apply);commit.apply(*value);}')
s=once(s,'value.reset();','{const auto bytes=pls10c9::enabled()?value->retained_bytes():0;pls10c9::scope release_observer(pls10c9::cleanup);value.reset();if(pls10c9::enabled())pls10c9::live-=bytes;}')
(out/'publish_source.hpp').write_text(s)
p=out/'publish_parser.cpp';s=p.read_text().replace((repo/'experiments/parallel-local-source-10-c3/publish_source.hpp').as_posix(),(out/'publish_source.hpp').as_posix());p.write_text(s)
s=inc+(repo/'server_engine/project/parser/source_parser_v2.cpp').read_text()
s=re.sub(r'#include "([^\"]+)"',lambda m:'#include "'+((repo/'server_engine/project/parser'/m[1]).resolve().as_posix())+'"' if not Path(m[1]).is_absolute() else m[0],s)
s=once(s,'    using clock = std::chrono::steady_clock;\n    const auto started = telemetry ? clock::now() : clock::time_point{};','    pls10c9::source_scope source_observer;\n    using clock = std::chrono::steady_clock;\n    const auto started = telemetry ? clock::now() : clock::time_point{};')
s=once(s,'const auto status = parser.parse(root);','const auto status = [&]{pls10c9::scope parse_observer(pls10c9::parse);return parser.parse(root);}();')
(out/'source_parser_v2.cpp').write_text(s)
p=out/'publish_main.cpp';s=inc+p.read_text();s=once(s,'pls10c3::print(std::cout);','pls10c3::print(std::cout);pls10c9::print(std::cout);');p.write_text(s)
# Count-only copies are linked exclusively into the allocation audit executable.
def audit_copy(relative,output,functions,extra=None):
 src=repo/relative;s=src.read_text()
 s=re.sub(r'#include "([^\"]+)"',lambda m:'#include "'+(src.parent/m[1]).resolve().as_posix()+'"',s)
 s='#include "'+(own/'audit.hpp').as_posix()+'"\n'+s
 for function,counter in functions.items():
  pattern=re.compile(re.escape(function)+r'\([^;]*?\)\s*(?:const\s*)?noexcept\s*\{')
  matches=list(pattern.finditer(s));assert len(matches)==1,(function,len(matches))
  m=matches[0];s=s[:m.end()]+'pls10c9audit::count(pls10c9audit::'+counter+');'+s[m.end():]
 if extra:s=extra(s)
 (out/output).write_text(s)
audit_copy('server_engine/project/graph/graph.cpp','audit_graph.cpp',{
 'graph::add_resolved_object':'object','graph::intern_resolved_endpoint_path':'path',
 'graph::add_resolved_initialization':'initialization','graph::apply_resolved_initialization':'slot_apply',
 'graph::add_resolved_initializations':'batch','graph::rebind_resolved_link':'rebind',
 'graph::add_resolved_link':'add_link','graph::find_initialization_position':'init_lookup','graph::find_link_target':'link_lookup'},
 lambda s:once(s,'pls10c9audit::count(pls10c9audit::batch);','pls10c9audit::count(pls10c9audit::batch);if(pls10c9audit::active)pls10c9audit::batch_records+=records.size();'))
audit_copy('server_engine/project/semantic/identity.cpp','audit_identity.cpp',{'identity_space::find':'identity_find','identity_space::resolve':'identity_resolve'})
