#!/usr/bin/env python3
import argparse,csv,math,statistics
from pathlib import Path
ap=argparse.ArgumentParser()
ap.add_argument('--directory',type=Path,required=True)
ap.add_argument('--pairs',type=int,required=True)
ap.add_argument('--warmups',type=int,required=True)
ap.add_argument('--no-allocations',action='store_true')
a=ap.parse_args()
def load(name):
    with (a.directory/name).open(newline='',encoding='utf-8-sig') as f:return list(csv.DictReader(f))
def val(s):
    x=float(s)
    if not math.isfinite(x):raise ValueError('nonfinite numeric value')
    return x
try:
    pairs=load('v341-pairs.csv');summary=load('v341-summary.csv');phases=load('v341-phases.csv');types=load('v341-type-samples.csv');resolution=load('v341-resolution.csv');fields=load('v341-fields.csv')
    if (len(pairs),len(summary),len(phases),len(types),len(resolution),len(fields))!=(a.pairs+a.warmups,1,1,1,1,1):raise ValueError('report counts differ')
    if any(r['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' or r['full_publish_gate']!='PARTIAL' for r in pairs):raise ValueError('pair parity failed')
    if any(r['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' for r in (summary[0],phases[0],types[0],resolution[0],fields[0])):raise ValueError('profile parity failed')
    if summary[0]['full_publish_gate']!='PARTIAL':raise ValueError('invalid PUBLISH flag')
    r=resolution[0]
    if r['full_publish_gate']!='PARTIAL':raise ValueError('invalid resolution PUBLISH flag')
    if (int(r['named_cache_hits'])+int(r['named_cache_misses'])+
        int(r['named_rejected_by_grammar'])!=int(r['named_attempts'])):
        raise ValueError('named branch partition mismatch')
    if sum(int(r[k]) for k in ('fallback_qualified','fallback_contextual_int64','fallback_modifiers','fallback_other'))!=int(r['fallback_attempts']):
        raise ValueError('fallback category partition mismatch')
    if int(r['fallback_success'])+int(r['fallback_failed'])!=int(r['fallback_attempts']):raise ValueError('fallback outcome partition mismatch')
    if int(r['sampled_fields'])<int(r['named_attempts']):raise ValueError('sampled fields mismatch')
    if sum(int(r[k]) for k in ('shape_failed','layers_zero','layers_one','layers_two_plus'))!=int(r['shape_attempts']):
        raise ValueError('shape category partition mismatch')
    if int(r['shape_attempts'])>int(r['sampled_fields']):raise ValueError('shape count exceeds sampled fields')
    f=fields[0]
    if f['full_publish_gate']!='PARTIAL':raise ValueError('fields PUBLISH flag invalid')
    for k in ('sampled_fields','sampled_records','append_calls','register_calls','member_lookup_calls'):
        if int(f[k])<1:raise ValueError('fields missing nonzero '+k)
    if int(f['sampled_fields'])!=int(types[0]['sampled_field_types']) or int(f['sampled_records'])!=int(types[0]['sampled_records']):raise ValueError('field counts inconsistent with type profile')
    if int(f['append_calls'])!=int(f['sampled_fields']) or int(f['register_calls'])!=int(f['sampled_fields']):raise ValueError('field counts inconsistent with appends')
    if int(f['construction_writes'])>int(f['sampled_fields']) or int(f['reference_bindings'])>int(f['sampled_fields']):raise ValueError('field writes exceed fields')
    if any(val(f[k])<0 for k in f if k.endswith('_ms_sampled')):raise ValueError('negative fields sampled time')
    diffs=[]; ctrl=[]; cand=[];orders={}
    for i,r in enumerate(pairs):
        warm=i<a.warmups
        if r['phase']!=('warmup' if warm else 'measure'):raise ValueError('warmup/measure mismatch')
        expected=i+1 if warm else i-a.warmups+1
        if int(r['pair'])!=expected:raise ValueError('pair index mismatch')
        old=val(r['v338a_ms']);new=val(r['v341_ms']);saved=val(r['saved_ms'])
        if min(old,new)<=0 or abs(old-new-saved)>0.005:raise ValueError('saved mismatch')
        if not warm:
            diffs.append(saved);ctrl.append(old);cand.append(new)
            orders.setdefault(r['order'],[]).append(saved)
    if len(orders)!=2:raise ValueError('missing AB/BA counterbalance')
    wins=sum(x>0 for x in diffs);med=statistics.median(diffs)
    if abs(med-val(summary[0]['median_paired_saved_ms']))>0.005 or wins!=int(summary[0]['paired_wins']):raise ValueError('summary mismatches pairs')
    if abs(statistics.median(ctrl)-val(summary[0]['median_v338a_ms']))>0.005 or abs(statistics.median(cand)-val(summary[0]['median_v341_ms']))>0.005:raise ValueError('median mismatch')
    if int(summary[0]['pairs'])!=a.pairs or int(summary[0]['warmups'])!=a.warmups:raise ValueError('count mismatch')
    if not a.no_allocations:
        allocations=load('v341-allocations.csv')
        if len(allocations)!=2 or [r['variant'] for r in allocations]!=['v338a','v341'] or any(r['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' for r in allocations):raise ValueError('allocation parity mismatch')
    else:allocations=[]
    n=sum(x!=0 for x in diffs); k=min(wins,n-wins)
    sign_p=min(1.,2*sum(math.comb(n,i) for i in range(k+1))/(2**n)) if n else 1.
    cut=sorted(diffs)[1:-1] if len(diffs)>2 else diffs
    md=['# V3-41 — sampled Fields instrumentation','',
      'Контроль: frozen V3-38A; диагностическая V3-41 только замеряет Fields.',
      f'- Пар: {len(diffs)}; побед: {wins}; sign test p={sign_p:.6f}',
      f'- Median control: {statistics.median(ctrl):.6f} ms',
      f'- Median diagnostic: {statistics.median(cand):.6f} ms',
      f'- Median paired saved: {med:.6f} ms',
      f'- Median без экстремальных пар: {statistics.median(cut):.6f} ms',
      '- A/B показывает стоимость инструментирования, НЕ ускорение.',
      '- Порядок A/B: '+', '.join(f'{key}={statistics.median(vals):.6f} ms' for key,vals in sorted(orders.items())),
      '',f'- Sampled records: {f["sampled_records"]}; sampled fields: {f["sampled_fields"]}',
      f'- Fields-phase sample: {f["fields_phase_ms_sampled"]} ms',
      f'- produce_type sample: {f["produce_type_ms_sampled"]} ms',
      f'- Member append sample: {f["member_append_ms_sampled"]} ms',
      f'- Member lookup sample: {f["member_lookup_ms_sampled"]} ms',
      f'- Initialization sample: {f["initializer_ms_sampled"]} ms',
      f'- Construction write sample: {f["construction_write_ms_sampled"]} ms',
      '', 'Вложенные таймеры неаддитивны; профилированное время не экстраполировать на весь проект.',
      'Graph parity: PASS; full PUBLISH: PARTIAL.','']
    path=a.directory/'v341-review.md'
    with path.open('x',encoding='utf-8') as f:f.write('\n'.join(md))
    print(f'V3-41 REVIEW PASS pairs={len(diffs)} wins={wins} median_paired_saved={med:.6f} sign_p={sign_p:.6f}')
except (ValueError,OSError,KeyError,TypeError) as ex:
    ap.exit(1,'V3-41 REVIEW FAIL: '+str(ex)+'\n')
