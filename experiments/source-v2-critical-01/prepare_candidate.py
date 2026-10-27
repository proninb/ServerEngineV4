"""One candidate: avoid duplicate new-link lookup, reusing frozen production .obj."""
from pathlib import Path
import hashlib,json,sys
repo=Path(__file__).resolve().parents[2]; out=Path(sys.argv[1]).resolve()
root=out/'candidate-source'
assert not root.exists() or not any(root.iterdir()),'do not overwrite a prepared candidate'
root.mkdir(exist_ok=True)
frozen=out/'source'
g=(frozen/'server_engine/project/graph/graph.cpp').read_text()
begin=g.index('server_status graph::add_resolved_link(')
tail_begin=g.index('    if (links.size() >=',begin)
tail_end=g.index('\n}\n',tail_begin)
tail=g[tail_begin:tail_end]
old='    return add_resolved_link(source, target, output);'
rebind=g.index('server_status graph::rebind_resolved_link(')
assert g[rebind:].count(old)==1
g=g[:rebind]+g[rebind:].replace(old,'''    if (!critical_single_lookup) return add_resolved_link(source, target, output);
    // Existing target and invalid endpoint paths above are unchanged.
    // This is the exact original new-slot append tail, after an already failed lookup.
'''+tail)
g='#include <cstdlib>\n#include <string_view>\nnamespace { const bool critical_single_lookup=[] { const auto* p=std::getenv("SOURCE_CRITICAL_LINK_MODE"); return p && std::string_view{p}=="candidate"; }(); }\n'+g
(root/'graph.cpp').write_text(g)
b=(frozen/'benchmarks/publish_benchmark.cpp').read_text()
a='    diagnostics.sort_deterministic();'
assert b.count(a)==1
b=b.replace(a,a+'\n    if (std::getenv("SOURCE_CRITICAL_LINK_DIAGNOSTICS")) cw::server::format_diagnostics(std::cerr, diagnostics);')
a='    std::cout\n        << "mode="'
assert b.count(a)==1
b=b.replace(a,'''    std::string critical_shm;
    if (std::getenv("SOURCE_CRITICAL_SHM_AUDIT"))
        critical_shm=runtime_object_lanes01::sha256(project->shm().bytes().first(
            static_cast<std::size_t>(project->runtime_size())));
    if (!critical_shm.empty()) std::cout << "shm_audit_sha256=" << critical_shm << '\\n';
'''+a)
(root/'main.cpp').write_text(b.replace('namespace {','#include "shm_audit.hpp"\nnamespace {',1))
(root/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(SourceCriticalCandidate LANGUAGES CXX)
set(SNAPSHOT "{out.as_posix()}")
file(GLOB CORE "${{SNAPSHOT}}/build/ServerEngineV4PublishBenchmark.dir/Release/*.obj")
list(FILTER CORE EXCLUDE REGEX "/(publish_benchmark|graph)[.]obj$")
set_source_files_properties(${{CORE}} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
foreach(t SourceCriticalCandidate SourceCriticalGraphTests)
 if(t STREQUAL "SourceCriticalCandidate")
  add_executable(${{t}} main.cpp graph.cpp ${{CORE}})
 else()
  add_executable(${{t}} "${{SNAPSHOT}}/source/tests/compiled_project_tests.cpp" graph.cpp ${{CORE}}
   "${{SNAPSHOT}}/source/server_engine/project/runtime/runtime_layout.cpp"
   "${{SNAPSHOT}}/source/server_engine/project/runtime/fixed_direct_materializer.cpp")
 endif()
 target_include_directories(${{t}} PRIVATE "${{SNAPSHOT}}/source/server_engine" "${{SNAPSHOT}}/source/server_engine/project/graph" "{(repo/'experiments/runtime-object-lanes-01').as_posix()}")
 target_compile_features(${{t}} PRIVATE cxx_std_20)
 target_compile_options(${{t}} PRIVATE /O2 /GL /Zi /Zo /W4 /permissive- /MP4)
 set_property(TARGET ${{t}} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
 target_link_options(${{t}} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO)
 target_link_libraries(${{t}} PRIVATE Psapi bcrypt crypt32 ncrypt winhttp ws2_32)
endforeach()
''')
(out/'candidate-source-manifest.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()},indent=2))
print(root)
