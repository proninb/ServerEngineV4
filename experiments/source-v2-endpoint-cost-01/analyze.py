"""Read-only reanalysis of saved Source ETW stacks; no benchmark or profiler."""
from pathlib import Path
import collections,csv,hashlib,json,sys
repo=Path(__file__).resolve().parents[2]
source=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
assert source.is_relative_to(repo/'build') and out.is_relative_to(repo/'build') and not out.exists()
out.mkdir(parents=True)
analysis=source/'capture/analysis'
timings=list(csv.DictReader((source/'capture/measurements.csv').open()))
launches=[r for r in timings if r['capture']=='sampled']
allowed={(int(r['block']),int(r['pid'])) for r in launches}
count_launches=len(launches)
metadata=[json.loads(p.read_text()) for p in sorted(analysis.glob('b*-samples.jsonl.meta.json'))]
assert len(metadata)==4 and all(m['sample_interval_ms']==1 and m['events_lost']==0 and not m['truncated'] for m in metadata)

def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

files=sorted(analysis.glob('b*-samples.jsonl'))
input_hashes={str(p.relative_to(repo)):sha(p) for p in files}
partition=collections.Counter();children=collections.Counter();context_inclusive=collections.Counter()
context_self=collections.Counter();intern_callers=collections.Counter();intern_parents=collections.Counter()
context_blocks=collections.defaultdict(collections.Counter)
intern_blocks=collections.defaultdict(collections.Counter)
path_callers=collections.Counter();partition_blocks=collections.defaultdict(collections.Counter)
endpoint_total=intern_total=intern_own=path_total=path_inside=source_total=0

def endpoint_child(child):
    if child=='<own leaf>':return 'own read_endpoint leaf'
    if any(p in child for p in ('source_parser_v2::advance','semantic_input::','source_parser_v2::peek_kind')):
        return 'lexical/token processing, includes intern'
    if 'source_endpoint_resolver::' in child:return 'typed member/index traversal'
    if 'identity_space::' in child or 'string_table::contains' in child:return 'name/ID lookup and validation'
    if 'source_map::add_dependency' in child:return 'dependency registration'
    if 'intern_resolved_endpoint_path' in child:return 'endpoint-path interning'
    return 'other observed child / OS / inlined remainder'

for path in files:
    for line in path.open():
        row=json.loads(line)
        if (row['block'],row['pid']) not in allowed:continue
        frames=row['frames'];n=row['count']
        if not frames or not any('source_parser_v2::' in f or 'parse_source_roots_v2' in f for f in frames):continue
        source_total+=n
        ep=next((i for i,f in enumerate(frames) if 'source_parser_v2::read_endpoint' in f),None)
        if ep is not None:
            endpoint_total+=n
            child=frames[ep+1] if ep+1<len(frames) else '<own leaf>'
            children[child]+=n;category=endpoint_child(child)
            partition[category]+=n;partition_blocks[row['block']][category]+=n
        si=next((i for i,f in enumerate(frames) if 'string_table::intern_prehashed' in f),None)
        if si is not None:
            intern_total+=n
            context=('read_endpoint on stack' if ep is not None else
                     'object() on stack, outside endpoint' if any('source_parser_v2::object' in f for f in frames) else
                     'assignment() on stack, outside endpoint' if any('source_parser_v2::assignment' in f for f in frames) else
                     'other Source context')
            context_inclusive[context]+=n;context_blocks[row['block']][context]+=n
            if si==len(frames)-1:context_self[context]+=n;intern_own+=n
            intern_callers[frames[si-1] if si else '<root>']+=n
            intern_parents[frames[si-2] if si>1 else '<root>']+=n
            intern_blocks[row['block']][context]+=n
        pi=next((i for i,f in enumerate(frames) if 'graph::intern_resolved_endpoint_path' in f),None)
        if pi is not None:
            path_total+=n
            if ep is not None:path_inside+=n
            path_callers[tuple(frames[max(0,pi-3):pi+1])]+=n
assert sum(partition.values())==endpoint_total
assert sum(context_inclusive.values())==intern_total
assert sum(context_self.values())==intern_own
partition.setdefault('endpoint-path interning',0)
result=dict(source_launches=count_launches,source_samples=source_total,
    endpoint_inclusive_samples=endpoint_total,endpoint_inclusive_cpu_ms=endpoint_total/count_launches,
    endpoint_intern_union_samples=endpoint_total+intern_total-context_inclusive['read_endpoint on stack'],
    endpoint_intern_union_cpu_ms=(endpoint_total+intern_total-context_inclusive['read_endpoint on stack'])/count_launches,
    endpoint_disjoint_partition={k:dict(samples=v,cpu_ms_per_publish=v/count_launches,
        percent_read_endpoint_cpu=100*v/endpoint_total) for k,v in partition.items()},
    endpoint_immediate_callees=dict(children.most_common()),endpoint_partition_by_block={str(k):dict(v) for k,v in partition_blocks.items()},
    intern_inclusive_samples=intern_total,intern_exclusive_samples=intern_own,
    intern_contexts={k:dict(inclusive_samples=v,exclusive_samples=context_self[k],
        inclusive_cpu_ms_per_publish=v/count_launches,exclusive_cpu_ms_per_publish=context_self[k]/count_launches,
        share_intern_inclusive=100*v/intern_total) for k,v in context_inclusive.items()},
    intern_immediate_callers=dict(intern_callers),intern_parent_callers=dict(intern_parents),
    intern_context_by_block={str(k):dict(v) for k,v in intern_blocks.items()},
    path_intern=dict(total_samples=path_total,inside_read_endpoint_samples=path_inside,
        outside_cpu_ms_per_publish=(path_total-path_inside)/count_launches,
        caller_stacks=[dict(frames=list(k),samples=v) for k,v in path_callers.most_common()]),
    caveats=['CPU sample weights, not call counts or exact per-call timing.',
        'Endpoint partition uses the first observed child frame; hidden inline work is not reconstructed.',
        'Caller context is not token semantic role: buffered/current root names may be interned before read_endpoint.',
        'Zero observed inlined frames is not proof of zero execution; separate code inspection locates canonicalization.',
        'No new timing series, candidate, cache, source counters or profiling sessions.'])
selected=['server_engine/project/parser/source_parser_v2.cpp','server_engine/project/parser/source_endpoint_resolver.hpp',
    'server_engine/project/frontend/semantic_input.cpp','server_engine/project/frontend/source_token.hpp',
    'server_engine/project/graph/graph.cpp','server_engine/project/string/string_table.cpp',
    'server_engine/project/string/string_table.hpp','server_engine/project/semantic/identity.cpp','CMakeLists.txt']
code={p:dict(frozen_sha256=sha(source/'source'/p),current_sha256=sha(repo/p)) for p in selected}
assert all(r['frozen_sha256']==r['current_sha256'] for r in code.values())
assert all(sha(repo/p)==h for p,h in input_hashes.items())
(out/'summary.json').write_text(json.dumps(result,indent=2))
(out/'provenance.json').write_text(json.dumps(dict(saved_trace= str(source/'capture'),
    stack_hashes_before_after=input_hashes,code_identity=code,application_symbols=json.loads((source/'capture/manifest.json').read_text())['symbols'],
    new_workload_runs=0,new_cpu_captures=0,production_modified=False),indent=2))
with (out/'endpoint-partition.csv').open('w',newline='') as f:
    w=csv.writer(f);w.writerow(['group','samples','cpu_ms_per_publish','percent_endpoint'])
    for k,v in partition.items():w.writerow([k,v,v/count_launches,100*v/endpoint_total])
print(json.dumps({k:v for k,v in result.items() if k in ('endpoint_disjoint_partition','intern_contexts','path_intern')},indent=2))
