"""Hash-verified source inventory; regex findings are review leads, not ownership proof."""
import hashlib
import json
from pathlib import Path
import re

root = Path('build/project-unload-memory-01')
source = root/'source'
manifest = json.loads((root/'source-manifest.json').read_text())
bad = [name for name,digest in manifest.items()
       if not (source/name).is_file() or hashlib.sha256((source/name).read_bytes()).hexdigest()!=digest]
assert not bad, bad
patterns = {
    'resident_owner': r'unique_ptr<project>|project\.reset\(',
    'construction': r'full_construction_context|manifest_composer',
    'potential_escape': r'shared_ptr|weak_ptr|add_subscription|subscription_registration|std::function|\[.*(?:&|this).*\]',
}
findings = []
for name in manifest:
    if not name.startswith('server_engine/') or not name.endswith(('.cpp','.hpp')):
        continue
    for number,line in enumerate((source/name).read_text(errors='replace').splitlines(),1):
        kinds = [kind for kind,pattern in patterns.items() if re.search(pattern,line)]
        if kinds: findings.append(dict(file=name,line=number,kinds=kinds,text=line.strip()))
out=root/'lifetime-owners-03'
out.mkdir(exist_ok=True)
(out/'source-audit.json').write_text(json.dumps(dict(manifest_files_verified=len(manifest),
    ownership_verdict='OPEN: inventory requires manual C++ lifetime review',findings=findings),indent=2))
print(len(manifest),'files verified;',len(findings),'review leads')
