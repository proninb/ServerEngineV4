"""Isolated copies; remove RG01 observers while retaining its field-free friend."""
import hashlib,json,re,sys
from pathlib import Path
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
def strip_hooks(s):
 lines=s.splitlines(keepends=True);result=[];depth=0
 for line in lines:
  if not depth and line.strip()=='#ifdef CW_RESOLVED_G_01':depth=1;continue
  if depth:
   if re.match(r'\s*#\s*(if|ifdef|ifndef)\b',line):depth+=1
   elif re.match(r'\s*#\s*endif\b',line):depth-=1
   continue
  result.append(line)
 assert depth==0
 return ''.join(result)
manifest={}
for relative in ('graph/graph.cpp','parser/source_parser_v2.cpp'):
 p=repo/'server_engine/project'/relative
 s=strip_hooks(p.read_text(encoding='utf-8-sig'))
 s=re.sub(r'#include "([^"]+)"',lambda m:'#include "'+(p.parent/m[1]).resolve().as_posix()+'"',s)
 if relative.startswith('parser'):
  s='#include "'+(repo/'experiments/bulk-materialization-01/bulk.hpp').as_posix()+'"\n'+s
  needle='endpoint_resolver(g, t ? &t->source_member_lookups : nullptr) {'
  assert s.count(needle)==1;s=s.replace(needle,needle[:-2]+', bulk(g) {')
  needle='    source_endpoint_resolver endpoint_resolver;'
  assert s.count(needle)==1;s=s.replace(needle,needle+'\n    bulk_materialization01::batch bulk;')
  needle='    [[nodiscard]] server_status validate_externs() noexcept {'
  assert s.count(needle)==1;s=s.replace(needle,'    server_status finish_bulk() noexcept {return bulk.finish();}\n\n'+needle)
  assert s.count('G.rebind_resolved_link(')==1
  s=s.replace('G.rebind_resolved_link(', 'bulk.rebind(')
  assert s.count('G.add_resolved_initialization(')==1
  s=s.replace('G.add_resolved_initialization(', 'bulk.add_initialization(')
  assert s.count('G.add_resolved_initializations(')==1
  s=s.replace('G.add_resolved_initializations(', 'bulk.add_initializations(')
  needle='    if (!succeeded(externs_valid)) return externs_valid;'
  assert s.count(needle)==1;s=s.replace(needle,needle+'\n    const auto materialized=parser.finish_bulk();\n    if(!succeeded(materialized))return materialized;')
 (out/p.name).write_text(s,encoding='utf-8')
 manifest[relative]=hashlib.sha256(p.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
