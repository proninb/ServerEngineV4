"""Aggregate normalized *real* ETW full-stack rows; never infer hidden IPO frames.

JSONL schema: block, pid, frames (root->leaf), count, weight_ms (total row weight).
WPA/export normalization is deliberately explicit: tree subtotals must not be fed
here as independent stacks, or both CPU counts and inclusive totals would double.
"""
import argparse
import collections
import csv
import json
import math
import pathlib
import re

def category(frame):
    rules=[('cursor',('semantic_input::','source_parser_v2::advance','source_parser_v2::peek_kind')),
      ('binding',('identity_space::find','identity_space::resolve','identity_space::record','source_parser_v2::find_visible')),
      ('string_intern',('string_table::',)),
      ('object_lookup',('graph::find_object','source_endpoint_resolver::object_type')),
      ('path_intern',('graph::intern_resolved_endpoint_path','graph::ensure_endpoint_path_index_capacity','graph::find_endpoint_path','graph::insert_endpoint_path_index','graph::intern_endpoint_path')),
      ('provenance',('source_map::',)),
      ('graph_write',('graph::add_resolved_','graph::complete_resolved_extern','graph::rebind_resolved','graph::upsert_','graph::insert_link_target_index','graph::ensure_link_target_index_capacity','graph::ensure_initialization_target_index_capacity','graph::find_initialization_position','graph::find_link_target')),
      ('member_transition',('source_endpoint_resolver::','graph::find_member','graph::resolve_member')),
      ('endpoint',('source_parser_v2::read_endpoint',))]
    return next((name for name,patterns in rules if any(p in frame for p in patterns)),'other')

def aggregate(records,allowed):
    group=collections.defaultdict(lambda:{'inclusive_samples':0,'exclusive_samples':0,'inclusive_ms':0.,'exclusive_ms':0.})
    functions=collections.defaultdict(lambda:{'inclusive_samples':0,'exclusive_samples':0})
    quality=dict(target_samples=0,source_samples=0,missing_stack_samples=0,unresolved_application_samples=0)
    blocks=set()
    for row in records:
        if (int(row['block']),int(row['pid'])) not in allowed:continue
        count=row['count'];weight=float(row['weight_ms']);frames=row['frames']
        assert isinstance(count,int) and count>0 and math.isfinite(weight) and weight>=0
        assert isinstance(frames,list) and all(isinstance(f,str) for f in frames)
        quality['target_samples']+=count
        if not frames:quality['missing_stack_samples']+=count;continue
        if any(re.search(r'ServerEngineV4PublishBenchmark(?:\.exe)?\+0x',f,re.I) for f in frames):quality['unresolved_application_samples']+=count
        source=any(any(p in f for p in ('parse_source_roots_v2','source_parser_v2::','source_endpoint_resolver::','source_predecode_batch::')) for f in frames)
        if not source:continue
        quality['source_samples']+=count;blocks.add(int(row['block']))
        inclusive={category(f) for f in frames}-{'other'}
        if not inclusive:inclusive={'other'}
        for name in inclusive:group[name]['inclusive_samples']+=count;group[name]['inclusive_ms']+=weight
        leaf=category(frames[-1]);group[leaf]['exclusive_samples']+=count;group[leaf]['exclusive_ms']+=weight
        for frame in set(frames):functions[frame]['inclusive_samples']+=count
        functions[frames[-1]]['exclusive_samples']+=count
    assert sum(r['exclusive_samples'] for r in group.values())==quality['source_samples']
    quality['source_blocks']=len(blocks)
    return dict(quality=quality,groups=dict(group),functions=dict(functions))

def main():
    p=argparse.ArgumentParser();p.add_argument('samples',type=pathlib.Path);p.add_argument('measurements',type=pathlib.Path);p.add_argument('output',type=pathlib.Path)
    p.add_argument('--quality',type=pathlib.Path,help='Measured trace-loss/symbol audit JSON; absence prevents acceptance')
    args=p.parse_args()
    with args.measurements.open() as f:allowed={(int(r['block']),int(r['pid'])) for r in csv.DictReader(f) if r['capture']=='sampled'}
    with args.samples.open() as f:result=aggregate((json.loads(line) for line in f if line.strip()),allowed)
    audit=json.loads(args.quality.read_text()) if args.quality else {}
    q=result['quality'];result['trace_audit']=audit
    result['status']='needs_quality_audit'
    if q['source_samples']<10000 or q['source_blocks']<4:result['status']='insufficient_samples'
    elif audit.get('lost_events')==0 and audit.get('lost_stacks')==0 and audit.get('symbol_audited') is True and q['unresolved_application_samples']==0 and q['missing_stack_samples']==0:result['status']='ready_for_review'
    result['note']='Inclusive groups overlap. Exclusive classification uses the actual sampled leaf; no reconstruction of missing inline frames. CPU weight is not wall time.'
    args.output.write_text(json.dumps(result,indent=2))
    print(json.dumps({'status':result['status'],'quality':q},indent=2))

if __name__=='__main__':main()
