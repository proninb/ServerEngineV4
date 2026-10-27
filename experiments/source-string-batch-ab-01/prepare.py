"""Freeze current V2 + Fast Reader; native toggle and separate counter/fault target."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys
repo=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve()
subprocess.run([sys.executable,str(repo/'experiments/publish-fast-acquire-02/prepare.py'),str(out)],check=True)
root=out/'source'
# Keep the production reader exactly; only batching is controlled here.
shutil.copy2(repo/'server_engine/project/file/file_context.cpp',root/'server_engine/project/file/file_context.cpp')
shutil.copy2(Path(__file__).parent/'control.hpp',root/'server_engine/project/source_batch01.hpp')

def edit(relative,transform):
    path=root/relative
    source=path.read_text()
    changed=transform(source)
    assert source!=changed,relative
    path.write_text(changed)

def replace(s,a,b):
    assert s.count(a)==1,(a[:100],s.count(a))
    return s.replace(a,b)

def parser(s):
    begin=s.index('    [[nodiscard]] server_status assign_string(')
    end=s.index('    [[nodiscard]] server_status assignment(',begin)
    body=s[begin:end]
    body=replace(body,'        derived_type_record array;',
        '''#ifdef CW_SOURCE_STRING_BATCH_AUDIT_01
        source_batch01::scope string_scope;
#endif
        const bool batch_commit = source_batch01::batch;
        derived_type_record array;''')
    body=replace(body,'''#ifdef CW_SOURCE_STRING_BATCH_V2
        string_initializations.clear();''', '''        if (batch_commit) {
#ifdef CW_SOURCE_STRING_BATCH_AUDIT_01
        if (source_batch01::fault == "reserve") {
            source_batch01::fault_fired=true; return server_status::io_error;
        }
#endif
        string_initializations.clear();''')
    body=replace(body,'''        catch (...) { return server_status::io_error; }
#endif
        for (std::uint64_t i = 0;''', '''        catch (...) { return server_status::io_error; }
        }
#ifdef CW_SOURCE_STRING_BATCH_AUDIT_01
        source_batch01::elements += array.payload;
#endif
        for (std::uint64_t i = 0;''')
    body=replace(body,'''#ifdef CW_SOURCE_STRING_BATCH_V2
            string_initializations.push_back({element, value});
#else''', '''            if (batch_commit) {
            string_initializations.push_back({element, value});
            } else {''')
    body=replace(body,'''            if (!succeeded(status)) return status;
#endif
        }
#ifdef CW_SOURCE_STRING_BATCH_V2
        status = G.add_resolved_initializations''', '''            if (!succeeded(status)) return status;
            }
        }
        if (batch_commit) {
        status = G.add_resolved_initializations''')
    body=replace(body,'''        }
#endif
        status = sources.add(target.unit.file''', '''        }
        }
        status = sources.add(target.unit.file''')
    return '#include "../source_batch01.hpp"\n'+s[:begin]+body+s[end:]
edit('server_engine/project/parser/source_parser_v2.cpp',parser)

def graph(s):
    for function,counter in [('add_resolved_initialization','++source_batch01::scalar_calls; source_batch01::string_scalar_calls += source_batch01::in_string;'),
                             ('add_resolved_initializations','++source_batch01::batch_calls; source_batch01::string_batch_calls += source_batch01::in_string;'),
                             ('intern_resolved_endpoint_path','++source_batch01::intern_calls; source_batch01::string_intern_calls += source_batch01::in_string;'),
                             ('ensure_initialization_target_index_capacity','++source_batch01::index_prepares;')]:
        pos=s.index('server_status graph::'+function+'(')
        pos=s.index(' noexcept {',pos)+len(' noexcept {')
        s=s[:pos]+'\n#ifdef CW_SOURCE_STRING_BATCH_AUDIT_01\n    '+counter+'\n#endif\n'+s[pos:]
    pos=s.index('server_status graph::upsert_initialization(')
    pos=s.index(' noexcept {',pos)+len(' noexcept {')
    s=s[:pos]+'''
#ifdef CW_SOURCE_STRING_BATCH_AUDIT_01
    ++source_batch01::upserts;
    if (source_batch01::in_string) {
        ++source_batch01::string_upserts;
        if (source_batch01::fault == "mid" && source_batch01::string_upserts == 17) {
            source_batch01::fault_fired=true; return server_status::io_error;
        }
    }
#endif
'''+s[pos:]
    return '#include "../source_batch01.hpp"\n'+s
edit('server_engine/project/graph/graph.cpp',graph)

def benchmark(s):
    s=replace(s,'    diagnostics.sort_deterministic();', '''#ifdef CW_SOURCE_STRING_BATCH_AUDIT_01
    if (!source_batch01::fault.empty()) {
        if (cw::server::succeeded(status) || project || !source_batch01::fault_fired) return 4;
        for (const auto* path : {&artifacts.compiled, &artifacts.runtime, &artifacts.manifest,
                                &artifacts.source_save, &artifacts.database})
            if (std::filesystem::exists(*path)) return 4;
        cw::server::fixed_shared_memory probe;
        if (probe.create(settings.shm.name, cw::server::fixed_shared_memory::size_alignment(),
                static_cast<std::uintptr_t>(settings.shm.fixed_base_address)) !=
                cw::server::fixed_shared_memory_result::success) return 4;
        source_batch01::print(std::cout);
        std::cout << "expected_failure=1,unpublished=1,artifacts_removed=1,shm_released=1\\n";
        return 0;
    }
#endif
    diagnostics.sort_deterministic();
    if (std::getenv("SOURCE_STRING_BATCH_01_DIAGNOSTICS"))
        cw::server::format_diagnostics(std::cerr, diagnostics);''')
    s=replace(s,'    std::cout\n        << "mode="', '''#ifdef CW_SOURCE_STRING_BATCH_AUDIT_01
    source_batch01::print(std::cout);
#endif
    std::cout
        << "mode="''')
    return '#include "project/source_batch01.hpp"\n'+s
edit('benchmarks/publish_benchmark.cpp',benchmark)
with (root/'CMakeLists.txt').open('a') as f:
    f.write('''
get_target_property(batch_sources ServerEngineV4PublishBenchmark SOURCES)
add_executable(ServerEngineV4StringBatchAudit ${batch_sources})
foreach(p INCLUDE_DIRECTORIES COMPILE_OPTIONS COMPILE_DEFINITIONS LINK_LIBRARIES INTERPROCEDURAL_OPTIMIZATION_RELEASE LINK_OPTIONS)
 get_target_property(v ServerEngineV4PublishBenchmark ${p})
 if(v AND NOT v MATCHES "NOTFOUND$")
  set_property(TARGET ServerEngineV4StringBatchAudit PROPERTY ${p} "${v}")
 endif()
endforeach()
target_compile_definitions(ServerEngineV4StringBatchAudit PRIVATE CW_SOURCE_STRING_BATCH_AUDIT_01=1)
''')
manifest={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file()}
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2))
(out/'snapshot.json').write_text(json.dumps(dict(git_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
    description='Current V2/Fast Reader, same-binary scalar/batch string commit; separate graph counter/failure target.'),indent=2))
print(root)
