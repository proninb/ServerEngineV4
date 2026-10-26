"""Freeze production sources and add private same-executable A/B controls."""
from pathlib import Path
import hashlib, json, shutil, subprocess, sys

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo / 'build') and not out.exists(), out
out.mkdir(parents=True)
root = out / 'source'
for name in ('server_engine', 'benchmarks', 'tests'):
    shutil.copytree(repo / name, root / name)
helper = root / 'experiments/runtime-object-lanes-01'
helper.mkdir(parents=True)
shutil.copy2(repo / 'experiments/runtime-object-lanes-01/shm_audit.hpp', helper)

def edit(relative, before, after):
    path = root / relative
    source = path.read_text()
    assert source.count(before) == 1, (relative, before)
    path.write_text(source.replace(before, after))

edit('server_engine/project/file/file_context.cpp',
     '        publish_fast_acquisition = true;',
     '''        const auto* control = std::getenv("FAST_ACQUIRE_02_REFERENCE");
        publish_fast_acquisition = !(control && std::string_view{control} == "1");''')
path = root / 'server_engine/project/file/file_context.cpp'
path.write_text('#include <cstdlib>\n' + path.read_text())
edit('server_engine/project/persistence/project_artifact.cpp',
     '        output.manifest = output.root / files.manifest;',
     '''        if (const auto* directory = std::getenv("FAST_ACQUIRE_02_ARTIFACT_ROOT"))
            output.root = std::filesystem::path{directory};
        output.manifest = output.root / files.manifest;''')
path = root / 'server_engine/project/persistence/project_artifact.cpp'
path.write_text('#include <cstdlib>\n' + path.read_text())
edit('benchmarks/publish_benchmark.cpp',
     '#ifdef CW_RUNTIME_OBJECT_LANES_AB\n#include "../experiments/runtime-object-lanes-01/shm_audit.hpp"',
     '#if defined(CW_RUNTIME_OBJECT_LANES_AB) || defined(CW_FAST_ACQUIRE_02)\n#include "../experiments/runtime-object-lanes-01/shm_audit.hpp"')
edit('benchmarks/publish_benchmark.cpp',
     '#ifdef CW_RUNTIME_OBJECT_LANES_AB\n    std::string shm_audit_sha;',
     '#if defined(CW_RUNTIME_OBJECT_LANES_AB) || defined(CW_FAST_ACQUIRE_02)\n    std::string shm_audit_sha;')
edit('benchmarks/publish_benchmark.cpp',
     '    if (!shm_audit_sha.empty())\n        std::cout << ",shm_audit_sha256="',
     '#endif\n#if defined(CW_RUNTIME_OBJECT_LANES_AB) || defined(CW_FAST_ACQUIRE_02)\n    if (!shm_audit_sha.empty())\n        std::cout << ",shm_audit_sha256="')
(root / 'CMakeLists.txt').write_text((repo / 'CMakeLists.txt').read_text() + '''
target_compile_definitions(ServerEngineV4PublishBenchmark PRIVATE CW_FAST_ACQUIRE_02=1)
if(MSVC)
 foreach(t ServerEngineV4PublishBenchmark ServerEngineV4SourceSaveTests ServerEngineV4FrontendTests ServerEngineV4Tests)
  target_compile_options(${t} PRIVATE /MP4)
 endforeach()
endif()
''')
manifest = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file()}
(out / 'source-manifest.json').write_text(json.dumps(manifest, indent=2))
(out / 'snapshot.json').write_text(json.dumps(dict(
    git_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
    description='Production fast acquisition plus private reference/artifact/audit controls only.'), indent=2))
print(root)
