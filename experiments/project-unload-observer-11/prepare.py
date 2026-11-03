import hashlib,json
from pathlib import Path
root=Path('build/project-unload-observer-11');parent=Path('experiments/project-unload-thread-06/replay.cpp');text=parent.read_text()
assert text.count('#include <cstring>')==1;text=text.replace('#include <cstring>','#include <cstring>\n#include "observer11.hpp"')
anchor='metadataBytes+10000*sizeof(Worker));';assert text.count(anchor)==1;text=text.replace(anchor,anchor+'observer11("baseline");')
anchor='live);fflush(stdout);}';assert text.count(anchor)==1;text=text.replace(anchor,'live);fflush(stdout);if(e.id<4){const char* names[]={"b00","b01","b02","b03"};observer11(names[e.id]);}}')
output=root/'replay_observer11.cpp'
with output.open('x') as stream:stream.write(text)
(root/'source-identity.json').write_text(json.dumps(dict(parent_source=str(parent),parent_sha256=hashlib.sha256(parent.read_bytes()).hexdigest(),diagnostic_source_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),observer_header_sha256=hashlib.sha256(Path('experiments/project-unload-observer-11/observer11.hpp').read_bytes()).hexdigest(),changes='Observer include plus baseline and boundary 0..3 calls; no heap/lifetime logic changes.'),indent=2))
