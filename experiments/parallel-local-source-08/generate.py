"""Isolated PUBLISH feasibility path over PLS07; no engine source patches."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-07/generate.py'),str(repo),str(out)],check=True)
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
s=(out/'main.cpp').read_text();s=once(s,'#include <iostream>','#include <iostream>\n#include "'+(repo/'experiments/parallel-local-source-08/runtime_probe.hpp').as_posix()+'"')
s=once(s,'try {\n        file_context files;', '''try {
        check(pls07::enabled(),"PLS08 requires PLS07_OWN=1");
        file_context files;''')
s=once(s,'string_table strings;identity_space ids{strings};graph G;', 'source_map publication_sources;check(succeeded(publication_sources.reset(files.size())),"Header SourceMap reset failed");\n        string_table strings;identity_space ids{strings};graph G;')
s=once(s,'for(auto root:headers){check(succeeded(pp.start(root))', 'for(auto root:headers){check(succeeded(publication_sources.begin_root(root)),"Header SourceMap begin failed");check(succeeded(pp.start(root))')
s=once(s,'header_parser_v2 parser{pp,ids,G,&f};','header_parser_v2 parser{pp,ids,G,&f};parser.set_provenance(&publication_sources);')
s=once(s,'throw failure(std::string(f.detail),{f.file,f.source});}', 'throw failure(std::string(f.detail),{f.file,f.source});check(succeeded(publication_sources.end_root()),"Header SourceMap end failed");}')
s=once(s,'pls04::guard memory_guard;', 'const auto header_object_count=G.object_count(),header_link_count=G.link_count(),header_initialization_count=G.initialization_count();\n        pls04::guard memory_guard;')
s=once(s,'const auto consumer_start=clock_type::now();', '''
        pls04::pause();std::vector<pls07b1::source_task> writer_tasks;for(const auto& t:tasks)writer_tasks.push_back({t.file,t.source_order});
        const pls08::catalog writer_catalog{G,files,header_object_count};
        const pls07b1::owned_reader reader{*owner,writer_catalog,pls07b1::domain_id{1,1},std::span<const pls07b1::source_task>{writer_tasks}};
        const auto* reverse_flag=std::getenv("PLS08_REVERSE");const bool physical_reverse=reverse_flag&&reverse_flag[0]=='1';
        auto artifact=output;artifact.replace_extension(".compiled.bin");
        const auto graph_probe=pls08::probe(reader,writer_catalog,G,publication_sources,ids,strings,header_link_count,header_initialization_count,physical_reverse,artifact);
        pls04::phase_begin(pls04::consumer);const auto consumer_start=clock_type::now();''')
# Preserve the historical semantic lifecycle field, explicitly excluding writer:
# a full PUBLISH timer is not claimed in feasibility mode.
s=once(s,'report<<"],\\n\\"consumer_ms\\":"', 'report<<"],\\n\\"graph_writer\\":";pls08::json(report,graph_probe);report<<",\\n\\"consumer_ms\\":"')
s=once(s,'lifecycle_ms','semantic_projection_lifecycle_ms')
(out/'main.cpp').write_text(s)

# PLS08-C uses a generated copy of the native orchestration, leaving production
# files untouched. Both A/B choices live in the same optimized executable.
p=(repo/'server_engine/project/parser/parser.cpp').read_text()
p=once(p,'#include "parser.hpp"','#include "parser.hpp"\n#include "'+(repo/'experiments/parallel-local-source-08/publish_source.hpp').as_posix()+'"')
p=once(p,'if (publish_frontend_v2)\n        (void)source_preparation.start', 'if (publish_frontend_v2 && !pls08c::candidate())\n        (void)source_preparation.start')
p=once(p,'const auto result = parse_source_roots_v2(', 'const auto result = (pls08c::candidate() ? pls08c::parse : parse_source_roots_v2)(')
(out/'publish_parser.cpp').write_text(p)
b=(repo/'benchmarks/publish_benchmark.cpp').read_text()
b='#include "'+(repo/'experiments/parallel-local-source-08/publish_stats.hpp').as_posix()+'"\n'+b
b='#include "'+(repo/'experiments/parallel-local-source-08/resident_probe.hpp').as_posix()+'"\n'+b
b=once(b,'"CW.ServerEngineV4.PublishBenchmark";', '"CW.ServerEngineV4.PLS08C." + std::to_string(GetCurrentProcessId());')
b=once(b,'    std::size_t expected_types = 0;', '''    if (mode != benchmark_mode::publish && mode != benchmark_mode::publish_profile) {
        std::cerr << "PLS08-C supports PUBLISH only\\n"; return 2;
    }
    std::size_t expected_types = 0;''')
b=once(b,'if (compiled.type_count() !=\n        expected_types)', 'if (expected_types != 0 && compiled.type_count() !=\n        expected_types)')
b=once(b,"    std::cout << '\\n';\n\n    return 0;",'''    pls08c::print(std::cout);std::cout << '\\n';
    pls08c::cold_check(*project,std::cout);
    const auto teardown_started=std::chrono::steady_clock::now();project.reset();
    const auto operation_end=std::chrono::steady_clock::now();
    std::cout<<"pls08_full_with_teardown_ms="<<std::chrono::duration<double,std::milli>((finished-started)+(operation_end-teardown_started)).count()
             <<",resident_teardown_ms="<<std::chrono::duration<double,std::milli>(operation_end-teardown_started).count()<<'\\n';
    return 0;''')
(out/'publish_main.cpp').write_text(b)
