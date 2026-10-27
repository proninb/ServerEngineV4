"""Source-only CPU evidence; keep inclusive overlap and ICF callers explicit."""
from pathlib import Path
import collections,csv,json,statistics,sys
out=Path(sys.argv[1]).resolve(); capture=out/'capture'; analysis=capture/'analysis'
timings=list(csv.DictReader((capture/'measurements.csv').open()))
allowed={(int(r['block']),int(r['pid'])) for r in timings if r['capture']=='sampled'}
source_pids={pid for _,pid in allowed}
metadata=[json.loads(p.read_text()) for p in analysis.glob('b*-samples.jsonl.meta.json')]
assert len(metadata)==4 and all(m['sample_interval_ms']==1 for m in metadata)
assert all(m['events_lost']==0 and not m['truncated'] for m in metadata)
rules={
 'read_endpoint':lambda f:'source_parser_v2::read_endpoint' in f,
 'graph_commit':lambda f:any(p in f for p in ('graph::add_resolved_','graph::rebind_resolved_','graph::upsert_',
      'graph::insert_link_target_index','graph::ensure_link_target_index_capacity',
      'graph::ensure_initialization_target_index_capacity','graph::find_initialization_position','graph::find_link_target')),
 'insert_link_target_index':lambda f:'graph::insert_link_target_index' in f,
 'string_identity':lambda f:'string_table::' in f or 'identity_space::' in f,
 'source_tokens':lambda f:'semantic_input::' in f or 'source_parser_v2::advance' in f or 'source_parser_v2::peek_kind' in f,
 'kernel_mm':lambda f:f.startswith('ntoskrnl!') and f.split('!',1)[1].startswith(('Mi','Mm','KiPageFault')),
 'user_heap':lambda f:f.startswith('ntdll!') and any(p in f for p in ('Heap','Alloc','Free')),
}
groups=collections.Counter();leaves=collections.Counter();blocks=collections.Counter();aliases=collections.Counter()
index_callers=collections.Counter(); total=0;kernel=0;system_unresolved=0;per_process=collections.Counter()
for path in sorted(analysis.glob('b*-samples.jsonl')):
    for line in path.open():
        row=json.loads(line)
        if (row['block'],row['pid']) not in allowed:continue
        frames=row['frames'];n=row['count'];weight=row['weight_ms']
        if not frames or not any('source_parser_v2::' in f or 'parse_source_roots_v2' in f for f in frames):continue
        total+=n;blocks[row['block']]+=n;per_process[row['pid']]+=weight
        leaves[frames[-1]]+=n
        if frames[-1].startswith('ntoskrnl!'):kernel+=n
        if '+0x' in frames[-1] and not frames[-1].startswith('serverenginev4publishbenchmark'):system_unresolved+=n
        for name,match in rules.items():
            if any(match(f) for f in frames):groups[name]+=n
        if 'vector<cw::server::link_record' in frames[-1]:aliases[tuple(frames[-4:])]+=n
        if 'insert_link_target_index' in frames[-1]:index_callers[tuple(frames[-4:])]+=n
# Windows can reuse a PID across blocks; count launches, not distinct PID values.
processes=sum(r['capture']=='sampled' for r in timings)
table={name:dict(inclusive_samples=count,percent_source_cpu=100*count/total,
                estimated_cpu_ms_per_publish=count/processes) for name,count in groups.items()}
functions=json.loads((analysis/'cpu-summary.json').read_text())['functions']
selected={}
for name in ('read_endpoint','intern_prehashed','insert_link_target_index','graph::add_resolved_link'):
    values=[(f,r) for f,r in functions.items() if name in f]
    selected[name]=[dict(function=f,**r,exclusive_cpu_ms_per_publish=r['exclusive_samples']/processes,
                        inclusive_cpu_ms_per_publish=r['inclusive_samples']/processes) for f,r in values]
replay=[]
for line in (out/'index-replay.log').read_text().splitlines():
    if not line.startswith('round='):continue
    fields={k:float(v) for k,v in (p.split('=',1) for p in line.split(','))}
    if not fields['warmup']:replay.append(fields)
assert len(replay)==32
micro={key:dict(median=statistics.median(r[key] for r in replay),mean=statistics.mean(r[key] for r in replay),
               minimum=min(r[key] for r in replay),maximum=max(r[key] for r in replay))
       for key in ('wall_ms','user_ms','kernel_ms','thread_cycles')}
micro.update(rounds=32,links_per_round=367144,
    component='Production rebind_resolved_link on fresh targets: lookup(s), append, index growth/insertion; no Parser',
    caveat='Contiguous warmed inputs and small active footprint differ from full PUBLISH cache pressure. Not pure insert timing or predicted speedup. CPU kernel/user per-round accounting is quantized.')
report=dict(source_samples=total,source_cpu_weight_ms_per_publish=sum(per_process.values())/processes,
    source_blocks=dict(blocks),processes=processes,inclusive_groups=table,selected_functions=selected,
    kernel_leaf_samples=kernel,unresolved_system_leaf_samples=system_unresolved,
    icf_push_back_callers=[dict(frames=list(k),samples=v) for k,v in aliases.most_common()],
    index_insert_leaf_callers=[dict(frames=list(k),samples=v) for k,v in index_callers.most_common()],
    top_exclusive_functions=[dict(function=k,samples=v,percent_source_cpu=100*v/total) for k,v in leaves.most_common(30)],
    wall={mode:{key:dict(mean=statistics.mean(float(r[key]) for r in timings if r['capture']==mode),
                       median=statistics.median(float(r[key]) for r in timings if r['capture']==mode))
                for key in ('total_ms','source_ms')} for mode in ('control','sampled')},
    isolated_link_commit=micro,
    notes=['Inclusive groups overlap; do not sum them.',
           'Sample CPU weights are estimates and not wall times or exact per-call durations.',
           'IPO/ICF names are attributed using observed callers, not invented inline frames.',
           'Kernel MM CPU is already included in Source CPU samples.',
           'Missing stacks and unresolved system leaves bound attribution quality.'])
(out/'critical-summary.json').write_text(json.dumps(report,indent=2))
print(json.dumps(dict(samples=total,processes=processes,groups=table,micro=micro),indent=2))
