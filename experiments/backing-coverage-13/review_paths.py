"""Source-anchored reviewed backing paths, distinct from lexical candidate inventory."""
import hashlib,json
from pathlib import Path
from audit import SOURCE,OUT,PROFILES,policy
inventory=json.loads((OUT/'allocation-sites.json').read_text(encoding='utf-8'));rows=[]
def add(file,line,operation,owner,last,boundary,transfer,workers,proof,nested,eligibility,function):
 text=(SOURCE/file).read_text(encoding='utf-8-sig').splitlines()
 if not 0<line<=len(text):raise ValueError('invalid anchor')
 api='Default STL allocator/C++ new/CRT'
 if 'mapping.create' in operation or 'mapping.open' in operation or 'shared_memory.create' in operation:api='Win32 mapping wrapper: CreateFileMapping/MapViewOfFile[Ex], view/handle cleanup'
 elif operation.startswith('new'):api='Explicit C++ new/new[] (nothrow where shown), default CRT backing'
 elif 'make_unique' in operation:api='std::make_unique -> C++ new; nested members use independent allocation paths'
 rows.append(dict(id=f'R{len(rows)+1:04}',file=file,line=line,function=function,operation=operation,
  logical_owner=owner,backing_api=api,last_consumer=last,
  lifetime_boundary=boundary,transfer=transfer,worker_safety=workers,physical_release_proof=proof,
  nested_allocation_paths=nested,construction_domain_eligibility=eligibility,
  evidence='Source inspection of declaration/operation and reviewed owner/consumer path; no physical-release claim from deallocation alone.',
  source_excerpt=' '.join(text[line-1:line+4])[:650]))
classes={'string_table','identity_space','graph','source_map','source_map_delta','file_context','lexical_generation','lexical_arena','assign_table','shm_layout','shm_runtime_v2','shm_type_batch','runtime_binding_index'}
for site in inventory['sites']:
 scopes=site['lexical_scope'].split(' / ');scope=scopes[-1]
 cls=scope.split(':')[-1]
 if site['category']!='dynamic_storage_spelling' or not site['storage_name_candidate'] or cls not in classes or not site['file'].endswith('.hpp'):continue
 if 'function:' in site['lexical_scope']:continue
 profile=policy(site['file'])
 if cls=='shm_type_batch':profile=('Construction','Type plan preparation, runtime materialization and physical-plan encoder.','Materialization/operation completion','Persisted spans borrow runtime plan image; temporary owned arrays do not transfer.','Plan owner mutates before publication; physical worker consumers must finish.')
 if not profile:continue
 owner,last,boundary,transfer,worker=profile;name=site['storage_name_candidate'];nested=[]
 if cls=='file_context' and name=='owned_content':nested=['vector<unique_ptr<string>> slot array','separately allocated string object','string character buffer, moved from worker-acquired snapshot; may use SSO for small data']
 elif cls=='lexical_generation' and name=='direct_source_tokens':nested=['outer vector buffer','per-file vector<source_predecoded_token> buffers']
 elif cls=='lexical_generation' and name=='arena_values':nested=['lexical_arena[] owner array','each arena.words uint32_t[]','each arena.directives lexical_directive_anchor[]']
 elif cls=='source_map' and 'unordered' in site['operation']:nested=['hash bucket array','individual nodes/rehash temporary bucket allocation']
 elif 'unordered' in site['operation']:nested=['hash buckets','nodes']
 elif name=='completed_roots' and 'vector' in site['operation']:nested=['vector<bool> bit-packed storage; allocator path is not vector<uint8_t>']
 else:nested=['separate old/new capacities during growth; element-owned dynamic state must be checked independently']
 add(site['file'],site['line'],'Owning storage '+cls+'::'+name,owner,last,boundary,transfer,worker,
  'Logical destructor/reset/growth cleanup visible; no independent committed-page release on the default CRT heap.',nested,
  'Candidate only after nested/growth/borrow/fault coverage is closed' if owner=='Construction' else 'EXCLUDE from Construction; Resident allocation from first ownership',
  cls+' storage declaration; allocation occurs through constructor/mutator sites in allocation-sites.json')

def explicit(relative,line,operation,owner,last,boundary,transfer,worker,nested=None,proof=None,eligibility=None,function=None):
 file='server_engine/'+relative
 candidates=[s for s in inventory['sites'] if s['file']==file and s['line']==line]
 lexical=candidates[0]['lexical_scope'] if candidates else 'See exact source anchor; manually reviewed caller'
 add(file,line,operation,owner,last,boundary,transfer,worker,proof or 'C++ deallocation only; physical commit return NOT PROVEN.',nested or [],eligibility or ('Construction candidate, not yet allocator-covered' if owner=='Construction' else 'Not eligible for a Construction-only domain'),function or lexical)

explicit('project/string/string_table.cpp',299,'candidate index(capacity) then index = move(candidate)','Construction','find/intern and persistence','Operation completion','Index stays in string_table; borrowed spellings do not own backing.','Semantic owner; no concurrent mutation.', ['new index capacity overlaps old capacity'])
explicit('project/string/string_table.cpp',440,'stable_value.assign(...) alias-protection scratch','Construction','intern_prehashed consumes stable spelling','Function return','No resident transfer','Semantic owner',['std::string character storage independent of all three table vectors'])
explicit('project/string/string_table.cpp',460,'bytes.insert + records.push_back','Construction','Canonical string lookup/encoding','Operation completion','Serialized string IDs/data only','No mutation while external views in use')
explicit('project/semantic/identity.cpp',436,'candidate index(capacity) then index = move(candidate)','Construction','resolve/find','Operation completion','IDs persist by value; no scratch transfer','Semantic owner',['rehash old/new capacities coexist'])
for line,method,target in [(360,'ensure_records','lexical_record[]'),(364,'ensure_records','lexical_directive_record[]'),(415,'ensure_words','uint32_t[]'),(455,'ensure_directives','lexical_directive_anchor[]'),(494,'reset','lexical_record[]'),(495,'reset','lexical_directive_record[]'),(503,'reset','lexical_arena[]'),(546,'bind_baseline','lexical_arena[]')]:
 explicit('project/frontend/lexical_generation.cpp',line,'new (nothrow) '+target,'Construction','Lexer/parser/persistence consuming the generation','Generation/operation completion','Move only within lexical_generation; no resident pointer escape','Arena lane owns writes; join consumers before move/reset',['old and candidate arrays overlap; moving unique_ptr does not change backing'],function='lexical_generation::'+method)
explicit('project/file/file_identity.cpp',1108,'std::string bytes(file_size, zero)','Construction','ReadFile then lexer/parser through file_context snapshot','Operation completion after worker commit','bytes -> acquire output -> snapshot -> file_context::owned_content; Construction to Construction','Worker allocates; owner thread may later destroy after join',['file-size char buffer; string object/SSO distinct'],function='acquire_file_content_fast')
explicit('project/file/file_context.cpp',2265,'owned_content.resize(index+1)','Construction','File-content accessor and persistence','Operation completion','Owns pointer slots only; does not allocate nested strings through same allocator','Owner after acquisition completion',['outer unique_ptr slot array'])
explicit('project/file/file_context.cpp',2268,'make_unique<string>(move(snapshot.bytes))','Construction','Lexer/parser/file-content views','Operation completion','Preserves worker char buffer backing even when string object moves to file_context','Cross-thread allocation/free supported by design, not TLS routing',['string object allocation','transferred worker character buffer'])
explicit('project/project_configuration_manifest.cpp',253,'vector<jthread> + reserve/emplace_back','Construction','Acquisition job completion','Reader helper return','No worker escapes helper scope','jthread destruction joins on normal/unwinding paths',['vector storage','CRT/thread startup state and OS stack; not just outer vector'],proof='jthread/OS cleanup plus vector deallocation; page-return coverage/fault paths OPEN')
explicit('project/frontend/semantic_input.hpp',116,'prepared_include/include frontier and lookup caches','Construction','Header include replay and lexing','semantic_input/operation completion','No resident transfer; paths, snapshot strings and lexical_stream arrays nested','include_workers must stop before frontier/caches backing disappears',['map buckets/nodes','filesystem paths','file_acquire_result snapshot bytes','lexical_stream unique_ptr arrays'],function='semantic_input (see actual declaration anchors in allocation-sites.json)')
for line,field in [(227,'object_offsets'),(232,'member_offsets'),(237,'base_offsets')]:
 explicit('project/runtime/runtime_binding.cpp',line,'next.'+field+'.assign(...)','Resident','Runtime query and IC','UNLOAD or failed creation unwind','next -> output -> bindings -> project by move','Prepare before resident publication; no readers after UNLOAD',function='prepare_runtime_bindings')
explicit('project/runtime/project_runtime.cpp',1555,'make_unique<project>','Resident','Runtime queries/operations','UNLOAD','Moves compiled mapping/SHM/bindings; copies path into Resident storage','Control thread publishes after materialization barriers',['project object','filesystem path storage','IC catalog added later'],function='create_resident_project')
explicit('project/preprocessor_configuration.hpp',24,'predefines/include_directories/root_directory owning state','Construction','Preprocessor/directive execution','Operation completion','No resident config view','Workers borrow immutable config only',['vector slots','predefine name/replacement strings','include directory strings','filesystem::path storage'],function='preprocessor_configuration / predefine_configuration')
explicit('project/project_configuration_manifest.hpp',73,'manifest.files vector of file proofs','Construction','Persistence/verification of composed project input','Operation completion','Persisted values only','Composer mutates; readers require stable manifest',['vector storage','each file_proof.path native-string storage'],function='project_configuration_manifest')
for line,operation in [(171,'new(nothrow) lane_pool_state'),(186,'new(nothrow) lane_worker[]')]:
 explicit('project/construction/execution_lanes_windows.cpp',line,operation,'Construction','Synchronous run + pool destruction','Helper/operation end','Raw job context is borrowed, never owns Project','start/run/destroy_state signal/join paths; wait failure coverage OPEN',['worker array or owner state','event handles and _beginthreadex CRT/stack allocations outside these new sites'],function='execution_lanes::start')
explicit('communication/connection/communication_connection.cpp',89,'new communication_connection','Connection','Endpoint/registry/session/request processing','Connection close after final lifetime token','Connection lifetime tokens can outlive Project but not borrow destroyed Project','Server control + transport, reference counting/callback barriers',['session strings','outbound queue nodes/payloads','outstanding-request unordered_set','subscription vector/registrations'],function='communication_connection::create')
explicit('communication/connection/outbound_channel.hpp',89,'deque<outbound_message> queue','Connection','Transport writer or queue discard','Write/request completion or connection close','Messages may cross PUBLISH return as owned copies, not borrowed Project views','Producer/writer synchronization and closure',['deque map/block arrays','variant server_response/client payload nested strings/vectors'],function='outbound_channel storage')
explicit('communication/tcp/tcp_endpoint.cpp',437,'peer.receive_buffer vector','Connection','Frame/protocol parser','Buffer reuse/connection close','No Project transfer','TCP worker owns peer transport buffers',function='tcp_endpoint::implementation::peer')
explicit('communication/tcp/tcp_endpoint.cpp',440,'peer.send_buffer string','Connection','Socket writer','Write completion/reuse/connection close','Owned serialized response, not Project memory','TCP worker owns peer transport buffers',function='tcp_endpoint::implementation::peer')
explicit('communication/tcp/tcp_endpoint.cpp',1315,'make_unique<implementation>','Server','TCP listener/worker lifetime','Endpoint stop/Server SHUTDOWN','Peer metadata lives under endpoint; per-peer buffers have Connection lifetime','Endpoint worker must stop/join before implementation destruction',['implementation','peers vector','nested per-peer buffers','worker CRT state'],function='tcp_endpoint constructor')
for line,op in [(375,'compiled writable mapping.create'),(480,'runtime writable mapping.create')]:
 explicit('project/project_full_construction.cpp',line,op,'Construction','Image encoding/persistence and final re-open','Helper/operation completion','File bytes persist; temporary mapping owner does not transfer','Encoding/physical-plan workers joined before unmapping',proof='Windows mapping owner reset unmaps/closes, separate from CRT heap deallocation; failures still require native error coverage.',function='Full-construction persistence helper')
explicit('project/project_full_construction.cpp',1602,'resident_mapping.open(final compiled.bin)','Resident','compiled_project_view and resident queries','UNLOAD or creation failure','read-only mapping moves into project; view points inside that mapping','Publish after bind; retire borrowers before unmap',proof='RAII mapping reset uses UnmapViewOfFile/CloseHandle; does not guarantee unrelated CRT PrivateUsage.',function='construct_full_project')
explicit('project/project_full_construction.cpp',1639,'resident_runtime_mapping.open(runtime.bin)','Construction','create_resident_project runtime materialization','create_resident_project completion','Physical-plan spans temporary; runtime mapping not a field in resident project','Materialization jobs finish before temporary runtime mapping reset',proof='Mapping RAII unmap/close; lifetime differs from resident compiled mapping.',function='construct_full_project')
explicit('project/runtime/project_runtime.cpp',1178,'shared_memory.create(...)','Resident','Native runtime system/queries','UNLOAD or creation failure','Final mapping moves into project','Materialization writes before publication; all resident users retire before reset',proof='fixed_shared_memory RAII unmaps/closes this process handle; other-process views are separately owned.',function='create_resident_project')
explicit('project/graph/constructor_defaults.hpp',77,'graph.constructor_defaults.values vector','Construction','Constructor default lookup and persistence','Operation completion','Containing graph owns this composite child; no resident vector transfer','Semantic owner; readers require stable values',['This allocation is not one of graph.hpp direct std::vector fields'],function='constructor_default_table::add / values')
explicit('project/frontend/lexical_stream.cpp',61,'new uint32_t[] candidate in reserve_words','Construction','Per-file lexer and lexical publication','Stream consumption/destruction','May be nested in include frontier; tokens output vectors are separately owned','Lane-local stream; owner joins before publication/reset',['Old words + replacement coexist'],function='lexical_stream::reserve_words')
explicit('project/frontend/lexical_stream.cpp',108,'new lexical_directive_anchor[] candidate','Construction','Directive executor and lexical publication','Stream consumption/destruction','Same Construction only','Lane-local stream',['Old directives + candidate coexist'],function='lexical_stream::reserve_directives')
explicit('project/frontend/preprocessor_v2.hpp',304,'resolutions vector<unique_ptr<resolution_record>>','Construction','Preprocessed Header/include traversal','Root preprocessing/operation completion','No resident transfer; callbacks borrow caller scope','No pool release while include/reuse callbacks can execute',['outer vector','resolution_record allocations','any nested record-owned paths must be separately reviewed'],function='preprocessor_v2 owning members')
explicit('project/parser/source_endpoint_resolver.hpp',293,'Conditional prepared_records.resize + resolver-local transition vectors','Construction','Source endpoint resolution','Resolver/parser scope','No resident cache transfer','Parser owner; not a process-global cache',['Activation of experimental macros must be verified separately'],function='source_endpoint_resolver::remember_preparation / cache fields')
explicit('project/ic/ic_catalog.hpp',23,'project.ic_catalog_value.items and nested entry strings/paths','Resident','IC operations after Project publication','UNLOAD or temporary replacement completion','Loaded local catalogue moves into Project via set_ic_catalog; JSON parse temporaries remain caller-sensitive','Server control mutation; no readers after Project teardown',['entry vector','description strings','filesystem paths; caller scratch not automatically Resident'],function='ic_catalog storage / server load/set_ic_catalog')

# Validate exact quoted anchors rather than silently using wrong line numbers.
expected_anchors={('project/frontend/semantic_input.hpp',116):'prepared_include',('communication/connection/outbound_channel.hpp',89):'queue'}
for (relative,line),token in expected_anchors.items():
 content=(SOURCE/'server_engine'/relative).read_text(encoding='utf-8-sig').splitlines()
 if token not in content[line-1]:
  candidates=[(i+1,t) for i,t in enumerate(content) if token in t and ('struct ' in t or 'std::deque' in t)]
  if len(candidates)!=1:raise ValueError(('anchor review needed',relative,candidates))
  corrected=candidates[0][0]
  for row in rows:
   if row['file']=='server_engine/'+relative and row['line']==line:
    row['line']=corrected;row['source_excerpt']=' '.join(content[corrected-1:corrected+4])[:650]
result=dict(status='REVIEWED OWNER/BACKING PATHS / FULL COVERAGE OPEN / IMPLEMENTATION NOT AUTHORIZED',reviewed_paths=rows,
 caveats=['Storage declaration is not a runtime allocation count. Concrete growth/new sites remain independently listed.',
 'Component policies describe reviewed construction uses, not every reusable API caller.',
 'Physical CRT backing release is unproven for all heap-backed paths; mappings are distinct.',
 'Header sites in inactive branches remain configuration-conditional; source inventory is not an active-build call graph.'])
with (OUT/'reviewed-allocation-paths.json').open('x',encoding='utf-8') as out:json.dump(result,out,indent=2)
print('Reviewed source-anchored paths:',len(rows))
