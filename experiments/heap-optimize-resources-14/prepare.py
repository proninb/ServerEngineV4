import pathlib,shutil,json,hashlib
root=pathlib.Path('build/heap-optimize-resources-14');old=pathlib.Path('build/project-unload-memory-01')
manifest=json.loads((old/'source-manifest.json').read_text(encoding='utf-8'))
assert all(hashlib.sha256((old/'source'/f).read_bytes()).hexdigest()==h for f,h in manifest.items())
shutil.copytree(old/'source',root/'source')
(root/'source-before.json').write_text(json.dumps(manifest,indent=2))
p=root/'source/server_engine/server.cpp';text=p.read_text(encoding='utf-8-sig')
assert text.count('#include "server.hpp"')==1
text=text.replace('#include "server.hpp"','#include "server.hpp"\n#include "heap_optimize_diagnostic.hpp"',1)
a=text.index('server_status server::unload(');b=text.index('void server::shutdown()',a);chunk=text[a:b]
needle='    context.project.reset();';assert chunk.count(needle)==1
chunk=chunk.replace(needle,needle+'\n    heap_optimize14::after_unload();')
p.write_text(text[:a]+chunk+text[b:],encoding='utf-8')
shutil.copy2('experiments/heap-optimize-resources-14/heap_optimize_diagnostic.hpp',root/'source/server_engine/heap_optimize_diagnostic.hpp')
changes=[f for f,h in manifest.items() if hashlib.sha256((root/'source'/f).read_bytes()).hexdigest()!=h]
assert changes==['server_engine/server.cpp']
(root/'source-delta.json').write_text(json.dumps(dict(changed=changes,added=['server_engine/heap_optimize_diagnostic.hpp'],scope='Diagnostic copy only; after successful UNLOAD project.reset before UNLOADED transition',production_pgo='OFF'),indent=2))
