"""Generate a diagnostic copy with exactly five observer hook sites."""
import hashlib,json
from pathlib import Path
root=Path('build/project-unload-address-09')
source=Path('experiments/project-unload-thread-06/replay.cpp')
text=source.read_text();original=text
text=text.replace('#include <cstring>','#include <cstring>\n#include "replay_checkpoint09.hpp"')
anchor='metadataBytes+10000*sizeof(Worker));'
assert text.count(anchor)==1
text=text.replace(anchor,anchor+'pause09("baseline");')
anchor='live);fflush(stdout);}'
assert text.count(anchor)==1
text=text.replace(anchor,'live);fflush(stdout);if(e.id<4){const char* names[]={"b00","b01","b02","b03"};pause09(names[e.id]);}}')
output=root/'replay_address09.cpp'
with output.open('x') as stream:stream.write(text)
(root/'source-identity.json').write_text(json.dumps(dict(parent_source=str(source),parent_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),diagnostic_source_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),changes='Include observer header; one baseline hook; boundary hooks for IDs 0..3 only.'),indent=2))
