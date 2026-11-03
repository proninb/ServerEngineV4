"""Frozen-source inventory + conservative ownership review. No production mutation.

Lexical inventory is intentionally not an AST/call-graph or complete allocation proof.
Unknown sites and implicit library/nested paths remain explicit.
"""
import bisect,hashlib,json,re
from collections import Counter
from pathlib import Path
ROOT=Path('build/project-unload-memory-01');SOURCE=ROOT/'source';OUT=Path('build/backing-coverage-13')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def scrub(text):
 pattern=r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27'
 clean=re.sub(pattern,lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text)
 return re.sub(r'^[ \t]*#[^\n]*',lambda m:' '*len(m[0]),clean,flags=re.M)
def scope_map(clean):
 scopes=[];result=[];delimiter=0
 for m in re.finditer(r'[{};\n]',clean):
  char=m[0]
  if char=='{':
   prefix=clean[max(delimiter,m.start()-2000):m.start()].strip();name=None
   function=re.search(r'([~A-Za-z_]\w*(?:::[~A-Za-z_]\w*)*)\s*\([^{};]*\)\s*(?:(?:const|noexcept|override|final)\s*)*$',prefix)
   if function and function[1] not in {'if','for','while','switch','catch','noexcept','sizeof','alignof','decltype'}:name='function:'+function[1]
   cls=re.search(r'\b(class|struct|namespace)\s+([\w:]+)[^;{}()]*$',prefix)
   if cls:name=cls[1]+':'+cls[2]
   scopes.append(name);delimiter=m.end()
  elif char=='}':
   if scopes:scopes.pop()
   delimiter=m.end()
  elif char==';':delimiter=m.end()
  else:result.append(' / '.join(x for x in scopes if x) or 'unresolved lexical scope')
 return result
PROFILES={
 'project/string/string_table':('Construction','String/identity lookup and packed-image encoder; BUILD baseline mapping must outlive borrowed view.','Operation completion (PUBLISH/BUILD/REBUILD)','No resident transfer of canonicalization vectors; spelling views expire with storage.','Semantic owner mutates; do not grow byte storage while workers hold views.'),
 'project/semantic/identity':('Construction','Semantic resolver and encoder; strings owner outlives identity_space.','Operation completion (PUBLISH/BUILD/REBUILD)','IDs persisted by value; no owning vector transfers to resident.','Mutation serialized in reviewed semantic path; baseline/string references remain alive.'),
 'project/graph/graph':('Construction','Parser/Semantic, physical-plan preparation and compiled-image encoder.','Operation completion (PUBLISH/BUILD/REBUILD)','Graph records persisted; G/index buffers do not become resident storage.','No concurrent graph mutation inferred; plan workers read only stable completed graph.'),
 'project/source/source_map':('Construction','Semantic provenance, dependency processing and compiled/source persistence.','Operation completion (PUBLISH/BUILD/REBUILD)','Provenance serialized; views must not survive owned vectors or baseline maps.','Root-local mutation/index scratch needs the same boundary as its source-map owner.'),
 'project/assign/assign_table':('Construction','Assign parser and persistence.','Operation completion (PUBLISH/BUILD/REBUILD)','Strings/records serialize; do not transfer scratch as resident strings.','Control/semantic owner; returned string_views borrow byte storage.'),
 'project/file/file_context':('Construction','Acquisition/lexer/parser and persistence, including baseline overlays.','Operation completion (PUBLISH/BUILD/REBUILD)','Worker snapshot bytes move into file_context only, not resident project.','Acquisition workers own results; join before owner commits/mutates paths/topology or destroys buffers.'),
 'project/frontend/lexical_generation':('Construction','Lexer, Header/Source parser and lexical persistence; baseline maps remain borrowed.','Operation completion (PUBLISH/BUILD/REBUILD)','Arena pointers/predecoded token views must not escape; serialized records only.','Lane-owned arenas/per-file outputs; no arena reset while any consumer is active.'),
 'project/shm/shm_layout':('Construction','Physical plans/materialization and prepare_runtime_bindings.','Runtime preparation completion, no later than operation return','Copy/detach resident offset vectors; scratch allocations cannot be stolen into a shorter-lived domain.','Read-only after plan completion; join plan/materialization consumers before reset.'),
 'project/shm/shm_runtime_v2':('Construction','Runtime materialization; persisted spans borrow the physical-plan mapping.','Runtime preparation completion, no later than operation return','Final SHM values survive, not temporary plan arrays.','Lane-specific result/output ownership; join before v2_runtime reset.'),
 'project/runtime/runtime_binding':('Resident','Runtime query and IC operations through project.runtime_bindings().','UNLOAD (or failure rollback before publication)','prepare_runtime_bindings output moves into project; allocate for Resident from the start.','Prepared before publication; resident readers must finish before UNLOAD.'),
}
def policy(path):
 relative=path.removeprefix('server_engine/');stem=relative.rsplit('.',1)[0]
 return PROFILES.get(stem)
def inventory():
 manifest=json.loads((ROOT/'source-manifest.json').read_text(encoding='utf-8'))
 for name,digest in manifest.items():
  if sha(SOURCE/name)!=digest:raise ValueError('snapshot mismatch '+name)
 sites=[];files=[]
 types=re.compile(r'\bstd::(?:pmr::)?(?:vector|basic_string|unordered_map|unordered_set|map|set|deque|list|unique_ptr|shared_ptr)\s*<|\bstd::(?:string|wstring)\b(?!_view)|\bstd::filesystem::path\b')
 operations=re.compile(r'\bnew\b|\b(?:malloc|calloc|realloc|aligned_alloc|VirtualAlloc(?:Ex)?|HeapAlloc|HeapReAlloc|CreateFileMapping[AW]?|MapViewOfFile(?:Ex)?|mmap|mremap)\s*\(|\bstd::make_(?:unique|shared)\s*<|\.\s*(?:reserve|resize|assign|append|insert|emplace(?:_back)?|push_back|push_front|rehash|try_emplace|insert_or_assign)\s*\(')
 for name in sorted(manifest):
  if not name.startswith('server_engine/') or not name.endswith(('.cpp','.hpp')):continue
  file=SOURCE/name;text=file.read_text(encoding='utf-8-sig');clean=scrub(text);lines=text.splitlines();contexts=scope_map(clean);newlines=[m.start() for m in re.finditer('\n',clean)]
  files.append(dict(file=name,sha256=manifest[name]))
  hits=sorted([(m.start(),'dynamic_storage_spelling',m[0]) for m in types.finditer(clean)]+[(m.start(),'potential_allocation_operation',m[0]) for m in operations.finditer(clean)])
  for offset,kind,token in hits:
   line=bisect.bisect_right(newlines,offset)+1;profile=policy(name);declaration=None;reference=False
   if kind=='dynamic_storage_spelling':
    end=offset+len(token)
    if token.rstrip().endswith('<'):
     depth=1
     while end<len(clean) and depth:
      depth+= (clean[end]=='<')-(clean[end]=='>');end+=1
    tail=clean[end:end+120];reference=bool(re.match(r'\s*(?:const\s*)?[&*]',tail))
    identifier=re.match(r'\s*([A-Za-z_]\w*)',tail);declaration=identifier[1] if identifier else None
    if reference:kind='borrowed_storage_reference'
   snippet=' '.join(lines[line-1:min(len(lines),line+6)]).strip()[:650]
   owner,last,boundary,transfer,worker=profile or ('Unknown','Call-site/escape review required.','Unknown; do not infer from allocation time.','Unknown; reusable output parameters and callbacks require caller review.','Unknown until caller and barrier are established.')
   backing='STL default allocator / C++ new / CRT; exact implicit call path not proven from spelling alone.'
   physical='No guaranteed page return: destructor/old-capacity deallocation is logical only; shared-heap commit may remain.'
   if re.match(r'(?:CreateFileMapping|MapViewOfFile|mmap)',token):backing='OS mapping API';physical='Mapping owner must unmap/close; API cleanup and success/failure path require call-site review.'
   elif re.match(r'(?:VirtualAlloc|HeapAlloc|HeapReAlloc|malloc|calloc|realloc)',token):backing='Explicit API: '+token.strip();physical='Matching release and ownership not inferred automatically.'
   if kind=='borrowed_storage_reference':backing='No allocation implied by reference declaration';physical='Borrow owner must outlive use; no backing ownership conferred.'
   sites.append(dict(id=f'S{len(sites)+1:05}',file=name,line=line,lexical_scope=contexts[line-1] if line<=len(contexts) else 'unresolved',
    operation=token,category=kind,storage_name_candidate=declaration,source_excerpt=snippet,logical_owner=owner,
    owner_basis='Reviewed component policy for known context usages; caller-sensitive outputs/locals still require per-site confirmation.' if profile else 'Unreviewed / caller-sensitive; Unknown retained.',
    backing_api=backing,last_consumer=last,lifetime_boundary=boundary,transfer=transfer,worker_safety=worker,physical_release_proof=physical,
    semantic_site_status='Lexical candidate, not an exact runtime allocation count; constructor/move/shrink/reference cases may not allocate.',
    preprocessor_activation='All frozen source branches inventoried; active configuration not inferred.'))
 return manifest,files,sites
def main():
 manifest,files,sites=inventory()
 result=dict(status='SOURCE INVENTORY COMPLETE / SEMANTIC COVERAGE OPEN / IMPLEMENTATION NOT AUTHORIZED',snapshot_manifest_sha256=sha(ROOT/'source-manifest.json'),manifest_files_verified=len(manifest),scanned_cpp_headers=len(files),
  categories=dict(Counter(s['category'] for s in sites)),owner_component_policies=dict(Counter(s['logical_owner'] for s in sites)),
  completeness='Not a call graph/AST or closed-world allocation proof. Hidden STL/CRT/filesystem/thread/exception allocations, aliases, initializer/assignment/operator[] growth and reusable caller-owned outputs need additional review. Lexical candidates are not traffic coverage.',
  files=files,sites=sites)
 with (OUT/'allocation-sites.json').open('x',encoding='utf-8') as out:json.dump(result,out,indent=2,ensure_ascii=False)
 print(json.dumps({k:v for k,v in result.items() if k not in ('files','sites')},indent=2))
if __name__=='__main__':main()
